#pragma once

// ─── KeyAuth application credentials ────────────────────────────────────────
//
// Fill these in from your KeyAuth dashboard (Account -> Manage Applications).
// The app will refuse to reach the network while they are still placeholders,
// so a forgotten paste shows a clear message instead of a confusing HTTP error.
//
//   name     Application Name, exactly as it appears in the dashboard
//   ownerid  Account -> Profile -> OwnerID  (24 characters)
//   secret   Application Secret. See the warning below.
//   version  Application Version. Must match the dashboard, or init fails.
//
// ── About the secret ────────────────────────────────────────────────────────
// KeyAuth requires the application secret to be compiled into the client, and
// anything compiled into a client can be pulled back out of it - a strings dump
// on the shipped exe is enough. This is a property of how KeyAuth works, not
// of this code, and every KeyAuth client has it. Treat the secret as something
// that gates casual copying, not as a secret kept from a determined attacker,
// and do not reuse it as a password anywhere else. Rotate it in the dashboard
// if it is ever posted publicly.
//
// Keep real credentials out of a public repository. If this repo is or becomes
// public, move the four values into a header you gitignore and include here, or
// pass them in as compiler defines from your release script.

namespace szk::keyauth_config
{
inline constexpr char name[] = "YOUR_APP_NAME";
inline constexpr char ownerid[] = "YOUR_OWNER_ID";
inline constexpr char secret[] = "YOUR_APP_SECRET";
inline constexpr char version[] = "1.0";

// KeyAuth signs every response with HMAC-SHA256 over the body, keyed by the
// application secret, and returns it in a "signature" header. Verifying it is
// what stops a proxy or a hosts-file entry from answering "success" on
// KeyAuth's behalf, so leave this on.
//
// Turn it off only to diagnose a signature mismatch - some older KeyAuth
// application versions name the header differently, and the login will fail
// closed until it matches. Never ship with it off.
inline constexpr bool verify_response_signature = true;

// Endpoint. Only change this if KeyAuth publishes a new API version.
inline constexpr wchar_t api_host[] = L"keyauth.win";
inline constexpr wchar_t api_path[] = L"/api/1.3/";

[[nodiscard]] constexpr bool is_configured()
{
    // A placeholder still in place means nobody has filled the dashboard values
    // in yet. Checking the first characters is enough to catch that.
    return name[0] != 'Y' || ownerid[0] != 'Y' || secret[0] != 'Y';
}
} // namespace szk::keyauth_config
