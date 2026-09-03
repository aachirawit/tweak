#include "backend/ed25519_verify.h"

#include <vector>

// TweetNaCl is a single-file, public-domain implementation of the NaCl
// primitives by Bernstein, van Gastel, Janssen, Lange, Schwabe and Smetsers.
// Drop tweetnacl.c and tweetnacl.h into thirdparty/tweetnacl/ and add the .c
// to the project; this file picks them up automatically.
#if __has_include(<tweetnacl.h>)
#define SZK_HAVE_TWEETNACL 1
extern "C"
{
#include <tweetnacl.h>
}
#else
#define SZK_HAVE_TWEETNACL 0
#pragma message(                                                                                   \
    "SZK: tweetnacl not found - KeyAuth replies will NOT have their Ed25519 signature verified. "  \
    "See thirdparty/tweetnacl/README.md before shipping.")
#endif

namespace szk::backend
{
namespace
{
// Decodes an even-length hex string. Returns false on any non-hex character,
// so a truncated or padded header is rejected rather than silently shortened.
bool from_hex(const std::string& in, std::vector<unsigned char>& out)
{
    if (in.empty() || (in.size() % 2) != 0)
        return false;

    auto nibble = [](char c) -> int
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };

    out.clear();
    out.reserve(in.size() / 2);

    for (size_t i = 0; i < in.size(); i += 2)
    {
        const int hi = nibble(in[i]);
        const int lo = nibble(in[i + 1]);
        if (hi < 0 || lo < 0)
            return false;
        out.push_back(static_cast<unsigned char>((hi << 4) | lo));
    }
    return true;
}
} // namespace

bool ed25519_available()
{
    return SZK_HAVE_TWEETNACL != 0;
}

bool ed25519_verify(const std::string& signature_hex, const std::string& message,
                    const std::string& public_key_hex)
{
#if SZK_HAVE_TWEETNACL
    std::vector<unsigned char> signature;
    std::vector<unsigned char> public_key;

    if (!from_hex(signature_hex, signature) || signature.size() != 64)
        return false;
    if (!from_hex(public_key_hex, public_key) || public_key.size() != 32)
        return false;

    // NaCl verifies a "signed message" - signature followed by the message -
    // and hands back the message it recovered, so the two have to be joined
    // and the output buffer sized for both.
    std::vector<unsigned char> signed_message;
    signed_message.reserve(signature.size() + message.size());
    signed_message.insert(signed_message.end(), signature.begin(), signature.end());
    signed_message.insert(signed_message.end(), message.begin(), message.end());

    std::vector<unsigned char> recovered(signed_message.size());
    unsigned long long recovered_length = 0;

    const int result = ::crypto_sign_open(
        recovered.data(), &recovered_length, signed_message.data(),
        static_cast<unsigned long long>(signed_message.size()), public_key.data());

    return result == 0 && recovered_length == message.size();
#else
    (void)signature_hex;
    (void)message;
    (void)public_key_hex;
    return false;
#endif
}
} // namespace szk::backend

#if SZK_HAVE_TWEETNACL
// TweetNaCl declares randombytes() for key generation and signing. Verifying a
// signature never calls it, but the linker still wants the symbol. Aborting is
// the right body: reaching it would mean something in this build is trying to
// generate keys with no entropy source, and quietly handing back zeros would
// be far worse than stopping.
extern "C" void randombytes(unsigned char*, unsigned long long)
{
    __debugbreak();
    ::abort();
}
#endif
