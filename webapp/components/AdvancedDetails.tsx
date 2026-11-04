"use client";

import type { FeederState } from "@/types/feeder";

type Props = {
  state: FeederState;
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

export default function AdvancedDetails({ state }: Props) {
  return (
    <section className="w-full max-w-xl rounded-2xl border border-gray-800 bg-gray-900 p-4 shadow-md">
      <h2 className="text-sm font-semibold text-gray-200">Advanced details</h2>
      <p className="mt-1 text-xs text-gray-500">
        Device diagnostics reported by the ESP32.
      </p>
      <div className="mt-3 grid grid-cols-1 gap-3 sm:grid-cols-2">
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
    </section>
  );
}
