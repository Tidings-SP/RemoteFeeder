export interface FeederState {
  trigger: boolean;
  lastFedAt: number | null;      // Unix ms
  lastFedSource: "manual" | "auto" | null;
  status: "idle" | "feeding" | "offline";
  espOnline: boolean;
  wifiRSSI: number | null;
  lastSeen: number | null;       // Unix ms heartbeat from ESP
}

