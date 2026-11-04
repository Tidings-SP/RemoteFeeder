"use client";

type Props = {
  rssi: number | null;
  online: boolean;
};

function getRssiLevel(rssi: number | null): { bars: number; label: string; color: string } {
  if (rssi === null || rssi === 0) return { bars: 0, label: "N/A", color: "text-gray-600" };
  if (rssi >= -50)  return { bars: 4, label: "Excellent", color: "text-green-400" };
  if (rssi >= -65)  return { bars: 3, label: "Good",      color: "text-cyan-400" };
  if (rssi >= -75)  return { bars: 2, label: "Fair",      color: "text-yellow-400" };
  return             { bars: 1, label: "Weak",      color: "text-red-400" };
}

export default function WifiStrength({ rssi, online }: Props) {
  const { bars, label, color } = getRssiLevel(online ? rssi : null);

  return (
    <span className="flex items-center gap-2 px-3 py-1.5 rounded-full border border-gray-700 bg-gray-800/60 text-sm">
      <span className="flex items-end gap-0.5 h-4">
        {[1, 2, 3, 4].map((b) => (
          <span
            key={b}
            className={`w-1 rounded-sm transition-all ${
              b <= bars ? color.replace("text-", "bg-") : "bg-gray-700"
            }`}
            style={{ height: `${b * 25}%` }}
          />
        ))}
      </span>
      <span className={`${color} font-medium`}>
        {online ? `${rssi ?? "??"} dBm` : "Sleep"}
      </span>
    </span>
  );
}

