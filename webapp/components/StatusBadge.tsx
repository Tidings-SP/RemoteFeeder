"use client";

type Props = {
  status: string;
  espOnline: boolean;
  loading: boolean;
};

const statusConfig: Record<string, { label: string; color: string; dot: string }> = {
  idle:    { label: "Idle",    color: "bg-green-900/50 text-green-400 border-green-700",  dot: "bg-green-400" },
  feeding: { label: "Feeding", color: "bg-cyan-900/50 text-cyan-300 border-cyan-600",     dot: "bg-cyan-300 animate-pulse" },
  offline: { label: "Offline", color: "bg-gray-800/60 text-gray-400 border-gray-700",    dot: "bg-gray-500" },
};

export default function StatusBadge({ status, espOnline, loading }: Props) {
  if (loading) {
    return (
      <span className="flex items-center gap-2 px-3 py-1.5 rounded-full border border-gray-700 bg-gray-800/60 text-gray-500 text-sm animate-pulse">
        <span className="w-2 h-2 rounded-full bg-gray-500"></span>
        Connecting…
      </span>
    );
  }

  const cfg = statusConfig[status] ?? statusConfig.offline;

  return (
    <span className={`flex items-center gap-2 px-3 py-1.5 rounded-full border text-sm font-medium ${cfg.color}`}>
      <span className={`w-2 h-2 rounded-full ${cfg.dot}`}></span>
      {cfg.label}
    </span>
  );
}

