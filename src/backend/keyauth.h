#pragma once

#include <string>

namespace szk::backend
{
// KeyAuth licence check.
//
// Every call here is non-blocking. The HTTP request runs on its own thread and
// the UI polls status() each frame, because a synchronous request would freeze
// the window for as long as the network takes - which is exactly when the user
// most wants to see a spinner.

enum class auth_status
{
    idle = 0,   // nothing has been attempted yet
    working,    // a request is in flight
    authorised, // the key is valid and not expired
    failed,     // rejected, expired, misconfigured, or unreachable
};

struct auth_result
{
    auth_status status = auth_status::idle;

    // Ready to show the user as-is. On failure this says what went wrong and
    // what to do about it; it never contains the key or the app secret.
    std::string message;

    // Populated on success.
    std::string subscription;    // plan name from the dashboard
    std::string expiry_readable; // "12 Mar 2027", or empty when unknown
    long long expiry_unix = 0;   // 0 when the plan has no expiry
    int days_left = 0;
};

// Kicks off init + licence check for `key` on a background thread. Returns
// false and leaves a failure in status() if a check is already running or the
// key is empty. Safe to call from the UI thread.
bool auth_begin(const std::string& key);

// Current state. Cheap; call it every frame.
auth_result auth_status_now();

// Clears a finished result back to idle so the form can be retried. Ignored
// while a request is in flight.
void auth_reset();

// This machine's KeyAuth HWID, so the licence screen can show the user the
// value to give you when they ask for a HWID reset.
std::string auth_hwid();

// Blocks until any in-flight request finishes. Call once during shutdown so
// the worker cannot outlive the state it writes into.
void auth_shutdown();
} // namespace szk::backend
