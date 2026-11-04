"use client";

/**
 * MotorPattern visualiser
 * Shows 3 cycles of ON(0.5s) / OFF(0.3s) as an animated timeline
 * when the feeder is actively running.
 */

type Props = {
  active: boolean;
};

export default function MotorPattern({ active }: Props) {
  // Two OFF gaps occur between the three 0.5s ON cycles.
  return (
    <div className="w-full max-w-xl bg-gray-900 border border-gray-800 rounded-2xl p-4 shadow-md">
      <p className="text-xs text-gray-500 uppercase tracking-wider mb-3">
        ⚙️ Motor Pattern  — 3 × (ON 0.5s / OFF 0.3s)
      </p>

      <div className="flex gap-1 items-end h-10">
        {[0, 1, 2].map((i) => (
          <div key={i} className="flex gap-1 flex-1">
            {/* ON segment – 62.5% of cycle width */}
            <div
              className={`flex-[5] rounded-sm transition-all duration-300 ${
                active
                  ? "bg-cyan-500 animate-pulse"
                  : "bg-cyan-900/40"
              } h-8`}
              title={`Cycle ${i + 1} — ON 0.5s`}
            />
            {/* OFF segment – 37.5% of cycle width, only between cycles */}
            {i < 2 && (
              <div
                className={`flex-[3] rounded-sm h-4 self-end ${
                  active ? "bg-gray-700" : "bg-gray-800"
                }`}
                title={`Cycle ${i + 1} — OFF 0.5s`}
              />
            )}
          </div>
        ))}
      </div>

      <div className="flex justify-between text-xs text-gray-600 mt-1">
        <span>Cycle 1</span>
        <span>Cycle 2</span>
        <span>Cycle 3</span>
      </div>

      {active && (
        <p className="text-xs text-cyan-400 mt-2 text-center animate-pulse">
          Motor running…
        </p>
      )}
    </div>
  );
}

