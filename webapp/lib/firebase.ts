import { initializeApp, getApps, getApp } from "firebase/app";
import { getDatabase } from "firebase/database";

function requiredPublicEnv(value: string | undefined, name: string) {
  if (!value) {
    throw new Error(`Missing required Firebase configuration: ${name}`);
  }
  return value;
}

const firebaseConfig = {
  apiKey: requiredPublicEnv(
    process.env.NEXT_PUBLIC_FIREBASE_API_KEY,
    "NEXT_PUBLIC_FIREBASE_API_KEY",
  ),
  authDomain: requiredPublicEnv(
    process.env.NEXT_PUBLIC_FIREBASE_AUTH_DOMAIN,
    "NEXT_PUBLIC_FIREBASE_AUTH_DOMAIN",
  ),
  databaseURL: requiredPublicEnv(
    process.env.NEXT_PUBLIC_FIREBASE_DATABASE_URL,
    "NEXT_PUBLIC_FIREBASE_DATABASE_URL",
  ),
  projectId: requiredPublicEnv(
    process.env.NEXT_PUBLIC_FIREBASE_PROJECT_ID,
    "NEXT_PUBLIC_FIREBASE_PROJECT_ID",
  ),
  storageBucket: requiredPublicEnv(
    process.env.NEXT_PUBLIC_FIREBASE_STORAGE_BUCKET,
    "NEXT_PUBLIC_FIREBASE_STORAGE_BUCKET",
  ),
  messagingSenderId: requiredPublicEnv(
    process.env.NEXT_PUBLIC_FIREBASE_MESSAGING_SENDER_ID,
    "NEXT_PUBLIC_FIREBASE_MESSAGING_SENDER_ID",
  ),
  appId: requiredPublicEnv(
    process.env.NEXT_PUBLIC_FIREBASE_APP_ID,
    "NEXT_PUBLIC_FIREBASE_APP_ID",
  ),
  measurementId: process.env.NEXT_PUBLIC_FIREBASE_MEASUREMENT_ID,
};

// Avoid re-initialising on hot-reload in dev
const app = getApps().length ? getApp() : initializeApp(firebaseConfig);
export const db = getDatabase(app);

