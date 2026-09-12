#pragma once

#include <cstddef>
#include <cstdint>

// ─── Compile-time string encryption ─────────────────────────────────────────
//
// A string wrapped in SK("...") is XOR-encrypted at compile time with a
// per-string key derived from __COUNTER__, so the plaintext never appears in
// the binary's .rdata. It is decrypted in place on first use and re-encrypted
// when the wrapper goes out of scope, so a memory scan catches it only during
// the brief window it is actually being read.
//
// What this is for: keeping API URLs, registry paths and message strings out
// of a plain `strings numbanine.exe` dump. It raises the effort to find them from
// "grep" to "set a breakpoint on the decryptor". It does NOT hide anything
// from a debugger sitting on the decrypt call - nothing compiled into the
// client can. Treat it as obfuscation, not secrecy.
//
// Usage:
//   const char* url = SK("https://keyauth.win/api/1.3/");   // decrypts on cast
//   printf("%s", SK("hello"));
//
// The object must outlive the pointer, so bind it to a name when you keep the
// pointer around:
//   auto path = SK("SOFTWARE\\numbanine");
//   use(path.get());

namespace szk::sec
{
// A tiny linear-congruential generator seeded from the build timestamp and the
// call-site counter, so every wrapped string gets a different key and two
// identical literals do not encrypt to the same bytes.
constexpr uint32_t sk_seed(uint32_t counter)
{
    uint32_t hash = 2166136261u ^ counter;
    // Fold in __TIME__ ("HH:MM:SS") so a rebuild reshuffles every key. The
    // non-determinism C5048 warns about is exactly the point here - the keys
    // are meant to differ build to build - so the warning is silenced for this
    // use only.
#pragma warning(push)
#pragma warning(disable : 5048)
    for (char c : __TIME__)
        hash = (hash ^ static_cast<uint32_t>(c)) * 16777619u;
#pragma warning(pop)
    return hash;
}

constexpr uint8_t sk_key_byte(uint32_t seed, size_t index)
{
    uint32_t x = seed + static_cast<uint32_t>(index) * 2654435761u;
    x ^= x >> 15;
    x *= 2246822519u;
    x ^= x >> 13;
    return static_cast<uint8_t>(x);
}

template <size_t N, uint32_t Seed> class sk_crypter
{
  public:
    // Encrypt at construction. consteval would be ideal but constexpr keeps
    // this usable on older toolchains; the array initialiser below still runs
    // at compile time in an optimised build.
    constexpr sk_crypter(const char (&literal)[N])
    {
        for (size_t i = 0; i < N; i++)
            data_[i] = static_cast<char>(literal[i] ^ sk_key_byte(Seed, i));
    }

    // Decrypt in place and return the plaintext. Idempotent per state: calling
    // get() twice without an intervening clear() decrypts, then reads.
    const char* get()
    {
        if (encrypted_)
        {
            for (size_t i = 0; i < N; i++)
                data_[i] = static_cast<char>(data_[i] ^ sk_key_byte(Seed, i));
            encrypted_ = false;
        }
        return data_;
    }

    operator const char*()
    {
        return get();
    }

    // Re-encrypt so the plaintext does not sit in memory after use. The
    // destructor does this automatically for scoped temporaries.
    void clear()
    {
        if (!encrypted_)
        {
            for (size_t i = 0; i < N; i++)
                data_[i] = static_cast<char>(data_[i] ^ sk_key_byte(Seed, i));
            encrypted_ = true;
        }
    }

    ~sk_crypter()
    {
        clear();
    }

  private:
    char data_[N]{};
    bool encrypted_ = true;
};
} // namespace szk::sec

// One macro, one unique key per expansion via __COUNTER__.
#define SK(str) (::szk::sec::sk_crypter<sizeof(str), ::szk::sec::sk_seed(__COUNTER__)>(str))
