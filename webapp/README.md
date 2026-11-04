This is a [Next.js](https://nextjs.org) project bootstrapped with [`create-next-app`](https://nextjs.org/docs/app/api-reference/cli/create-next-app).

## Security and deployment

Before connecting this project to GitHub or Vercel:

1. Copy `.env.example` to `.env.local` and add the Firebase web configuration locally. `.env.local` is ignored by Git.
2. In Vercel, add every variable listed in `.env.example` under **Project Settings → Environment Variables** for the environments you deploy. Do not upload a `.env` file.
3. Never add Wi-Fi passwords, Firebase database secrets, or Firebase service-account JSON to a `NEXT_PUBLIC_` variable. Those values are intentionally visible to browser code.
4. The Firebase web configuration uses `NEXT_PUBLIC_` because the browser must receive it. It is not an access secret. Firebase Realtime Database rules and authentication are what protect the feeder data.

> Important: the current Realtime Database rules in `../firebase-rules.json` allow public reads and writes. Do not publish the dashboard with those rules. Choose an authentication model and deploy rules that require authenticated users before going live.

The ESP32 now reads its local credentials from `firmware/RemoteFeeder/secrets.h`. Copy `secrets.example.h` to `secrets.h` when setting up another device; the real file is ignored by Git.

## Getting Started

First, run the development server:

```bash
npm run dev
# or
yarn dev
# or
pnpm dev
# or
bun dev
```

Open [http://localhost:3000](http://localhost:3000) with your browser to see the result.

You can start editing the page by modifying `app/page.tsx`. The page auto-updates as you edit the file.

This project uses [`next/font`](https://nextjs.org/docs/app/building-your-application/optimizing/fonts) to automatically optimize and load [Geist](https://vercel.com/font), a new font family for Vercel.

## Learn More

To learn more about Next.js, take a look at the following resources:

- [Next.js Documentation](https://nextjs.org/docs) - learn about Next.js features and API.
- [Learn Next.js](https://nextjs.org/learn) - an interactive Next.js tutorial.

You can check out [the Next.js GitHub repository](https://github.com/vercel/next.js) - your feedback and contributions are welcome!

## Deploy on Vercel

The easiest way to deploy your Next.js app is to use the [Vercel Platform](https://vercel.com/new?utm_medium=default-template&filter=next.js&utm_source=create-next-app&utm_campaign=create-next-app-readme) from the creators of Next.js.

Check out our [Next.js deployment documentation](https://nextjs.org/docs/app/building-your-application/deploying) for more details.
