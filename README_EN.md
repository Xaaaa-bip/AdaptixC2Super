# AdaptixC2 v2.0

[中文](README.md)

## Project Overview

AdaptixC2 is a red-team C2 framework — a heavily customized fork of [Adaptix-Framework/AdaptixC2](https://github.com/Adaptix-Framework/AdaptixC2). It is built around a Go team server with a plugin architecture; a **single port** serves the web console, the REST API (`/api`), and the WebSocket collaboration channel from the same origin.

| Component | Description |
|-----------|-------------|
| `AdaptixServer/` | Go team server (operator authentication, task/data persistence, multi-client WebSocket collaboration); plugins load dynamically via `-buildmode=plugin` |
| `AdaptixServer/extenders/*` | Listener and agent plugins: HTTP / HTTPS-emulation / SMB / TCP / WS / DNS / Discord listeners, Kharon, libp2p relay transport, Linux eBPF agent, macOS agent, VNC/HVNC, and more |
| `AdaptixWeb/` | Web console frontend (React + Vite; built assets are served by the team server) |
| `AdaptixApp/` | Android operator console (**new in v2.0**, same API as the web console) |
| `Extension-Kit/` | BOF / script extension pack (`extension-kit.axs`) |

## Quick Start: Deployment & Usage

### 1. Deploy the server (Linux)

The easiest path is the CI artifact — **`adaptixc2-linux-amd64.tar.gz`** on the GitHub Release (a complete, deploy-on-extract package):

```bash
tar xzf adaptixc2-linux-amd64.tar.gz -C /opt/adaptix
cd /opt/adaptix
vim profile.yaml        # change Teamserver.password at minimum; listeners live in extenders/*/config.yaml
./adaptixserver -profile profile.yaml
```

- The package ships everything: the server binary, all plugins (flattened under `extenders/<p>/`), the web console (`web/`), Extension-Kit, and `404page.html`.
- **Start it from the package root** — every path in the profile (extenders / Web / axscripts) is relative to the process cwd.
- TLS: with `tls_autogen: true` a self-signed certificate (`server.rsa.crt` / `server.rsa.key`) is generated on first start; to use a real domain certificate, point `cert` / `key` at your own files.
- Runtime state (operator database, Agents/Tasks/Consoles, certificates, logs) lives in `data/` and the run directory — never overwrite it when upgrading.
- Building from source: `make server-ext` (builds server + plugins, copies web / kit into `dist/`; start from inside `dist/`).

### 2. Use the web console

- Open `https://<ip>:8080<Web.path>/` in a browser — the entry path comes from `Web.path` in `profile.yaml` (`/bf21a14d8b0eb60f8b95ae0df61162b8/` in CI packages); click through the self-signed-certificate warning.
- Login: by default `only_password: true` — **any username + `Teamserver.password`**. Set it to `false` to log in per-account via the `operators:` table.
- ★ Changing the console entry path requires **simultaneously** rebuilding the frontend with the matching `VITE_BASE` — a mismatch means a blank page (all assets 404).
- The API is same-origin on the same port: REST under `Teamserver.endpoint` (default `/api`), collaboration over WebSocket.

### 3. Use the Android console (AdaptixApp)

Download **`AdaptixApp-debug.apk`** from the Release and install it (`AdaptixApp-release-unsigned.apk` must be signed first — see the app README). Connection details are **identical** to the web console:

- Enter `https://<ip>:8080` (**without** the entry path) and the same credentials; self-signed certificates just work.
- Features: Agents / Listeners / Targets / team chat (interoperates with the web console) / console (send commands, review task output), realtime WebSocket push with automatic reconnection.
- Full documentation: [`AdaptixApp/README_EN.md`](AdaptixApp/README_EN.md) (bilingual).

### 4. Web console: local development / build / run

The frontend is React 18 + Vite (Node 20+). Only sources are committed — everything is built on demand:

```bash
cd AdaptixWeb
npm ci

# Dev mode (hot reload) at http://localhost:5173;
# /api and WebSocket are proxied to the team server (default https://127.0.0.1:8080)
VITE_API_TARGET=https://<your-server>:8080 npm run dev

# Production build (VITE_BASE must equal Web.path in profile.yaml — see below)
VITE_BASE=/bf21a14d8b0eb60f8b95ae0df61162b8/ npm run build   # → dist/
```

- Wiring the build into the server: run `make web` from the package root (copies `AdaptixWeb/dist/` to the run directory's `web/`; the Makefile never runs npm — build first). CI packages build the frontend in the pipeline (`npm ci && npm run build`) automatically.
- `npm run preview` serves the built assets locally (also port 5173).

### 5. Changing the web console URL

**Entry path** (both places must match character-for-character, otherwise the console is a blank page — all assets 404):

1. Edit `profile.yaml`: set `Web.path` to the new path (e.g. `/newpath/`, keep leading and trailing slashes);
2. Rebuild the frontend with the **same value**: `VITE_BASE=/newpath/ npm run build`, then `make web` to copy it into the run directory;
3. Restart adaptixserver and browse to `https://<ip>:<port>/newpath/`.
   (If you use the CI package, also update `VITE_BASE` in `build.yml` — it is the frontend build prefix used at packaging time.)

**Port**: change `Teamserver.port` (console and API share the same origin and port — one change covers both), open the new port in the firewall/security group, restart.

**Domain / certificate**: set `tls_autogen_common_name` / `tls_autogen_dns_names` to your domain and delete the old certificate to have it re-issued; or point `cert` / `key` directly at your own certificate files. Self-signed certificates work fine in both browsers (click-through) and AdaptixApp.

## Branches

| Branch | Description |
|--------|-------------|
| `main` | Latest code (**v2.0**, AdaptixApp merged in) |
| `dev` | v2.0 pre-release line (merged into `main`, kept as archive) |
| `1.3` | Original v1.3 backup |

## What's new in v2.0

This release adds **AdaptixApp** — the Android team-server client. The server (AdaptixServer) and web console (AdaptixWeb) sources are **unchanged** from v1.6.

### 1. AdaptixApp (Android console, new component)

- **Connects exactly like the web console**: same server address (`https://<ip>:<port>`, no entry path), same operator credentials; self-signed certificates work out of the box.
- Connection flow follows the server spec: `POST /api/login` (Bearer) → `POST /api/otp/generate` → `WS /api/connect?otp=…` (`client_type=2`, subscribing to targets / agents / tasks / chat events).
- **Dashboard with five tabs**: Agents (online status), Listeners, Targets, team chat (interoperates with the web console), and console (pick an agent, send commands, review task history and output).
- Multiple server profiles saved locally with one-tap refill; WebSocket auto-reconnect; new-message/task notifications (permission required on Android 13+).
- Requires Android 7.0+ (minSdk 24), Kotlin + Compose — see `AdaptixApp/README_EN.md`.

### 2. CI: new `adaptix-app` build job

- Pushes to main / tags automatically build the APKs and upload them straight to the GitHub Release (following the established "artifacts go to Release, no upload-artifact" policy).
- Two assets: `AdaptixApp-debug.apk` (install directly) and `AdaptixApp-release-unsigned.apk` (unsigned — the README includes zipalign + apksigner signing instructions).

### 3. Repository cleanup

- The leftover root-level Android project (`app/`, root gradle files) is gone; the project now lives under `AdaptixApp/`; local artifacts (`build/`, `.gradle/`, `local.properties`) are all kept out of the repository.

---

Historical changelogs (v1.4 – v1.6) are in the [Chinese README](README.md).
