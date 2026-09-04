#pragma once

#include <string>

namespace szk::backend
{
// User-initiated auto-updater. Like the licence check, every network step runs
// on a background thread and the UI polls status(); a synchronous download
// would freeze the window for the length of the transfer.
//
// The security that matters here is the hash check, not the transport. TLS
// already stops a passive MITM, but the manifest and the binary can come from
// different places, and a compromised or mistaken CDN could serve a bad file
// over perfectly valid TLS. So the manifest carries a SHA-256 the client
// computes over the downloaded exe and refuses to install on any mismatch.
//
// The stronger version - and the one to add before this gates anything a user
// pays for - is to sign the manifest (Ed25519, the verifier is already
// vendored for KeyAuth) so the hash itself cannot be swapped. Noted in the
// .cpp where it belongs. As written this trusts whoever can write the manifest
// endpoint.

enum class update_status
{
    idle = 0,
    checking,     // fetching the manifest
    up_to_date,   // manifest version <= current
    update_ready, // a newer version exists; details filled in
    downloading,  // fetching + hashing the new exe
    verified,     // hash matched; ready to install
    installing,   // swap script written and launched
    failed,       // any error; message says what
};

struct update_state
{
    update_status status = update_status::idle;
    std::string message;     // ready to show the user
    std::string new_version; // e.g. "1.1.0"
    std::string notes;       // changelog line from the manifest
    int percent = 0;         // download progress, 0-100
};

// Fetches the manifest and compares versions. Non-blocking. Safe from the UI
// thread. No-op (returns false) if a check or download is already running.
bool update_check_begin();

// Starts downloading + verifying the version a prior check found. Only valid
// after status() reports update_ready.
bool update_download_begin();

// Writes the swap script, launches it, and asks the app to quit so the running
// exe can be replaced. Only valid after status() reports verified. Returns
// true once the installer has been launched - the caller should then exit.
bool update_install_and_restart();

update_state update_status_now();

// Clears a finished result back to idle. Ignored mid-operation.
void update_reset();

// Blocks until any in-flight work finishes. Call once at shutdown.
void update_shutdown();
} // namespace szk::backend
