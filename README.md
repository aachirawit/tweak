# SZK

A Windows tuning tool for FiveM and general system optimisation, built with C++20,
Dear ImGui and DirectX 11.

SZK reads the machine's current registry, power and driver state, scores it against
the tweaks it can verify, and applies the ones you choose. It takes a System Restore
point before any bulk change and refuses to proceed if Windows will not give it one.

## What it does

- **Dashboard** — an optimisation score derived only from settings SZK can read back,
  live CPU / memory / disk / ping, and a "Needs attention" list that says what leaving
  each tweak off actually costs you.
- **Performance, Graphics, Network, Power plan, Cleanup** — the tweak pages, grouped by
  what they change rather than by where they live in the registry.
- **Auto ReShade** — installs ReShade and the QuantV preset into FiveM's game folder.
- **Drivers / This machine** — board lookup and system information.

## Network use

SZK is not offline. It makes network requests in two places:

1. **Licence check.** On launch it validates your licence key against one of two
   backends (selected at build time — see below). The request sends the key and a
   hardware ID derived from this machine's Windows machine GUID.
   - **License Platform** (default): a single `POST /api/activate` to your own
     self-hosted dashboard over HTTPS.
   - **KeyAuth**: `keyauth.win`, with responses verified by Ed25519 signature.
2. **Ping.** The dashboard's latency tile sends ICMP echo requests to `1.1.1.1`.

It does not send telemetry, and it does not transmit any of the settings it reads.

## Building

Requires Visual Studio 2022 (or newer) with **Desktop development with C++**.

```powershell
.\scripts\build.ps1 -Configuration Release -Run
```

Before the first build, copy `src/backend/keyauth_secrets.example.h` to
`keyauth_secrets.h` (gitignored) and fill it in. Until you do, the login screen
says the build is unconfigured instead of attempting a request.

The licence backend is chosen at compile time by `szk::license_backend::active`
in [`src/backend/keyauth_config.h`](src/backend/keyauth_config.h):

- **License Platform** (default): set `SZK_PLATFORM_HOST` to your dashboard's
  domain and `SZK_PLATFORM_APP_ID` to the app's public id (Apps → your app in
  the dashboard). These are not secrets — the host is public and the id is sent
  in the clear. The server owns every decision and the reply is trusted over TLS.
- **KeyAuth**: set the KeyAuth `NAME`/`OWNERID`/`SECRET`/`VERSION`. Note the
  application secret is compiled into the client — it can be recovered from the
  shipped binary by anyone who looks, so never reuse it as a password. The
  platform backend has no such compiled-in secret.

Do not commit real credentials to a public repository.

## Requirements at runtime

- Windows 10 or 11, 64-bit
- Administrator rights (the app manifest requests them; the tweaks write to HKLM,
  `powercfg`, `netsh` and `bcdedit`)
- An active SZK licence key

## Layout

```
src/application    screen flow
src/auth           licence screen and legal pages
src/backend        tweaks, system monitor, KeyAuth client
src/ui             design system, controls, screens
src/platform       Win32 window and D3D11 renderer
assets/reshade     the ReShade payload installed into FiveM
```

## Licence

See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
