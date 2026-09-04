#pragma once

// Copy this file to keyauth_secrets.h next to it and fill in your own values
// from the KeyAuth dashboard. keyauth_secrets.h is gitignored, so the real
// credentials never enter the repository.
//
//   name     Manage Applications -> Application Name (must match exactly)
//   ownerid  Account -> Profile -> OwnerID
//   secret   Manage Applications -> Application Secret
//   version  Manage Applications -> Version (init fails if it does not match)
//
// Without keyauth_secrets.h the app still builds; the licence screen simply
// says it has no credentials compiled in and makes no network request.

#define SZK_KEYAUTH_NAME "YOUR_APP_NAME"
#define SZK_KEYAUTH_OWNERID "YOUR_OWNER_ID"
#define SZK_KEYAUTH_SECRET "YOUR_APP_SECRET"
#define SZK_KEYAUTH_VERSION "1.0"

// ── License Platform (self-hosted dashboard) ────────────────────────────────
// Used when szk::license_backend::active == kind::platform (the default). Not
// secret - the host is public and the app id is sent in the clear - but kept
// here so one file configures the build.
//
//   host    the dashboard's domain, no scheme, e.g. "panel.szk.gg"
//   app id  App.appId from the dashboard (Apps -> your app), e.g. "SZK"
#define SZK_PLATFORM_HOST "your-domain.example"
#define SZK_PLATFORM_APP_ID "YOUR_APP_ID"
// Ed25519 public key (64 hex) that verifies signed activation responses. Leave
// empty to trust replies over TLS only. See ACTIVATION_SIGNING_PRIVATE_KEY in
// the platform's .env.example for how to generate the keypair.
#define SZK_PLATFORM_SIGNING_KEY ""
