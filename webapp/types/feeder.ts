export interface FeederState {
  trigger: boolean;
  lastFedAt: number | null;      // Unix ms
  lastFedSource: "manual" | "auto" | null;
  status: "idle" | "feeding" | "offline";
  espOnline: boolean;
  wifiRSSI: number | null;
  lastSeen: number | null;       // Unix ms heartbeat from ESP
  chipTemperatureC: number | null; // ESP32 internal sensor, not ambient air
  chipTemperatureAt: number | null;
  lastBootAt: number | null;     // Unix ms of most recent ESP boot
  lastBootReason: string | null;
  lastPowerCutAt: number | null; // Latest power-on or brownout reset
  lastPowerCutReason: string | null;
  lastWifiIssueAt: number | null;
  lastWifiIssueReason: string | null;
}

export interface TemperatureSample {
  timestamp: number;
  celsius: number;
}

