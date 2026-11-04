"use client";

import type { TemperatureSample } from "@/types/feeder";

type Props = {
  samples: TemperatureSample[];
  online: boolean;
};

const WIDTH = 320;
const HEIGHT = 132;
const PADDING = { top: 18, right: 12, bottom: 28, left: 38 };

function formatTime(timestamp: number) {
  return new Intl.DateTimeFormat("en-IN", {
    timeZone: "Asia/Kolkata",
    hour: "2-digit",
    minute: "2-digit",
    hourCycle: "h23",
  }).format(new Date(timestamp));
}

export default function TemperatureChart({ samples, online }: Props) {
  const plotWidth = WIDTH - PADDING.left - PADDING.right;
  const plotHeight = HEIGHT - PADDING.top - PADDING.bottom;
  const temperatures = samples.map((sample) => sample.celsius);
  const low = Math.floor((Math.min(...temperatures, 70) - 2) / 5) * 5;
  const high = Math.ceil((Math.max(...temperatures, 80) + 2) / 5) * 5;
  const range = Math.max(high - low, 1);
  const pointAt = (sample: TemperatureSample, index: number) => {
    const x =
      PADDING.left +
      (samples.length === 1 ? plotWidth / 2 : (index / (samples.length - 1)) * plotWidth);
    const y = PADDING.top + ((high - sample.celsius) / range) * plotHeight;
    return { x, y };
  };
  const points = samples.map(pointAt);
  const line = points.map((point) => `${point.x},${point.y}`).join(" ");
  const latest = samples.at(-1);

  return (
    <section className="w-full rounded-xl border border-gray-800 bg-gray-950/60 p-3">
      <div className="flex items-baseline justify-between gap-3">
        <div>
          <h3 className="text-xs font-medium uppercase tracking-wider text-gray-400">
            Live chip temperature
          </h3>
          <p className="mt-1 text-xs text-gray-500">
            {online
              ? "Updating every 10 seconds while the ESP32 is online."
              : "Paused while the ESP32 is offline; previous samples remain visible."}
          </p>
        </div>
        {latest && <span className="text-sm font-semibold text-cyan-300">{latest.celsius.toFixed(1)} °C</span>}
      </div>

      {samples.length < 2 ? (
        <div className="flex h-28 items-center justify-center text-sm text-gray-500">
          Collecting temperature samples…
        </div>
      ) : (
        <svg
          className="mt-3 h-auto w-full"
          viewBox={`0 0 ${WIDTH} ${HEIGHT}`}
          role="img"
          aria-label="ESP32 chip temperature over time"
        >
          {[0, 0.5, 1].map((ratio) => {
            const y = PADDING.top + ratio * plotHeight;
            const label = (high - ratio * range).toFixed(0);
            return (
              <g key={ratio}>
                <line x1={PADDING.left} x2={WIDTH - PADDING.right} y1={y} y2={y} stroke="#374151" strokeDasharray="3 3" />
                <text x={PADDING.left - 6} y={y + 3} fill="#9ca3af" fontSize="9" textAnchor="end">
                  {label}°
                </text>
              </g>
            );
          })}
          <polyline fill="none" stroke="#22d3ee" strokeWidth="2.5" points={line} strokeLinejoin="round" strokeLinecap="round" />
          {points.map((point, index) => (
            <circle key={samples[index].timestamp} cx={point.x} cy={point.y} r="2.5" fill="#67e8f9" />
          ))}
          <text x={PADDING.left} y={HEIGHT - 8} fill="#9ca3af" fontSize="9">
            {formatTime(samples[0].timestamp)}
          </text>
          <text x={WIDTH - PADDING.right} y={HEIGHT - 8} fill="#9ca3af" fontSize="9" textAnchor="end">
            {formatTime(latest!.timestamp)}
          </text>
        </svg>
      )}
    </section>
  );
}
