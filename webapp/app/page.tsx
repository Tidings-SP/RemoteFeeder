"use client";

import { useFeeder } from "@/hooks/useFeeder";
import StatusBadge from "@/components/StatusBadge";
import FeedButton from "@/components/FeedButton";
import InfoCard from "@/components/InfoCard";
import WifiStrength from "@/components/WifiStrength";
import MotorPattern from "@/components/MotorPattern";

export default function Home() {
  const { state, loading, feeding, triggerFeed } = useFeeder();

  const lastFedTime = state.lastFedAt
    ? new Date(state.lastFedAt).toLocaleString("en-IN", {
        timeZone: "Asia/Kolkata",
        dateStyle: "medium",
        timeStyle: "short",
      })
    : "Never";

  return (
    <main className="min-h-screen flex flex-col items-center justify-start py-10 px-4 gap-6">
      {/* Header */}
      <div className="flex flex-col items-center gap-1">
        <span className="text-4xl">🐟</span>
        <h1 className="text-3xl font-bold tracking-tight text-cyan-400">
          RemoteFeeder
        </h1>
        <p className="text-gray-400 text-sm">ESP32-S3 Fish Feeder Dashboard</p>
      </div>

      {/* Status Row */}
      <div className="flex gap-3 flex-wrap justify-center">
        <StatusBadge status={state.status} espOnline={state.espOnline} loading={loading} />
        <WifiStrength rssi={state.wifiRSSI} online={state.espOnline} />
      </div>

      {/* Feed Button */}
      <FeedButton
        onFeed={triggerFeed}
        feeding={feeding}
        status={state.status}
        espOnline={state.espOnline}
        loading={loading}
      />

      {/* Info Cards */}
      <div className="grid grid-cols-1 sm:grid-cols-2 gap-4 w-full max-w-xl">
        <InfoCard
          icon="🕐"
          label="Last Fed"
          value={lastFedTime}
          sub={state.lastFedSource ? `via ${state.lastFedSource}` : undefined}
        />
        <InfoCard
          icon="⏰"
          label="Auto-feed Schedule"
          value="09:00 IST"
          sub="Daily — skipped if manually fed"
        />
        <InfoCard
          icon="📡"
          label="ESP32-S3 Status"
          value={state.espOnline ? "Online" : "Offline"}
          sub={state.espOnline ? "Connected to Firebase" : "Might be in sleep window (20:00–08:00)"}
          highlight={state.espOnline ? "green" : "red"}
        />
        <InfoCard
          icon="🌙"
          label="WiFi Sleep Window"
          value="20:00 – 08:00 IST"
          sub="Radio off to reduce heat"
        />
      </div>

      {/* Motor Pattern Visualiser */}
      <MotorPattern active={state.status === "feeding"} />

      {/* Footer */}
      <p className="text-xs text-gray-600 mt-4">
        Auto-feeds daily at 09:00 IST · Offline backup active · Power-cut safe
      </p>
    </main>
  );
}
