"use client";

import type { FeederState } from "@/types/feeder";
import type { TemperatureSample } from "@/types/feeder";
import TemperatureChart from "@/components/TemperatureChart";

type Props = {
  state: FeederState;
  currentTime: number;
  temperatureHistory: TemperatureSample[];
};

function formatTimestamp(timestamp: number | null) {
  if (!timestamp || !Number.isFinite(timestamp)) return "No record yet";

  return new Date(timestamp).toLocaleString("en-IN", {
    timeZone: "Asia/Kolkata",
    dateStyle: "medium",
    timeStyle: "medium",
  });
}

function Detail({ label, value, note }: { label: string; value: string; note?: string }) {
  return (
    <div className="rounded-xl bg-gray-950/60 border border-gray-800 p-3">
      <p className="text-xs uppercase tracking-wider text-gray-500">{label}</p>
      <p className="mt-1 text-sm font-medium text-gray-200">{value}</p>
      {note && <p className="mt-1 text-xs text-gray-500">{note}</p>}
    </div>
  );
}

function isSameIstDay(first: number, second: number) {
  const formatter = new Intl.DateTimeFormat("en-CA", {
    timeZone: "Asia/Kolkata",
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
  });
  return formatter.format(new Date(first)) === formatter.format(new Date(second));
}

function getTodayFeedPlan(state: FeederState, currentTime: number) {
  if (!currentTime) {
    return { value: "Checking feed plan…", note: "Loading the current IST day." };
  }

  if (!state.lastFedAt || !isSameIstDay(state.lastFedAt, currentTime)) {
    return {
      value: "No feed recorded today",
      note: "Auto-feed is scheduled for 09:00 IST.",
    };
  }

  if (state.lastFedSource === "manual") {
    return {
      value: "Manual feed recorded today",
      note: "No additional auto-feed will run for the rest of this IST day.",
    };
  }

  return {
    value: "Auto-feed completed today",
    note: "No additional auto-feed will run for the rest of this IST day.",
  };
}

type WifiSchedule = {
  sleeping: boolean;
  value: string;
  note: string;
};

function getIstMinutes(currentTime: number) {
  const parts = new Intl.DateTimeFormat("en-US", {
    timeZone: "Asia/Kolkata",
    hour: "2-digit",
    minute: "2-digit",
    hourCycle: "h23",
  }).formatToParts(new Date(currentTime));

  const hour = Number(parts.find((part) => part.type === "hour")?.value ?? 0);
  const minute = Number(parts.find((part) => part.type === "minute")?.value ?? 0);
  return hour * 60 + minute;
}

function getWifiSchedule(currentTime: number): WifiSchedule {
  if (!currentTime) {
    return { sleeping: false, value: "Checking schedule…", note: "Loading IST time." };
  }

  const minutes = getIstMinutes(currentTime);
  if (minutes < 8 * 60) {
    return { sleeping: true, value: "Wi-Fi sleep active", note: "Expected to wake at 08:00 IST." };
  }
  if (minutes < 12 * 60) {
    return { sleeping: false, value: "Wi-Fi should be active", note: "Next scheduled sleep: 12:00 IST." };
  }
  if (minutes < 16 * 60 + 30) {
    return { sleeping: true, value: "Wi-Fi sleep active", note: "Expected to wake at 16:30 IST." };
  }
  if (minutes < 20 * 60) {
    return { sleeping: false, value: "Wi-Fi should be active", note: "Next scheduled sleep: 20:00 IST." };
  }
  return { sleeping: true, value: "Wi-Fi sleep active", note: "Expected to wake at 08:00 IST tomorrow." };
}

function getRemoteControlStatus(state: FeederState, schedule: WifiSchedule) {
  if (state.status === "feeding") {
    return { value: "Feed in progress", note: "Wait for the ESP32 to return to idle." };
  }
  if (state.espOnline) {
    return { value: "Ready for remote feed", note: "ESP32 is online and accepting commands." };
  }
  if (schedule.sleeping) {
    return { value: "Remote feed unavailable", note: schedule.note };
  }
  return {
    value: "ESP32 needs attention",
    note: state.lastSeen
      ? `Last contact: ${formatTimestamp(state.lastSeen)}.`
      : "No heartbeat has been received yet.",
  };
}

export default function AdvancedDetails({ state, currentTime, temperatureHistory }: Props) {
  const todayFeedPlan = getTodayFeedPlan(state, currentTime);
  const wifiSchedule = getWifiSchedule(currentTime);
  const remoteControlStatus = getRemoteControlStatus(state, wifiSchedule);

  return (
    <section className="w-full max-w-xl rounded-2xl border border-gray-800 bg-gray-900 p-4 shadow-md">
      <h2 className="text-sm font-semibold text-gray-200">Advanced details</h2>
      <p className="mt-1 text-xs text-gray-500">
        Device diagnostics reported by the ESP32.
      </p>
      <div className="mt-3 grid grid-cols-1 gap-3 sm:grid-cols-2">
        <Detail
          label="Remote control"
          value={remoteControlStatus.value}
          note={remoteControlStatus.note}
        />
        <Detail
          label="Wi-Fi schedule"
          value={wifiSchedule.value}
          note={wifiSchedule.note}
        />
        <Detail
          label="Today’s feed plan"
          value={todayFeedPlan.value}
          note={todayFeedPlan.note}
        />
        <Detail
          label="ESP32 chip temperature"
          value={
            state.chipTemperatureC === null
              ? "Unavailable"
              : `${state.chipTemperatureC.toFixed(1)} °C`
          }
          note={
            state.chipTemperatureAt
              ? `Reported ${formatTimestamp(state.chipTemperatureAt)} · Internal chip sensor, not room temperature.`
              : "Internal chip sensor, not room temperature."
          }
        />
        <Detail
          label="Last detected power event"
          value={formatTimestamp(state.lastPowerCutAt)}
          note={state.lastPowerCutReason ?? "No power-on or brownout reset recorded."}
        />
        <Detail
          label="Last restart"
          value={formatTimestamp(state.lastBootAt)}
          note={state.lastBootReason ?? "No restart data reported yet."}
        />
        <Detail
          label="Last Wi-Fi issue"
          value={formatTimestamp(state.lastWifiIssueAt)}
          note={state.lastWifiIssueReason ?? "No Wi-Fi issue recorded yet."}
        />
        <Detail
          label="Last heartbeat"
          value={formatTimestamp(state.lastSeen)}
          note={state.wifiRSSI === null ? "Signal data unavailable." : `${state.wifiRSSI} dBm`}
        />
      </div>
      <div className="mt-3">
        <TemperatureChart samples={temperatureHistory} online={state.espOnline} />
      </div>
    </section>
  );
}
