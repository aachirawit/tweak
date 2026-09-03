#include "backend/keyauth.h"

#include "backend/keyauth_config.h"

#include <atomic>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>

#include <bcrypt.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

namespace szk::backend
{
namespace
{
// ── Small helpers ───────────────────────────────────────────────────────────

std::string url_encode(const std::string& in)
{
    static constexpr char k_hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(in.size() + in.size() / 4);

    for (const unsigned char c : in)
    {
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
                                c == '~';

        if (unreserved)
        {
            out.push_back(static_cast<char>(c));
        }
        else
        {
            out.push_back('%');
            out.push_back(k_hex[c >> 4]);
            out.push_back(k_hex[c & 0x0F]);
        }
    }
    return out;
}

std::string wide_to_utf8(const std::wstring& in)
{
    if (in.empty())
        return {};

    const int size = ::WideCharToMultiByte(CP_UTF8, 0, in.data(), static_cast<int>(in.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};

    std::string out(static_cast<size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, in.data(), static_cast<int>(in.size()), out.data(), size,
                          nullptr, nullptr);
    return out;
}

// ── Just enough JSON ────────────────────────────────────────────────────────
//
// KeyAuth's responses are small and flat, and pulling in a JSON library for
// four fields is not worth the build time. These read a named field anywhere
// in the document, which is safe here because each name we ask for appears
// once - but it is scanning, not parsing, so do not reach for it on arbitrary
// JSON where a name could repeat under a different object.

std::string json_field(const std::string& body, const char* name)
{
    const std::string needle = std::string("\"") + name + "\"";
    const size_t at = body.find(needle);
    if (at == std::string::npos)
        return {};

    size_t i = body.find(':', at + needle.size());
    if (i == std::string::npos)
        return {};
    i++;

    while (i < body.size() && (body[i] == ' ' || body[i] == '\t'))
        i++;
    if (i >= body.size())
        return {};

    if (body[i] == '"')
    {
        i++;
        std::string out;
        while (i < body.size() && body[i] != '"')
        {
            if (body[i] == '\\' && i + 1 < body.size())
                i++;
            out.push_back(body[i++]);
        }
        return out;
    }

    const size_t end = body.find_first_of(",}]", i);
    return body.substr(i, (end == std::string::npos ? body.size() : end) - i);
}

bool json_is_true(const std::string& body, const char* name)
{
    return json_field(body, name) == "true";
}

// ── HWID ────────────────────────────────────────────────────────────────────
//
// The Windows machine GUID, which is what KeyAuth's own clients use. It
// survives reboots and app reinstalls but changes on an OS reinstall, so a
// user who reimages their machine needs a HWID reset from the dashboard -
// which is why the licence screen shows this value.

std::string read_machine_guid()
{
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0,
                        KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
        return {};

    wchar_t buffer[128] = {};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    const LSTATUS status = ::RegQueryValueExW(key, L"MachineGuid", nullptr, &type,
                                              reinterpret_cast<LPBYTE>(buffer), &size);
    ::RegCloseKey(key);

    if (status != ERROR_SUCCESS || type != REG_SZ)
        return {};

    return wide_to_utf8(buffer);
}

// ── HMAC-SHA256, for the response signature ─────────────────────────────────

std::string hmac_sha256_hex(const std::string& key, const std::string& message)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (!BCRYPT_SUCCESS(::BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
                                                      BCRYPT_ALG_HANDLE_HMAC_FLAG)))
        return {};

    std::string result;
    BCRYPT_HASH_HANDLE hash = nullptr;

    DWORD hash_length = 0;
    DWORD copied = 0;
    if (BCRYPT_SUCCESS(::BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                                           reinterpret_cast<PUCHAR>(&hash_length),
                                           sizeof(hash_length), &copied, 0)) &&
        BCRYPT_SUCCESS(::BCryptCreateHash(algorithm, &hash, nullptr, 0,
                                          reinterpret_cast<PUCHAR>(const_cast<char*>(key.data())),
                                          static_cast<ULONG>(key.size()), 0)))
    {
        std::vector<unsigned char> digest(hash_length);

        if (BCRYPT_SUCCESS(
                ::BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(message.data())),
                                 static_cast<ULONG>(message.size()), 0)) &&
            BCRYPT_SUCCESS(::BCryptFinishHash(hash, digest.data(), hash_length, 0)))
        {
            static constexpr char k_hex[] = "0123456789abcdef";
            result.reserve(static_cast<size_t>(hash_length) * 2);
            for (const unsigned char byte : digest)
            {
                result.push_back(k_hex[byte >> 4]);
                result.push_back(k_hex[byte & 0x0F]);
            }
        }
    }

    if (hash)
        ::BCryptDestroyHash(hash);
    ::BCryptCloseAlgorithmProvider(algorithm, 0);
    return result;
}

// Compares in time independent of where the first difference is, so a caller
// on the same machine cannot learn the expected signature a byte at a time.
bool constant_time_equal(const std::string& a, const std::string& b)
{
    if (a.size() != b.size() || a.empty())
        return false;

    unsigned char difference = 0;
    for (size_t i = 0; i < a.size(); i++)
        difference |= static_cast<unsigned char>(a[i] ^ b[i]);

    return difference == 0;
}

// ── HTTP ────────────────────────────────────────────────────────────────────

struct http_response
{
    bool sent = false; // the request reached KeyAuth and came back
    DWORD status = 0;
    std::string body;
    std::string signature; // the "signature" response header, if present
};

http_response post_form(const std::string& form)
{
    http_response out;

    HINTERNET session = ::WinHttpOpen(L"SZK", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        return out;

    // A licence check should not hold the login screen for half a minute if the
    // network is black-holed; these are deliberately shorter than the defaults.
    ::WinHttpSetTimeouts(session, 5000, 5000, 8000, 8000);

    HINTERNET connection =
        ::WinHttpConnect(session, keyauth_config::api_host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection)
    {
        ::WinHttpCloseHandle(session);
        return out;
    }

    HINTERNET request =
        ::WinHttpOpenRequest(connection, L"POST", keyauth_config::api_path, nullptr,
                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request)
    {
        ::WinHttpCloseHandle(connection);
        ::WinHttpCloseHandle(session);
        return out;
    }

    static constexpr wchar_t k_content_type[] =
        L"Content-Type: application/x-www-form-urlencoded\r\n";

    bool ok = ::WinHttpAddRequestHeaders(request, k_content_type, static_cast<DWORD>(-1),
                                         WINHTTP_ADDREQ_FLAG_ADD) != FALSE;

    ok = ok && ::WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                    const_cast<char*>(form.data()), static_cast<DWORD>(form.size()),
                                    static_cast<DWORD>(form.size()), 0) != FALSE;

    ok = ok && ::WinHttpReceiveResponse(request, nullptr) != FALSE;

    if (ok)
    {
        DWORD status_size = sizeof(out.status);
        ::WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &out.status, &status_size,
                              WINHTTP_NO_HEADER_INDEX);

        DWORD signature_size = 0;
        ::WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, L"signature", WINHTTP_NO_OUTPUT_BUFFER,
                              &signature_size, WINHTTP_NO_HEADER_INDEX);
        if (signature_size > 0 && ::GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        {
            std::wstring wide(signature_size / sizeof(wchar_t), L'\0');
            if (::WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, L"signature", wide.data(),
                                      &signature_size, WINHTTP_NO_HEADER_INDEX))
            {
                wide.resize(signature_size / sizeof(wchar_t));
                while (!wide.empty() && wide.back() == L'\0')
                    wide.pop_back();
                out.signature = wide_to_utf8(wide);
            }
        }

        for (;;)
        {
            DWORD available = 0;
            if (!::WinHttpQueryDataAvailable(request, &available) || available == 0)
                break;

            const size_t offset = out.body.size();
            out.body.resize(offset + available);

            DWORD read = 0;
            if (!::WinHttpReadData(request, out.body.data() + offset, available, &read))
            {
                out.body.resize(offset);
                break;
            }
            out.body.resize(offset + read);
        }

        out.sent = true;
    }

    ::WinHttpCloseHandle(request);
    ::WinHttpCloseHandle(connection);
    ::WinHttpCloseHandle(session);
    return out;
}

// ── Worker state ────────────────────────────────────────────────────────────

std::mutex g_mutex;
auth_result g_result;
std::atomic<bool> g_running{false};
std::thread g_worker;

void publish(auth_status status, std::string message)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_result = auth_result{};
    g_result.status = status;
    g_result.message = std::move(message);
}

void publish_success(const std::string& subscription, long long expiry)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_result = auth_result{};
    g_result.status = auth_status::authorised;
    g_result.subscription = subscription;
    g_result.expiry_unix = expiry;

    if (expiry > 0)
    {
        const auto now = static_cast<long long>(::time(nullptr));
        g_result.days_left = static_cast<int>((expiry - now) / 86400);

        const auto stamp = static_cast<time_t>(expiry);
        tm parts{};
        if (::localtime_s(&parts, &stamp) == 0)
        {
            char buffer[32] = {};
            if (::strftime(buffer, sizeof(buffer), "%d %b %Y", &parts) > 0)
                g_result.expiry_readable = buffer;
        }

        char line[96];
        std::snprintf(line, sizeof(line), "Licence valid until %s.",
                      g_result.expiry_readable.empty() ? "the date on your key"
                                                       : g_result.expiry_readable.c_str());
        g_result.message = line;
    }
    else
    {
        g_result.message = "Licence valid.";
    }
}

// Turns whatever KeyAuth said into something the user can act on. KeyAuth's own
// messages are terse ("Invalid Key"), so the common ones get a second sentence
// that says what to do next.
std::string explain(const std::string& keyauth_message)
{
    if (keyauth_message.find("Invalid Key") != std::string::npos ||
        keyauth_message.find("not found") != std::string::npos)
        return "That key was not recognised. Check for a typo, or paste it again from your "
               "purchase email.";

    if (keyauth_message.find("HWID") != std::string::npos ||
        keyauth_message.find("hwid") != std::string::npos)
        return "This key is locked to a different machine. Send the hardware ID shown below to "
               "support for a reset.";

    if (keyauth_message.find("expired") != std::string::npos ||
        keyauth_message.find("Expired") != std::string::npos)
        return "This key has expired. Renew it to sign in again.";

    if (keyauth_message.find("banned") != std::string::npos ||
        keyauth_message.find("Banned") != std::string::npos)
        return "This key has been banned. Contact support if you think that is a mistake.";

    if (keyauth_message.empty())
        return "KeyAuth rejected the key but did not say why. Try again in a moment.";

    return keyauth_message;
}

void run_check(std::string key)
{
    // 1. init - establishes a session and proves the app version matches.
    std::string form;
    form.reserve(256);
    form += "type=init";
    form += "&ver=" + url_encode(keyauth_config::version);
    form += "&name=" + url_encode(keyauth_config::name);
    form += "&ownerid=" + url_encode(keyauth_config::ownerid);

    http_response init = post_form(form);

    if (!init.sent)
    {
        publish(auth_status::failed,
                "Could not reach the licence server. Check your internet connection, then try "
                "again.");
        g_running.store(false);
        return;
    }

    if (keyauth_config::verify_response_signature)
    {
        const std::string expected = hmac_sha256_hex(keyauth_config::secret, init.body);
        if (!constant_time_equal(expected, init.signature))
        {
            publish(auth_status::failed,
                    "The licence server's reply could not be verified. If you are on a public or "
                    "filtered network, try another connection.");
            g_running.store(false);
            return;
        }
    }

    if (!json_is_true(init.body, "success"))
    {
        const std::string why = json_field(init.body, "message");
        publish(auth_status::failed, why.empty()
                                         ? "The application could not start a licence session."
                                         : "Licence session refused: " + why);
        g_running.store(false);
        return;
    }

    const std::string session_id = json_field(init.body, "sessionid");

    // 2. license - the actual key check, bound to this machine.
    form.clear();
    form += "type=license";
    form += "&key=" + url_encode(key);
    form += "&hwid=" + url_encode(auth_hwid());
    form += "&sessionid=" + url_encode(session_id);
    form += "&name=" + url_encode(keyauth_config::name);
    form += "&ownerid=" + url_encode(keyauth_config::ownerid);

    http_response check = post_form(form);

    if (!check.sent)
    {
        publish(auth_status::failed,
                "The connection dropped while checking the key. Try again in a moment.");
        g_running.store(false);
        return;
    }

    if (keyauth_config::verify_response_signature)
    {
        const std::string expected = hmac_sha256_hex(keyauth_config::secret, check.body);
        if (!constant_time_equal(expected, check.signature))
        {
            publish(auth_status::failed,
                    "The licence server's reply could not be verified. Nothing was unlocked.");
            g_running.store(false);
            return;
        }
    }

    if (!json_is_true(check.body, "success"))
    {
        publish(auth_status::failed, explain(json_field(check.body, "message")));
        g_running.store(false);
        return;
    }

    const std::string subscription = json_field(check.body, "subscription");
    const std::string expiry_text = json_field(check.body, "expiry");

    long long expiry = 0;
    if (!expiry_text.empty())
        expiry = ::_strtoi64(expiry_text.c_str(), nullptr, 10);

    // KeyAuth already refuses an expired key, so this is a second opinion
    // rather than the only check - it catches a clock or timezone surprise
    // before the user gets a shell they cannot use.
    if (expiry > 0 && expiry <= static_cast<long long>(::time(nullptr)))
    {
        publish(auth_status::failed, "This key has expired. Renew it to sign in again.");
        g_running.store(false);
        return;
    }

    publish_success(subscription, expiry);
    g_running.store(false);
}
} // namespace

bool auth_begin(const std::string& key)
{
    if (g_running.load())
        return false;

    if (key.empty())
    {
        publish(auth_status::failed, "Enter your licence key.");
        return false;
    }

    if (!keyauth_config::is_configured())
    {
        publish(auth_status::failed, "This build has no licence credentials compiled in. Fill in "
                                     "src/backend/keyauth_config.h and rebuild.");
        return false;
    }

    if (g_worker.joinable())
        g_worker.join();

    publish(auth_status::working, "Checking your key...");
    g_running.store(true);
    g_worker = std::thread(run_check, key);
    return true;
}

auth_result auth_status_now()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_result;
}

void auth_reset()
{
    if (g_running.load())
        return;

    std::lock_guard<std::mutex> lock(g_mutex);
    g_result = auth_result{};
}

std::string auth_hwid()
{
    static const std::string cached = read_machine_guid();
    return cached;
}

void auth_shutdown()
{
    if (g_worker.joinable())
        g_worker.join();
}
} // namespace szk::backend
