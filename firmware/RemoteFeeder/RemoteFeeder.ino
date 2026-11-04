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
#include <esp_system.h>               // reset-reason diagnostics

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

// --- WiFi sleep windows (IST, radio OFF) ---
// Edit only these entries while testing. A window can cross midnight, such as
// 20:00 → 08:00. The end time is exclusive, so 16:30 means WiFi wakes at 16:30.
struct WifiSleepWindow {
  uint8_t startHour;
  uint8_t startMinute;
  uint8_t endHour;
  uint8_t endMinute;
};

const WifiSleepWindow WIFI_SLEEP_WINDOWS[] = {
  {20, 0,  8,  0},  // Night: 20:00 – 08:00
  {12, 0, 16, 30},  // Midday: 12:00 – 16:30
};
const size_t WIFI_SLEEP_WINDOW_COUNT =
  sizeof(WIFI_SLEEP_WINDOWS) / sizeof(WIFI_SLEEP_WINDOWS[0]);

// --- Motor pattern ---
const int FEED_CYCLES       = 3;
const int MOTOR_ON_MS       = 500;
const int MOTOR_OFF_MS      = 300;

// --- Timing intervals (ms) ---
const unsigned long FIREBASE_POLL_MS  = 2000;    // how often we check trigger
const unsigned long HEARTBEAT_MS      = 10000;   // online status heartbeat
const unsigned long WIFI_CHECK_MS     = 30000;   // reconnect check
const unsigned long NTP_RETRY_MS      = 300000;  // retry time sync every 5 min if needed

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
unsigned long lastTimeSyncAttempt = 0;

esp_reset_reason_t bootResetReason;
bool bootMetadataSynced = false;
bool wifiIssueOngoing   = false;
bool wifiIssuePending   = false;
String pendingWifiIssueReason;

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
bool  isWithinWifiSleepWindow(int currentMinutes, const WifiSleepWindow& window);
const char* resetReasonName(esp_reset_reason_t reason);
bool  isPowerRelatedReset(esp_reset_reason_t reason);
void  syncBootMetadata();
void  recordWifiIssue(const char* reason);
void  syncPendingWifiIssue();
void  blinkLED(int times, int onMs, int offMs);
time_t getNowTimestamp();
String todayString();

// ============================================================
//  setup()
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  bootResetReason = esp_reset_reason();
  Serial.println("\n[RemoteFeeder] Booting...");
  Serial.printf("[System] Reset reason: %s\n", resetReasonName(bootResetReason));

  // --- Pin setup ---
  pinMode(PIN_MOTOR, OUTPUT);
  digitalWrite(PIN_MOTOR, LOW);
  pinMode(PIN_LED, OUTPUT);
  rgbLedWrite(PIN_LED, 0,0,0);

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
      recordWifiIssue("WiFi connection lost");
      wifiConnected = false;
      firebaseReady = false;   // Connection is dead – must re-init
      connectWiFi();
      if (wifiConnected && !timeSync) syncTime();
      if (wifiConnected) initFirebase();
    }
  }

  // Retry NTP after a transient startup failure even if WiFi stays connected.
  // Without a valid clock, scheduled feeding and the WiFi sleep window cannot run.
  if (
    wifiConnected &&
    !wifiSleepMode &&
    !timeSync &&
    now - lastTimeSyncAttempt >= NTP_RETRY_MS
  ) {
    syncTime();
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
    wifiIssueOngoing = false;
    Serial.println("\n[WiFi] Connected: " + WiFi.localIP().toString());
  } else {
    wifiConnected = false;
    recordWifiIssue("WiFi connection attempt failed");
    Serial.println("\n[WiFi] Failed – offline mode active.");
  }
}

// ============================================================
//  NTP time sync
// ============================================================
void syncTime() {
  lastTimeSyncAttempt = millis();
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
    syncBootMetadata();
    syncPendingWifiIssue();
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

  syncBootMetadata();
  syncPendingWifiIssue();

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

  // Persist this before applying motor power. A brownout during a cycle can
  // reboot the ESP32; recording first prevents missed-feed recovery from
  // starting the motor again in a reboot loop. This intentionally favors
  // at-most-once feeding over a possible duplicate feed.
  markFedToday();

  // Visual indicator: LED on during feeding
  rgbLedWrite(PIN_LED, 2, 8, 6);

  for (int i = 0; i < FEED_CYCLES; i++) {
    Serial.printf("[Feeder] Cycle %d/%d – motor ON\n", i + 1, FEED_CYCLES);
    digitalWrite(PIN_MOTOR, HIGH);
    delay(MOTOR_ON_MS);

    Serial.printf("[Feeder] Cycle %d/%d – motor OFF\n", i + 1, FEED_CYCLES);
    digitalWrite(PIN_MOTOR, LOW);
    if (i < FEED_CYCLES - 1) delay(MOTOR_OFF_MS);  // no pause after last cycle
  }

  rgbLedWrite(PIN_LED, 0,0,0);

  // Update records
  updateFeedRecord(source);

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

    // Reset trigger FIRST to avoid double-fire. If the acknowledgement fails,
    // do not run the motor: retrying a still-true trigger is safer than a
    // possible duplicate feed after an uncertain write.
    if (!Firebase.setBool(fbData, "/feeder/trigger", false)) {
      Serial.printf(
        "[Firebase] Could not clear manual trigger: %s\n",
        fbData.errorReason().c_str()
      );
      return;
    }

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
  // On boot the ESP may retain a valid-looking UTC clock from before NTP sets
  // the IST offset. Never apply a sleep window until this run has synced time.
  if (!timeSync) return false;

  struct tm ti;
  if (!getLocalTime(&ti)) return false;

  int currentMinutes = ti.tm_hour * 60 + ti.tm_min;
  for (size_t i = 0; i < WIFI_SLEEP_WINDOW_COUNT; i++) {
    if (isWithinWifiSleepWindow(currentMinutes, WIFI_SLEEP_WINDOWS[i])) {
      return true;
    }
  }
  return false;
}

bool isWithinWifiSleepWindow(int currentMinutes, const WifiSleepWindow& window) {
  int startMinutes = window.startHour * 60 + window.startMinute;
  int endMinutes = window.endHour * 60 + window.endMinute;

  // Equal start/end would otherwise mean an accidental all-day sleep window.
  if (startMinutes == endMinutes) return false;
  if (startMinutes < endMinutes) {
    return currentMinutes >= startMinutes && currentMinutes < endMinutes;
  }
  return currentMinutes >= startMinutes || currentMinutes < endMinutes;
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

const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:  return "Power-on reset";
    case ESP_RST_EXT:      return "External reset";
    case ESP_RST_SW:       return "Software reset";
    case ESP_RST_PANIC:    return "Panic reset";
    case ESP_RST_INT_WDT:  return "Interrupt watchdog";
    case ESP_RST_TASK_WDT: return "Task watchdog";
    case ESP_RST_WDT:      return "Other watchdog";
    case ESP_RST_DEEPSLEEP:return "Deep-sleep wake";
    case ESP_RST_BROWNOUT: return "Brownout";
    case ESP_RST_SDIO:     return "SDIO reset";
    default:               return "Unknown reset";
  }
}

bool isPowerRelatedReset(esp_reset_reason_t reason) {
  return reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT;
}

void syncBootMetadata() {
  if (!firebaseReady || bootMetadataSynced) return;

  time_t now = getNowTimestamp();
  if (now <= 100000) return;

  FirebaseJson json;
  json.set("lastBootAt", (double)now * 1000.0);
  json.set("lastBootReason", resetReasonName(bootResetReason));
  if (isPowerRelatedReset(bootResetReason)) {
    // A power-on/brownout reset is the closest reliable on-device signal for
    // a power interruption. Other reset causes keep the previous power event.
    json.set("lastPowerCutAt", (double)now * 1000.0);
    json.set("lastPowerCutReason", resetReasonName(bootResetReason));
  }

  bootMetadataSynced = Firebase.updateNode(fbData, "/feeder", json);
}

void recordWifiIssue(const char* reason) {
  if (wifiIssueOngoing) return;

  wifiIssueOngoing = true;
  wifiIssuePending = true;
  pendingWifiIssueReason = reason;
  syncPendingWifiIssue();
}

void syncPendingWifiIssue() {
  if (!firebaseReady || !wifiIssuePending) return;

  time_t now = getNowTimestamp();
  if (now <= 100000) return;

  FirebaseJson json;
  json.set("lastWifiIssueAt", (double)now * 1000.0);
  json.set("lastWifiIssueReason", pendingWifiIssueReason);
  if (Firebase.updateNode(fbData, "/feeder", json)) {
    wifiIssuePending = false;
  }
}

// NOTE: Named getNowTimestamp() to avoid collision with the
// ESP32 SDK's getLocalTime(struct tm*) which takes a pointer.
time_t getNowTimestamp() {
  struct tm ti;
  if (!getLocalTime(&ti)) return 0;
  return mktime(&ti);
}

void blinkLED(int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    rgbLedWrite(PIN_LED, 2, 8, 6);
    delay(onMs);
    rgbLedWrite(PIN_LED, 0, 0, 0);
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

