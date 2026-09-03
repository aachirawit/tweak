#pragma once

#include <string>

namespace szk::backend
{
// Ed25519 signature verification, used to check that a KeyAuth reply really
// came from KeyAuth.
//
// The primitive itself comes from TweetNaCl, which is not vendored here yet -
// see thirdparty/tweetnacl/README.md. Until those two files are present,
// ed25519_available() returns false and ed25519_verify() always returns false,
// so the caller fails closed rather than quietly accepting anything.

// True when a real implementation is compiled in.
[[nodiscard]] bool ed25519_available();

// Verifies `signature_hex` (128 hex characters, 64 bytes) over `message`
// against `public_key_hex` (64 hex characters, 32 bytes).
//
// Returns false on a bad signature, on malformed hex, and whenever
// ed25519_available() is false. There is no "could not check, assume fine"
// result on purpose: a verification that can be skipped is not one.
[[nodiscard]] bool ed25519_verify(const std::string& signature_hex, const std::string& message,
                                  const std::string& public_key_hex);
} // namespace szk::backend
