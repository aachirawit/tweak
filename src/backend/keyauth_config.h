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
// This client currently checks that a signature is present and that its
// timestamp is within five minutes. That stops a captured "success" from
// being replayed later. It does not stop someone who can get a root
// certificate trusted on the machine from minting a fresh reply, which is the
// usual way these checks get bypassed; closing that means verifying the
// Ed25519 signature itself, which needs an Ed25519 implementation this
// project does not vendor yet.
//
// Leave this on. Turning it off removes the replay protection too and gains
// nothing.
inline constexpr bool check_response_freshness = true;

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
