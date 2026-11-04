"use client";

type HighlightColor = "green" | "red" | "cyan" | "yellow" | undefined;

type Props = {
  icon: string;
  label: string;
  value: string;
  sub?: string;
  highlight?: HighlightColor;
};

const highlightMap: Record<string, string> = {
  green:  "text-green-400",
  red:    "text-red-400",
  cyan:   "text-cyan-400",
  yellow: "text-yellow-400",
};

export default function InfoCard({ icon, label, value, sub, highlight }: Props) {
  const valueColor = highlight ? highlightMap[highlight] : "text-white";

  return (
    <div className="flex flex-col gap-1 bg-gray-900 border border-gray-800 rounded-2xl p-4 shadow-md">
      <span className="text-xs text-gray-500 uppercase tracking-wider font-medium">
        {icon} {label}
      </span>
      <span className={`text-lg font-semibold ${valueColor}`}>{value}</span>
      {sub && <span className="text-xs text-gray-500">{sub}</span>}
    </div>
  );
}

