import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "RemoteFeeder",
  description: "ESP32-S3 Automatic Fish Feeder Dashboard",
};

export default function RootLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  return (
    <html lang="en" className="dark">
      <body className="font-sans bg-gray-950 text-gray-100 min-h-screen">
        {children}
      </body>
    </html>
  );
}
