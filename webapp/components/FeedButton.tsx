"use client";

type Props = {
  onFeed: () => void;
  feeding: boolean;
  status: string;
  espOnline: boolean;
  loading: boolean;
};

export default function FeedButton({ onFeed, feeding, status, espOnline, loading }: Props) {
  const isFeeding = feeding || status === "feeding";
  const disabled  = loading || isFeeding || !espOnline;

  return (
    <div className="flex flex-col items-center gap-2">
      <button
        onClick={onFeed}
        disabled={disabled}
        className={`
          relative w-40 h-40 rounded-full text-5xl font-bold shadow-2xl
          transition-all duration-200 select-none
          ${
            isFeeding
              ? "bg-cyan-700 scale-95 shadow-cyan-500/30 cursor-wait"
              : disabled
              ? "bg-gray-800 text-gray-600 cursor-not-allowed opacity-50"
              : "bg-cyan-600 hover:bg-cyan-500 active:scale-95 hover:shadow-cyan-500/40 cursor-pointer text-white"
          }
          flex items-center justify-center
          ring-4 ${isFeeding ? "ring-cyan-400/40" : "ring-cyan-800/30"}
        `}
        aria-label="Feed now"
      >
        {isFeeding ? (
          <span className="animate-spin text-4xl">⚙️</span>
        ) : (
          "🐠"
        )}
        {/* Pulse ring when feeding */}
        {isFeeding && (
          <span className="absolute inset-0 rounded-full border-4 border-cyan-400 animate-ping opacity-30" />
        )}
      </button>
      <p className="text-sm text-gray-400">
        {loading
          ? "Loading…"
          : isFeeding
          ? "Feeding fish… 🌊"
          : !espOnline
          ? "ESP32 offline – feeding unavailable"
          : "Tap to feed now"}
      </p>
      {!espOnline && !loading && (
        <p className="text-xs text-yellow-500 max-w-xs text-center">
          ⚠️ ESP32 is offline (sleep window or no network). Feeding will be
          available when it reconnects.
        </p>
      )}
    </div>
  );
}

