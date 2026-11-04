"use client";

import { db } from "@/lib/firebase";
import { ref, onValue, set, update } from "firebase/database";
import { useEffect, useState, useCallback, useRef } from "react";
import type { FeederState, TemperatureSample } from "@/types/feeder";

const FEEDER_PATH = "/feeder";
const HEARTBEAT_TIMEOUT_MS = 25000; // 25s timeout (ESP32 sends heartbeat every 10s)
const MAX_TEMPERATURE_SAMPLES = 60; // Ten minutes at the 10-second heartbeat cadence

export function useFeeder() {
  const [rawState, setRawState] = useState<FeederState>({
    trigger: false,
    lastFedAt: null,
    lastFedSource: null,
    status: "offline",
    espOnline: false,
    wifiRSSI: null,
    lastSeen: null,
    chipTemperatureC: null,
    chipTemperatureAt: null,
    lastBootAt: null,
    lastBootReason: null,
    lastPowerCutAt: null,
    lastPowerCutReason: null,
    lastWifiIssueAt: null,
    lastWifiIssueReason: null,
  });
  const [loading, setLoading] = useState(true);
  const [feeding, setFeeding] = useState(false);
  const [temperatureHistory, setTemperatureHistory] = useState<TemperatureSample[]>([]);
  // Keep the initial render deterministic for prerendering. The current time is
  // initialized once the component mounts, then refreshed by the heartbeat timer.
  const [now, setNow] = useState<number>(0);

  const rawStateRef = useRef(rawState);
  useEffect(() => {
    rawStateRef.current = rawState;
  }, [rawState]);

  // Track whether we've already written offline to DB to avoid
  // spamming Firebase with identical writes every 2 seconds
  const offlineSyncedRef = useRef(false);

  useEffect(() => {
    const feederRef = ref(db, FEEDER_PATH);
    const unsub = onValue(
      feederRef,
      (snap) => {
        if (snap.exists()) {
          const data = snap.val();
          setRawState({
            trigger: data.trigger ?? false,
            lastFedAt: data.lastFedAt ?? null,
            lastFedSource: data.lastFedSource ?? null,
            status: data.status ?? "offline",
            espOnline: data.espOnline ?? false,
            wifiRSSI: data.wifiRSSI ?? null,
            lastSeen: data.lastSeen ?? null,
            chipTemperatureC: data.chipTemperatureC ?? null,
            chipTemperatureAt: data.chipTemperatureAt ?? null,
            lastBootAt: data.lastBootAt ?? null,
            lastBootReason: data.lastBootReason ?? null,
            lastPowerCutAt: data.lastPowerCutAt ?? null,
            lastPowerCutReason: data.lastPowerCutReason ?? null,
            lastWifiIssueAt: data.lastWifiIssueAt ?? null,
            lastWifiIssueReason: data.lastWifiIssueReason ?? null,
          });

          const chipTemperatureC = data.chipTemperatureC;
          const chipTemperatureAt = data.chipTemperatureAt;
          if (
            typeof chipTemperatureC === "number" &&
            Number.isFinite(chipTemperatureC) &&
            typeof chipTemperatureAt === "number" &&
            Number.isFinite(chipTemperatureAt)
          ) {
            setTemperatureHistory((history) => {
              if (history.at(-1)?.timestamp === chipTemperatureAt) return history;

              return [
                ...history,
                { timestamp: chipTemperatureAt, celsius: chipTemperatureC },
              ].slice(-MAX_TEMPERATURE_SAMPLES);
            });
          }

          // Reset the offline-synced flag when ESP comes back online
          if (data.espOnline) {
            offlineSyncedRef.current = false;
          }

          // Clear local feeding state once ESP confirms idle
          if (data.status === "idle" || data.status === "offline") {
            setFeeding(false);
          }
        }
        setLoading(false);
      },
      () => {
        // Permission and listener errors otherwise leave the UI on
        // "Connecting…" forever. The default state safely renders offline.
        setFeeding(false);
        setLoading(false);
      },
    );

    return () => unsub();
  }, []);

  // Regularly check heartbeat freshness (every 2 seconds)
  useEffect(() => {
    const checkHeartbeat = () => {
      const current = Date.now();
      setNow(current);

      const latest = rawStateRef.current;
      // If DB still says espOnline: true but ESP hasn't sent heartbeat in >30s,
      // sync DB to offline — but only once
      if (
        latest.espOnline &&
        latest.lastSeen &&
        current - latest.lastSeen > 30000 &&
        !offlineSyncedRef.current
      ) {
        offlineSyncedRef.current = true;
        update(ref(db, FEEDER_PATH), {
          espOnline: false,
          status: "offline",
        }).catch(() => {});
      }
    };

    checkHeartbeat();
    const interval = setInterval(checkHeartbeat, 2000);

    return () => clearInterval(interval);
  }, []);

  // Compute live presence: online ONLY IF espOnline is true AND heartbeat is within 25s
  const isFresh = rawState.lastSeen
    ? now - rawState.lastSeen < HEARTBEAT_TIMEOUT_MS
    : false;
  const isOnline = Boolean(rawState.espOnline) && isFresh;
  const computedStatus = isOnline ? rawState.status : "offline";

  const state: FeederState = {
    ...rawState,
    espOnline: isOnline,
    status: computedStatus,
  };

  const triggerFeed = useCallback(async () => {
    if (feeding) return;
    setFeeding(true);
    try {
      await set(ref(db, `${FEEDER_PATH}/trigger`), true);
    } catch (err) {
      console.error("Failed to trigger feed:", err);
      setFeeding(false);
    }
  }, [feeding]);

  return {
    state,
    loading,
    feeding,
    currentTime: now,
    temperatureHistory,
    triggerFeed,
  };
}
