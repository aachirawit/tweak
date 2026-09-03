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
