#pragma once

// ─── KeyAuth application credentials ────────────────────────────────────────
//
// The real values live in keyauth_secrets.h next to this file, which is
// gitignored. Copy keyauth_secrets.example.h to keyauth_secrets.h and fill it
// in; without it the app still builds and the licence screen says it has no
// credentials rather than making a request that cannot succeed.
//
// ── About the secret ────────────────────────────────────────────────────────
// KeyAuth requires the application secret to be compiled into the client, and
// anything compiled into a client can be pulled back out of it - a strings dump
// on the shipped exe is enough. This is a property of how KeyAuth works, not of
// this code, and every KeyAuth client has it. Treat the secret as something
// that gates casual copying, not as a secret kept from a determined attacker,
// and never reuse it as a password anywhere else. Rotate it in the dashboard if
// it is ever posted somewhere public.
//
// Keeping it out of git is still worth doing: a committed secret is public
// forever even after it is deleted, because it stays in the history.

#if __has_include("backend/keyauth_secrets.h")
#include "backend/keyauth_secrets.h"
#else
#define SZK_KEYAUTH_NAME "YOUR_APP_NAME"
#define SZK_KEYAUTH_OWNERID "YOUR_OWNER_ID"
#define SZK_KEYAUTH_SECRET "YOUR_APP_SECRET"
#define SZK_KEYAUTH_VERSION "1.0"
#endif

// License Platform endpoint (the self-hosted dashboard's public activation API).
// These are NOT secrets - the host is public and the app id is what the client
// already sends in the clear - but they live in keyauth_secrets.h too so a build
// is configured in one place. Fallbacks keep the app building unconfigured.
#ifndef SZK_PLATFORM_HOST
#define SZK_PLATFORM_HOST "your-domain.example" // e.g. "panel.szk.gg"
#endif
#ifndef SZK_PLATFORM_APP_ID
#define SZK_PLATFORM_APP_ID "YOUR_APP_ID" // App.appId from the dashboard, e.g. "SZK"
#endif
// Ed25519 PUBLIC key (64 hex chars) that verifies signed activation responses.
// Empty = the server sends unsigned replies and the client trusts them over TLS
// only. Set this (and ACTIVATION_SIGNING_PRIVATE_KEY on the server) to close the
// MITM gap. Public key - safe to compile in.
#ifndef SZK_PLATFORM_SIGNING_KEY
#define SZK_PLATFORM_SIGNING_KEY ""
#endif

namespace szk::keyauth_config
{
inline constexpr char name[] = SZK_KEYAUTH_NAME;
inline constexpr char ownerid[] = SZK_KEYAUTH_OWNERID;
inline constexpr char secret[] = SZK_KEYAUTH_SECRET;
inline constexpr char version[] = SZK_KEYAUTH_VERSION;

// KeyAuth 1.3 signs each response with Ed25519 over (timestamp + body) and
// returns it as "x-signature-ed25519" plus "x-signature-timestamp", the same
// scheme Discord uses for interactions. The signing key is KeyAuth's own
// published public key - the application secret is not involved.
//
// Two checks run on every reply:
//
//   1. The timestamp is present and within five minutes. Stops a captured
//      "success" from being replayed later.
//   2. The Ed25519 signature verifies over (timestamp + body). Stops a forged
//      reply from a proxy with a trusted root certificate, which is the usual
//      way a licence check gets bypassed.
//
// The second one only runs when TweetNaCl is vendored - see
// thirdparty/tweetnacl/README.md. Without it the build prints a warning and
// only check 1 applies.
//
// Leave this on. Turning it off gives up the replay protection too.
inline constexpr bool check_response_freshness = true;

// KeyAuth's Ed25519 public key, published in their own client examples. It is
// KeyAuth's key, not yours, and it is the same for every application.
//
// Confirmed against the live service: with verification enabled, a valid key
// signs in. Since verification fails closed, a wrong constant here would have
// rejected that sign-in, so a successful login is the proof.
inline constexpr char signing_public_key[] =
    "5586b4bc69c7a4b487e4563a4cd96afd39140f919bd31cea7d1c6a1e8439422b";

// Endpoint. Only change this if KeyAuth publishes a new API version.
inline constexpr wchar_t api_host[] = L"keyauth.win";
inline constexpr wchar_t api_path[] = L"/api/1.3/";

[[nodiscard]] constexpr bool is_configured()
{
    // A placeholder still in place means keyauth_secrets.h is missing or has
    // not been filled in. Checking the first character is enough to catch that.
    return name[0] != 'Y' || ownerid[0] != 'Y' || secret[0] != 'Y';
}
} // namespace szk::keyauth_config

// ─── License backend selection ──────────────────────────────────────────────
//
// The client can authenticate a key against one of two backends behind the same
// szk::backend::auth_* interface:
//
//   keyauth  - KeyAuth's hosted service (form API, Ed25519-signed responses).
//   platform - the self-hosted License Platform dashboard (POST /api/activate,
//              JSON envelope). This is the default: the platform is the app's
//              own licensing server and needs no third-party account.
//
// Switch here at compile time; nothing else in the UI changes.
namespace szk::license_backend
{
enum class kind
{
    keyauth,
    platform,
};

inline constexpr kind active = kind::platform;
} // namespace szk::license_backend

namespace szk::platform_config
{
// Host and path of the dashboard's public activation endpoint. Host is stored
// narrow and converted to wide at call time so all config sits in one header.
inline constexpr char api_host[] = SZK_PLATFORM_HOST;
inline constexpr char api_path[] = "/api/activate";

// The app's PUBLIC identifier (App.appId in the dashboard). Sent in the clear;
// not a secret. The server maps it to the internal app and scopes the key.
inline constexpr char app_id[] = SZK_PLATFORM_APP_ID;

// Ed25519 public key (hex) for verifying signed activation responses. Empty
// when the deployment does not sign (TLS-only trust).
inline constexpr char signing_public_key[] = SZK_PLATFORM_SIGNING_KEY;

[[nodiscard]] constexpr bool is_configured()
{
    // Both placeholders must be replaced. Checking the first character catches a
    // missing or unfilled keyauth_secrets.h.
    return api_host[0] != 'y' && app_id[0] != 'Y';
}

// True when the server signs its replies and the client must verify them. When
// false the reply is trusted over TLS only.
[[nodiscard]] constexpr bool signing_enabled()
{
    return signing_public_key[0] != '\0';
}
} // namespace szk::platform_config
