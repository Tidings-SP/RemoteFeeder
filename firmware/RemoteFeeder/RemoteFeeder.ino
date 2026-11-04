/*
 * ============================================================
 *  RemoteFeeder – ESP32-S3 Automatic Fish Feeder
 * ============================================================
 *  Hardware:
 *    - ESP32-S3 (onboard LED on GPIO 48)
 *    - Motor driver: IRFZ44N MOSFET gate on GPIO 2
 *    - 12 V relay coil also driven from GPIO 2
 *      (relay in series with MOSFET drain – both fire together)
 *
 *  Firebase RTDB schema:
 *    /feeder
 *      /trigger       : bool   – web app writes true → ESP feeds, then resets
 *      /lastFedAt     : number – Unix timestamp (ms) of last feed (ESP writes)
 *      /lastFedSource : string – "manual" | "auto"
 *      /status        : string – "idle" | "feeding" | "offline"
 *      /espOnline     : bool   – heartbeat flag
 *      /wifiRSSI      : number – signal strength
 *
 *  Auto-feed logic:
 *    - Target time: 09:00 IST (UTC+5:30 = UTC+19800 s)
 *    - After any feed (manual or auto) lastFedAt is stored in
 *      NVS so it survives power cuts.
 *    - On boot: if lastFedAt is from a previous calendar day AND
 *      current time ≥ 09:00 → feed immediately (missed-feed recovery).
 *    - During normal operation: feeds at 09:00 only if no feed
 *      has occurred today, and no manual trigger has fired today.
 *
 *  Motor pattern: ON 1.5 s → OFF 0.5 s  × 3 cycles
 *
 *  Power-saving: WiFi radio turned OFF between 20:00–08:00 IST
 *    to reduce heat (ESP32-S3 on 5 V USB).
 *    Deep-sleep is NOT used because we need NTP time keeping and
 *    quick manual response; instead WiFi is simply disconnected.
 *    If you want deep-sleep instead, see the comment at the bottom.
 * ============================================================
 */

// ---- Arduino / ESP-IDF includes ----
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <Preferences.h>              // NVS for persistent storage

// ---- Firebase ESP32 Client ----
// Install: "Firebase ESP32 Client" by Mobizt (Library Manager)
#include <FirebaseESP32.h>
#include "secrets.h"

// ============================================================
//  Private configuration
// ============================================================
// Copy secrets.example.h to secrets.h and fill it in locally. secrets.h is
// ignored by Git so Wi-Fi and Firebase credentials never reach GitHub.
const char* WIFI_SSID     = FEEDER_WIFI_SSID;
const char* WIFI_PASSWORD = FEEDER_WIFI_PASSWORD;
const char* FIREBASE_HOST = FEEDER_FIREBASE_HOST;
const char* FIREBASE_AUTH = FEEDER_FIREBASE_AUTH;

// --- Hardware pins ---
const int PIN_MOTOR  = 2;   // IRFZ44N gate + relay coil
                             // ⚠ GPIO2 is a strapping pin on ESP32-S3.
                             // MUST have a 10 kΩ pull-down to GND on the
                             // PCB so the motor doesn't glitch during boot.
const int PIN_LED    = 48;  // Onboard RGB/LED on ESP32-S3

// --- Timezone: IST = UTC+5:30 ---
const long  TZ_OFFSET_SEC  = 5 * 3600 + 30 * 60;   // 19800
const int   DAYLIGHT_SEC   = 0;

// --- Auto-feed schedule ---
const int AUTO_FEED_HOUR   = 9;   // 09:00 IST
const int AUTO_FEED_MIN    = 0;

// --- WiFi sleep window: 20:00 – 08:00 IST (radio OFF) ---
const int WIFI_OFF_HOUR_START = 20;
const int WIFI_OFF_HOUR_END   = 8;

// --- Motor pattern ---
const int FEED_CYCLES       = 3;
const int MOTOR_ON_MS       = 1500;
const int MOTOR_OFF_MS      = 500;

// --- Timing intervals (ms) ---
const unsigned long FIREBASE_POLL_MS  = 2000;    // how often we check trigger
const unsigned long HEARTBEAT_MS      = 10000;   // online status heartbeat
const unsigned long WIFI_CHECK_MS     = 30000;   // reconnect check

// ============================================================
//  Globals
// ============================================================

FirebaseData   fbData;
FirebaseConfig fbConfig;
FirebaseAuth   fbAuth;
Preferences    prefs;

bool    wifiConnected   = false;
bool    firebaseReady   = false;
bool    timeSync        = false;
bool    wifiSleepMode   = false;    // true when radio is intentionally off

unsigned long lastFirebasePoll  = 0;
unsigned long lastHeartbeat     = 0;
unsigned long lastWifiCheck     = 0;

// NVS keys
const char* NVS_NS           = "feeder";
const char* NVS_LAST_FED     = "lastFed";   // stores Unix timestamp (seconds)
const char* NVS_FED_TODAY    = "fedToday";  // stores YYYY-MM-DD string

// ============================================================
//  Forward declarations
// ============================================================
void  connectWiFi();
void  initFirebase();
void  syncTime();
void  runFeedCycle(const char* source);
void  updateFeedRecord(const char* source);
bool  hasBeenFedToday();
void  markFedToday();
void  checkAutoFeed();
void  checkManualTrigger();
void  updateHeartbeat();
void  manageWifiSleep();
bool  isWifiSleepTime();
void  blinkLED(int times, int onMs, int offMs);
time_t getNowTimestamp();
String todayString();

// ============================================================
//  setup()
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[RemoteFeeder] Booting...");

  // --- Pin setup ---
  pinMode(PIN_MOTOR, OUTPUT);
  digitalWrite(PIN_MOTOR, LOW);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  // --- NVS ---
  prefs.begin(NVS_NS, false);

  // --- Connect WiFi ---
  connectWiFi();

  // --- Sync NTP time ---
  if (wifiConnected) {
    syncTime();
  }

  // --- Init Firebase ---
  if (wifiConnected) {
    initFirebase();
  }

  // --- Boot-time missed-feed check ---
  // If we crashed / had a power cut: check if today's auto-feed was missed
  if (timeSync) {
    checkAutoFeed();
  }

  blinkLED(3, 200, 200);  // startup signal
  Serial.println("[RemoteFeeder] Ready.");
}

// ============================================================
//  loop()
// ============================================================
void loop() {
  unsigned long now = millis();

  // 1. Manage WiFi sleep window
  manageWifiSleep();

  // 2. Reconnect WiFi if dropped (and not in sleep window)
  if (!wifiSleepMode && (now - lastWifiCheck >= WIFI_CHECK_MS)) {
    lastWifiCheck = now;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] Reconnecting...");
      wifiConnected = false;
      firebaseReady = false;   // Connection is dead – must re-init
      connectWiFi();
      if (wifiConnected && !timeSync) syncTime();
      if (wifiConnected) initFirebase();
    }
  }

  // 3. Firebase: poll manual trigger
  if (firebaseReady && !wifiSleepMode && (now - lastFirebasePoll >= FIREBASE_POLL_MS)) {
    lastFirebasePoll = now;
    checkManualTrigger();
  }

  // 4. Heartbeat
  if (firebaseReady && !wifiSleepMode && (now - lastHeartbeat >= HEARTBEAT_MS)) {
    lastHeartbeat = now;
    updateHeartbeat();
  }

  // 5. Auto-feed check (runs every loop iteration, cheap)
  if (timeSync) {
    checkAutoFeed();
  }

  delay(100);
}

// ============================================================
//  WiFi
// ============================================================
void connectWiFi() {
  if (isWifiSleepTime()) {
    Serial.println("[WiFi] In sleep window, skipping connect.");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WiFi] Connecting");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\n[WiFi] Connected: " + WiFi.localIP().toString());
  } else {
    wifiConnected = false;
    Serial.println("\n[WiFi] Failed – offline mode active.");
  }
}

// ============================================================
//  NTP time sync
// ============================================================
void syncTime() {
  configTime(TZ_OFFSET_SEC, DAYLIGHT_SEC, "pool.ntp.org", "time.google.com");
  Serial.print("[NTP] Syncing");

  struct tm ti;
  int attempts = 0;
  while (!getLocalTime(&ti) && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (getLocalTime(&ti)) {
    timeSync = true;
    Serial.println("\n[NTP] Synced: " + String(asctime(&ti)));
  } else {
    timeSync = false;
    Serial.println("\n[NTP] Sync failed.");
  }
}

// ============================================================
//  Firebase init
// ============================================================
void initFirebase() {
  fbConfig.host              = FIREBASE_HOST;
  fbConfig.signer.tokens.legacy_token = FIREBASE_AUTH;

  Firebase.begin(&fbConfig, &fbAuth);
  Firebase.reconnectWiFi(true);

  // Set stream timeout
  Firebase.setReadTimeout(fbData, 1000 * 60);
  Firebase.setwriteSizeLimit(fbData, "tiny");

  firebaseReady = true;

  // Mark device online and send initial heartbeat with lastSeen timestamp
  time_t now = getNowTimestamp();
  FirebaseJson json;
  json.set("espOnline", true);
  json.set("status", "idle");
  if (now > 100000) {
    json.set("lastSeen", (double)now * 1000.0);
  }
  Firebase.updateNode(fbData, "/feeder", json);

  Serial.println("[Firebase] Initialized.");
}

// ============================================================
//  Feed cycle  –  ON 1.5 s / OFF 0.5 s × 3
// ============================================================
void runFeedCycle(const char* source) {
  Serial.printf("[Feeder] Starting feed – source: %s\n", source);

  if (firebaseReady) {
    Firebase.setString(fbData, "/feeder/status", "feeding");
  }

  // Visual indicator: LED on during feeding
  digitalWrite(PIN_LED, HIGH);

  for (int i = 0; i < FEED_CYCLES; i++) {
    Serial.printf("[Feeder] Cycle %d/%d – motor ON\n", i + 1, FEED_CYCLES);
    digitalWrite(PIN_MOTOR, HIGH);
    delay(MOTOR_ON_MS);

    Serial.printf("[Feeder] Cycle %d/%d – motor OFF\n", i + 1, FEED_CYCLES);
    digitalWrite(PIN_MOTOR, LOW);
    if (i < FEED_CYCLES - 1) delay(MOTOR_OFF_MS);  // no pause after last cycle
  }

  digitalWrite(PIN_LED, LOW);

  // Update records
  updateFeedRecord(source);
  markFedToday();

  if (firebaseReady) {
    Firebase.setString(fbData, "/feeder/status", "idle");
  }

  Serial.println("[Feeder] Feed complete.");
  blinkLED(2, 100, 100);  // success blink
}

// ============================================================
//  Update lastFedAt in Firebase + NVS
// ============================================================
void updateFeedRecord(const char* source) {
  time_t now = getNowTimestamp();

  // NVS – persist across reboots
  prefs.putLong(NVS_LAST_FED, (long)now);

  if (firebaseReady) {
    // Store Unix ms timestamp for JS compatibility
    FirebaseJson json;
    json.set("lastFedAt",     (double)now * 1000.0);
    json.set("lastFedSource", source);
    Firebase.updateNode(fbData, "/feeder", json);
  }
}

// ============================================================
//  NVS helpers for "fed today" tracking
// ============================================================
String todayString() {
  struct tm ti;
  if (!getLocalTime(&ti)) return "";
  char buf[11];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &ti);
  return String(buf);
}

bool hasBeenFedToday() {
  String stored = prefs.getString(NVS_FED_TODAY, "");
  String today  = todayString();
  if (today.isEmpty()) return false;    // no time, can't decide
  return stored == today;
}

void markFedToday() {
  String today = todayString();
  if (!today.isEmpty()) prefs.putString(NVS_FED_TODAY, today);
}

// ============================================================
//  Auto-feed check
//  Called every loop iteration (cheap struct-tm check)
// ============================================================

void checkAutoFeed() {
  struct tm ti;
  if (!getLocalTime(&ti)) return;

  int h = ti.tm_hour;
  int m = ti.tm_min;

  // Already fed today (NVS-persisted) — nothing to do
  if (hasBeenFedToday()) return;

  // --- Boot-time missed-feed recovery ---
  // Power cut before 09:00 and restored after 09:00:
  //   hasBeenFedToday() is false, pastAutoTime is true → feed now
  // Power cut after 09:00 (already fed):
  //   hasBeenFedToday() returns true above → returns early, no double feed
  bool pastAutoTime = (h > AUTO_FEED_HOUR) ||
                      (h == AUTO_FEED_HOUR && m >= AUTO_FEED_MIN);

  if (pastAutoTime) {
    Serial.println("[AutoFeed] Feed due – triggering now.");
    runFeedCycle("auto");
    // runFeedCycle calls markFedToday(), so hasBeenFedToday()
    // will return true on the next iteration → no double fire.
  }
}

// ============================================================
//  Manual trigger via Firebase
// ============================================================
void checkManualTrigger() {
  if (!Firebase.getBool(fbData, "/feeder/trigger")) return;

  if (fbData.boolData()) {
    Serial.println("[Firebase] Manual trigger received!");

    // Reset trigger FIRST to avoid double-fire
    Firebase.setBool(fbData, "/feeder/trigger", false);

    // Run feed – mark as "manual" so auto-feed is also suppressed today
    runFeedCycle("manual");
  }
}

// ============================================================
//  Heartbeat – keep Firebase status up to date
// ============================================================
void updateHeartbeat() {
  int rssi = WiFi.RSSI();
  time_t now = getNowTimestamp();
  FirebaseJson json;
  json.set("espOnline", true);
  json.set("wifiRSSI",  rssi);
  if (now > 100000) {
    json.set("lastSeen", (double)now * 1000.0);
  }
  Firebase.updateNode(fbData, "/feeder", json);
}

// ============================================================
//  WiFi Sleep Window (20:00 – 08:00)
//  Turning off WiFi radio significantly reduces heat/power.
//  The ESP still runs, keeps NTP time, and can still auto-feed
//  at 09:00 when radio is back on.
// ============================================================

bool isWifiSleepTime() {
  struct tm ti;
  if (!getLocalTime(&ti)) return false;

  int h = ti.tm_hour;
  // Sleep window: 20:00–23:59  AND  00:00–07:59
  return (h >= WIFI_OFF_HOUR_START || h < WIFI_OFF_HOUR_END);
}

void manageWifiSleep() {
  bool shouldSleep = isWifiSleepTime();

  if (shouldSleep && !wifiSleepMode) {
    // Enter sleep: disconnect WiFi
    Serial.println("[WiFi] Entering sleep window – radio OFF.");

    if (firebaseReady) {
      Firebase.setBool(fbData,   "/feeder/espOnline", false);
      Firebase.setString(fbData, "/feeder/status",    "offline");
    }

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    wifiConnected   = false;
    firebaseReady   = false;
    wifiSleepMode   = true;

    // LED slow blink to indicate sleep mode (1 blink every 5 s handled below)
    blinkLED(1, 50, 50);
  }

  if (!shouldSleep && wifiSleepMode) {
    // Wake up: re-enable WiFi
    Serial.println("[WiFi] Sleep window over – radio ON.");
    wifiSleepMode = false;
    connectWiFi();
    if (wifiConnected) {
      syncTime();
      initFirebase();
    }
  }
}

// ============================================================
//  Helpers
// ============================================================

// NOTE: Named getNowTimestamp() to avoid collision with the
// ESP32 SDK's getLocalTime(struct tm*) which takes a pointer.
time_t getNowTimestamp() {
  struct tm ti;
  if (!getLocalTime(&ti)) return 0;
  return mktime(&ti);
}

void blinkLED(int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(PIN_LED, HIGH);
    delay(onMs);
    digitalWrite(PIN_LED, LOW);
    if (i < times - 1) delay(offMs);
  }
}

/*
 * ============================================================
 *  ALTERNATIVE: Deep-Sleep approach
 * ============================================================
 *  If you want the ESP32-S3 to actually deep-sleep and wake at
 *  09:00 to feed, replace the WiFi-sleep approach with:
 *
 *    1. After feeding (or at 20:00), calculate seconds until
 *       09:00 next morning.
 *    2. Call esp_sleep_enable_timer_wakeup(seconds * 1e6).
 *    3. Call esp_deep_sleep_start().
 *    4. On wake, NVS still holds lastFed, so the sketch can
 *       decide whether to feed.
 *
 *  Downside: manual Firebase triggers won't wake the ESP.
 *  You would need a separate GPIO wakeup pin wired to a button
 *  or a second always-on MCU for that.
 * ============================================================
 */

