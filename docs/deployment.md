# Deploy PIFL on Cloudflare

## Connected production build settings

The connected Worker is **`fl-on-esp2866`**, hosted at **`https://fl-on-esp2866.abhyudaymishr.workers.dev`**. The account suffix is `abhyudaymishr.workers.dev`; the Worker name is prepended to it. The approved production build settings were applied on 13 November 2026.

Cloudflare Workers Builds is configured as the production deployment provider, with root directory `/`, build command `npm run ci:cloudflare`, and deploy command `npm run release:cloudflare`. The build runs domain/API/SQL tests, native/Python regressions, a pinned PlatformIO firmware build, and console packaging. The release verifies the deployed commit and authenticated schema readiness and attempts code rollback on smoke failure. Preview deployments are disabled so branch builds do not use the production release command or credentials.

The existing Cloudflare-generated build token deploys Worker code. D1 schema administration is performed separately: `0001_initial.sql` has already been applied to database `69fb8a40-831c-411d-811c-36f8675403a2`. Build variables select `PIFL_MIGRATION_MODE=preprovisioned` and `PIFL_APPLIED_MIGRATIONS=0001_initial.sql`. A new migration file blocks deployment until an administrator applies it and updates that list. `/api/ready` verifies the applied migration names through the Worker's D1 binding. No D1 administration permission is needed on the build token in this mode.

`PIFL_GATEWAY_KEYS` and `PIFL_READ_TOKEN` are saved in Cloudflare as build secrets; deployment binds them as Worker secrets. Other build variables are `CLOUDFLARE_ACCOUNT_ID`, `PIFL_WORKER_NAME`, `PIFL_D1_DATABASE_ID`, and `PIFL_CLOUD_URL`. Credentials for this setup are also kept in the ignored local `.env` file with owner-only permissions. Do not commit or share that file.

GitHub Actions continues to run validation and publish firmware/release artifacts. Its production deployment job is opt-in through `PIFL_DEPLOY_PROVIDER=github-actions`, so the two providers do not race to deploy. To move deployment ownership to GitHub Actions, disable Cloudflare auto-build deployments before enabling that variable and configuring the secrets below.

## Run the console locally

Use Node.js 22.18+ and Python 3.10+:

```sh
npm ci --ignore-scripts
npm run dev
```

Open `http://127.0.0.1:3000`, then choose **Explore simulation**. The preview serves the real console modules but does not emulate D1 or proxy cloud tokens. For real telemetry, open the deployed Worker URL and connect with its read token. The existing local dashboard remains available with `python3 -m pifl_vis.server`.

## One-time account configuration

1. Use a Cloudflare Workers **Free** account. Open **Workers & Pages** once to initialize a `workers.dev` subdomain if the account does not have one. No custom domain is required.
2. Create an account-scoped API token with **Workers Scripts: Edit** and **D1: Edit**. Restrict it to the intended account. A pre-existing `workers.dev` subdomain is required; the deployment does not change account settings or billing plans.
3. Generate separate random gateway and read tokens, for example by running `node -e "console.log(require('node:crypto').randomBytes(32).toString('hex'))"` twice. Save the values securely. Do not commit them.
4. Add these GitHub Actions secrets to `abhyudaymishr/FL-on-ESP2866`, either as repository secrets or production-environment secrets:

| Secret | Value |
| --- | --- |
| `CLOUDFLARE_ACCOUNT_ID` | Your Cloudflare account's 32-character ID |
| `CLOUDFLARE_API_TOKEN` | The scoped deployment token |
| `PIFL_GATEWAY_KEYS` | JSON object: `{"lab-01":"YOUR_RANDOM_GATEWAY_TOKEN"}` |
| `PIFL_READ_TOKEN` | A different random read-only token |

Optional Actions variables: `PIFL_WORKER_NAME` (default `fl-on-esp2866`) and `PIFL_D1_DATABASE_ID` (an existing database UUID). Without a database ID, the deploy script looks up a database named after the Worker and creates one if absent. It does not create a new database on each release. If the account has more than 100 databases, supply the explicit ID.

The connected Cloudflare app can inspect and prepare resources. Its session credentials are not GitHub Actions secrets: they must not be copied from browser sessions or committed. The workflow needs its own scoped token.

The D1 database `pifl-control-room` was prepared on 13 November 2026 with UUID `69fb8a40-831c-411d-811c-36f8675403a2`. Migration `0001_initial.sql` is applied. The connected build supplies this UUID explicitly because the Worker and database have different names.

## CI and deployment

Push this implementation to `main` to trigger the connected Cloudflare build and GitHub validation. Pull requests run GitHub checks and create artifacts without deploying. If GitHub Actions owns deployment, the workflow's manual `deploy` option deploys only from `main` and requires `PIFL_DEPLOY_PROVIDER=github-actions`.

For protected branches, require the **Native, protocol, Python and bridge regressions**, **ESP8266 firmware build**, and **Domain, SQL, API and deployment checks** jobs. Configure any production-environment reviewer policy in GitHub to match your release process. The repository does not assume one exists.

The release script applies numbered SQL migrations before uploading code. It uploads the packaged static assets and module graph, enables the Worker's existing `workers.dev` hostname, and reports the URL. Smoke checks verify public health, release SHA, UI shell, read authentication, ingestion method restrictions, and state/history access. A failed smoke check attempts to restore the previous code deployment. An initial deployment has no rollback target.

Missing credentials or an uninitialized Workers account cause deployment to fail with a visible error. They are not silently treated as a successful release. CI must pass in GitHub before the pipeline can be considered activated.

Local packaging and checks, without cloud credentials:

```sh
npm run check
npm test
python3 -m unittest discover tests -v
npm run build
npm run deploy -- --dry-run
```

With the same secrets exported in your shell, `npm run deploy` deploys the packaged `dist/` directory. Use a separate Worker name, database, and credentials for staging; do not point a staging deployment at the production database. Review the deployed Workers CPU and D1 row metrics before increasing fleet size, viewers, or cadence.

## Connect the physical network

Run on a host connected to the boards' Wi-Fi network. Set these environment variables in your service manager or shell:

| Variable | Purpose |
| --- | --- |
| `PIFL_CLOUD_URL` | The deployed HTTPS origin, without query parameters |
| `PIFL_GATEWAY_ID` | `lab-01`, matching the Worker secret map |
| `PIFL_GATEWAY_TOKEN` | The gateway token value from `PIFL_GATEWAY_KEYS` |
| `PIFL_NETWORK_SECRET` | Optional override matching the firmware's wire-envelope key |

```sh
python3 -m pifl_vis.bridge --udp-port 4210 --interval 30
```

The bridge receives UDP, checks wire validity and model sender boot identity, and uploads a bounded current snapshot every 30 seconds. Upload failure uses exponential backoff capped at five minutes. A successful upload restores the normal cadence. It never forwards a backlog of old captures. Run the bridge as a supervised service on an always-on LAN host for continuous monitoring. Do not run another listener on the same UDP port. The current bridge supports UDP mode; the legacy dashboards retain optional MQTT support.

Open the cloud console, select **Connect**, and enter the read token. The gateway upload token cannot read the dashboard; the read token cannot ingest telemetry. An empty authorized dashboard means no capture has arrived yet. A stale gateway is different from offline boards at the latest capture. Use **Ask the guide** for explanations, not hardware commands.

## Rollback and recovery

In GitHub Actions, run **Roll back Cloudflare code** on `main` with an existing Worker version UUID from the Cloudflare dashboard. This restores code and its versioned assets; it retains D1. Keep migrations additive. If a release rotates tokens, verify the intended token state after rollback.

An initial failed deployment must be corrected and redeployed because no previous version exists. For data recovery, use D1's available backup/Time Travel facilities rather than code rollback. No script in this repository drops the telemetry tables or upgrades the billing plan.

## UI acceptance checklist

Before treating the interface as visually approved, run the unrestricted local preview and check:

- Desktop and 375px mobile widths, dark theme, keyboard navigation, modal focus/close.
- Disconnected empty state, explicitly labeled simulation, real authorized empty state.
- Search, status filter, sort, node inspector, time-range selection, pause/resume, JSON export.
- Separate sensor/model freshness, stale gateway, rejected read token, failed network request.
- Guide answers for health, leaders, loss, physics, and request budget.

The implementation is responsive, but this session's browser security policy prevented visual verification. Automated SQL/API checks do not replace this acceptance pass.
