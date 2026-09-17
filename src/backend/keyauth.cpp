#include "backend/keyauth.h"

#include "core/product_info.h"

#include "backend/ed25519_verify.h"
#include "backend/keyauth_config.h"
#include "security/anti_debug.h"

#include <atomic>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>

#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

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

// ── Anti-debug, folded into the HWID ────────────────────────────────────────
//
// The design goal is that a debugger does not cause a visible, patchable
// branch. Instead its presence becomes data: a taint byte that is zero on a
// clean machine and non-zero under analysis. The taint is XORed into a copy of
// the HWID, so:
//
//   - Clean machine: taint 0, HWID unchanged, login works.
//   - Under a debugger: HWID silently wrong, KeyAuth returns "HWID mismatch",
//     login fails like any other machine-mismatch - no anti-debug string, no
//     local branch, the deciding logic sits on a server the cracker cannot
//     patch.
//
// It degrades rather than destroys: detaching the debugger restores the real
// HWID, so a false positive (some legit overlays trip PEB checks) costs the
// user a retry, never a permanently bricked licence. That reversibility is the
// whole reason this is preferable to corrupting state or calling exit().
//
// Kept deliberately plain here; the value it produces is what matters, not the
// obfuscation of the check. Obfuscating the check is what SK() and the lazy
// import of NtQueryInformationProcess inside debugger_present() already do.
unsigned char anti_debug_taint()
{
    // Any set bit invalidates the HWID. A fixed non-zero constant keeps the
    // result deterministic, so a debugged machine fails the same way every
    // time rather than flapping.
    return szk::sec::debugger_present() ? 0x5Au : 0x00u;
}

// The HWID actually sent in the licence request. auth_hwid() stays truthful for
// the value shown on the licence screen (support needs the real one); only the
// wire value carries the taint, and only when clean are the two identical.
std::string hwid_for_request()
{
    std::string hwid = read_machine_guid();
    const unsigned char taint = anti_debug_taint();
    if (taint && !hwid.empty())
    {
        // Perturb one character so the string stays a plausible 36-char GUID -
        // the server does a normal lookup and returns mismatch, not "malformed
        // HWID", which would stand out. Rotating a hex digit keeps it hex.
        const char c = hwid[0];
        const char rotated = (c >= '0' && c <= '9') ? static_cast<char>('0' + ((c - '0' + 1) % 10))
                             : (c >= 'a' && c <= 'f') ? static_cast<char>('a' + ((c - 'a' + 1) % 6))
                             : (c >= 'A' && c <= 'F') ? static_cast<char>('A' + ((c - 'A' + 1) % 6))
                                                      : c;
        hwid[0] = rotated;
    }
    return hwid;
}

// ── HTTP ────────────────────────────────────────────────────────────────────

struct http_response
{
    bool sent = false; // the request reached KeyAuth and came back
    DWORD status = 0;
    std::string body;

    // KeyAuth 1.3 signs responses the way Discord signs interactions: Ed25519
    // over (timestamp + body), returned in these two headers. The signing key
    // is KeyAuth's, not the application secret - the secret plays no part in
    // verifying a response.
    std::string signature_ed25519;
    std::string signature_timestamp;
};

// Reads one response header by name. Returns empty when the header is absent.
std::string query_header(HINTERNET request, const wchar_t* name)
{
    DWORD size = 0;
    ::WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, name, WINHTTP_NO_OUTPUT_BUFFER, &size,
                          WINHTTP_NO_HEADER_INDEX);
    if (size == 0 || ::GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return {};

    std::wstring wide(size / sizeof(wchar_t), L'\0');
    if (!::WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, name, wide.data(), &size,
                               WINHTTP_NO_HEADER_INDEX))
        return {};

    wide.resize(size / sizeof(wchar_t));
    while (!wide.empty() && wide.back() == L'\0')
        wide.pop_back();

    return wide_to_utf8(wide);
}

// Is this reply really from KeyAuth, and is it about now?
//
// The timestamp check alone closes replay: a "success" captured once cannot be
// served back tomorrow. The signature check closes forgery: a proxy with a
// trusted root certificate cannot mint a reply of its own. The signature is
// over (timestamp + body), the same construction Discord uses, so the
// timestamp cannot be swapped for a fresh one without breaking it.
//
// When TweetNaCl is not vendored, ed25519_available() is false and only the
// replay check applies. That is a real gap, so it is named in the failure
// path rather than passed over.
bool response_is_trusted(const http_response& response, const std::string& public_key_hex)
{
    if (response.signature_ed25519.empty() || response.signature_timestamp.empty())
        return false;

    const long long stamp = ::_strtoi64(response.signature_timestamp.c_str(), nullptr, 10);
    if (stamp <= 0)
        return false;

    const long long now = static_cast<long long>(::time(nullptr));
    const long long drift = now > stamp ? now - stamp : stamp - now;

    // Wide enough that a clock a few minutes out still works, narrow enough
    // that a saved response is useless by the next session.
    if (drift > 300)
        return false;

    if (!ed25519_available())
        return true; // replay-checked only; the build warned about this

    return ed25519_verify(response.signature_ed25519, response.signature_timestamp + response.body,
                          public_key_hex);
}

// One HTTPS POST. Shared by both backends: KeyAuth sends form-urlencoded to
// keyauth.win, the License Platform sends JSON to its own host. The caller
// supplies host, path, the Content-Type header line, and the raw body.
http_response http_post(const wchar_t* host, const wchar_t* path, const wchar_t* content_type,
                        const std::string& body)
{
    http_response out;

    HINTERNET session = ::WinHttpOpen(product_info::name_wide, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        return out;

    // A licence check should not hold the login screen for half a minute if the
    // network is black-holed; these are deliberately shorter than the defaults.
    ::WinHttpSetTimeouts(session, 5000, 5000, 8000, 8000);

    HINTERNET connection = ::WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection)
    {
        ::WinHttpCloseHandle(session);
        return out;
    }

    HINTERNET request =
        ::WinHttpOpenRequest(connection, L"POST", path, nullptr, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request)
    {
        ::WinHttpCloseHandle(connection);
        ::WinHttpCloseHandle(session);
        return out;
    }

    const std::string& form = body;

    bool ok = ::WinHttpAddRequestHeaders(request, content_type, static_cast<DWORD>(-1),
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

        out.signature_ed25519 = query_header(request, L"x-signature-ed25519");
        out.signature_timestamp = query_header(request, L"x-signature-timestamp");

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

// KeyAuth's form-urlencoded POST to the fixed KeyAuth endpoint.
http_response post_form(const std::string& form)
{
    return http_post(keyauth_config::api_host, keyauth_config::api_path,
                     L"Content-Type: application/x-www-form-urlencoded\r\n", form);
}

// ── License Platform helpers ─────────────────────────────────────────────────

std::wstring utf8_to_wide(const std::string& in)
{
    if (in.empty())
        return {};

    const int size =
        ::MultiByteToWideChar(CP_UTF8, 0, in.data(), static_cast<int>(in.size()), nullptr, 0);
    if (size <= 0)
        return {};

    std::wstring out(static_cast<size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, in.data(), static_cast<int>(in.size()), out.data(), size);
    return out;
}

std::string json_escape(const std::string& in)
{
    std::string out;
    out.reserve(in.size() + 8);
    for (const unsigned char c : in)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20)
            {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                out += buffer;
            }
            else
            {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    return out;
}

// "2026-10-04T00:00:00.000Z" -> unix seconds (UTC). 0 for null/empty/unparseable
// or a lifetime licence (the server sends null).
long long parse_iso8601_utc(const std::string& s)
{
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    // sscanf_s matches sscanf for %d (no buffer arguments); MSVC's /WX rejects
    // the non-_s form as deprecated.
    if (::sscanf_s(s.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6)
        return 0;

    tm parts{};
    parts.tm_year = y - 1900;
    parts.tm_mon = mo - 1;
    parts.tm_mday = d;
    parts.tm_hour = h;
    parts.tm_min = mi;
    parts.tm_sec = se;

    const long long unix = static_cast<long long>(::_mkgmtime(&parts));
    return unix < 0 ? 0 : unix;
}

// Maps the platform's stable refusal codes to an actionable sentence. The server
// also sends a `message`, but the code is the contract, so we key off it.
std::string explain_platform(const std::string& code)
{
    if (code == "INVALID_LICENSE")
        return "That key was not recognised. Check for a typo, or paste it again from your "
               "purchase email.";
    if (code == "HWID_MISMATCH")
        return "This key is locked to a different machine. Send the hardware ID shown below to "
               "support for a reset.";
    if (code == "LICENSE_EXPIRED")
        return "This key has expired. Renew it to sign in again.";
    if (code == "LICENSE_BANNED")
        return "This key has been banned. Contact support if you think that is a mistake.";
    if (code == "LICENSE_REVOKED")
        return "This key has been revoked. Contact support if you think that is a mistake.";
    if (code == "RATE_LIMITED")
        return "Too many attempts. Wait a moment, then try again.";
    if (code == "VALIDATION_ERROR")
        return "The app sent a request the server rejected. Update to the latest version.";
    return "The licence server refused the key. Try again in a moment.";
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
    // KeyAuth's casing is not consistent between messages - the live reply for
    // a bad key is "Invalid license key", while its own docs write
    // "Invalid Key" - so match on a folded copy rather than guessing which.
    std::string folded;
    folded.reserve(keyauth_message.size());
    for (const unsigned char c : keyauth_message)
        folded.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c));

    auto says = [&folded](const char* needle) { return folded.find(needle) != std::string::npos; };

    if (says("invalid") || says("not found"))
        return "That key was not recognised. Check for a typo, or paste it again from your "
               "purchase email.";

    if (says("hwid") || says("hardware"))
        return "This key is locked to a different machine. Send the hardware ID shown below to "
               "support for a reset.";

    if (says("expired"))
        return "This key has expired. Renew it to sign in again.";

    if (says("banned") || says("blacklist"))
        return "This key has been banned. Contact support if you think that is a mistake.";

    if (says("used") || says("in use"))
        return "That key is already in use on another machine.";

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

    if (keyauth_config::check_response_freshness &&
        !response_is_trusted(init, keyauth_config::signing_public_key))
    {
        publish(auth_status::failed,
                "The licence server's reply was not signed or was out of date. If you are on a "
                "public or filtered network, try another connection.");
        g_running.store(false);
        return;
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
    //
    // The HWID sent here is the display HWID with an anti-debug taint folded
    // in. See hwid_for_request(): under a debugger it is perturbed, so KeyAuth
    // returns an ordinary "HWID mismatch" and the login fails for a reason that
    // looks nothing like an anti-debug trip. The deciding branch is on KeyAuth's
    // servers, not a local `if` a patch can cut, and the failure surfaces one
    // network round trip away from the check that caused it.
    form.clear();
    form += "type=license";
    form += "&key=" + url_encode(key);
    form += "&hwid=" + url_encode(hwid_for_request());
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

    if (keyauth_config::check_response_freshness &&
        !response_is_trusted(check, keyauth_config::signing_public_key))
    {
        publish(auth_status::failed,
                "The licence server's reply was not signed or was out of date. Nothing was "
                "unlocked.");
        g_running.store(false);
        return;
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

// License Platform check: a single POST /api/activate with a JSON body. The
// server owns every decision (validity, expiry, HWID binding) and answers with
// the shared envelope { success, code, data }. When the deployment is configured
// with a signing key, the reply is Ed25519-signed over (timestamp + body) and
// verified below - the same anti-MITM guarantee as the KeyAuth path. With no
// signing key set, the reply is trusted over TLS only.
void run_check_platform(std::string key)
{
    const std::string hwid = hwid_for_request();

    std::string body;
    body.reserve(160);
    body += "{\"appId\":\"" + json_escape(platform_config::app_id) + "\",";
    body += "\"key\":\"" + json_escape(key) + "\",";
    body += "\"hwid\":\"" + json_escape(hwid) + "\"}";

    const std::wstring host = utf8_to_wide(platform_config::api_host);
    const std::wstring path = utf8_to_wide(platform_config::api_path);

    const http_response res =
        http_post(host.c_str(), path.c_str(), L"Content-Type: application/json\r\n", body);

    if (!res.sent)
    {
        publish(auth_status::failed,
                "Could not reach the licence server. Check your internet connection, then try "
                "again.");
        g_running.store(false);
        return;
    }

    // When the deployment signs its replies, verify the Ed25519 signature over
    // (timestamp + body) before trusting anything in the response - the same
    // check the KeyAuth path runs. This is what stops a trusted-root proxy from
    // forging a "valid" reply. When signing is not configured, the reply is
    // trusted over TLS only.
    if (platform_config::signing_enabled() &&
        !response_is_trusted(res, platform_config::signing_public_key))
    {
        publish(auth_status::failed,
                "The licence server's reply was not signed or was out of date. Nothing was "
                "unlocked.");
        g_running.store(false);
        return;
    }

    // A refusal (403/429/400) still returns a well-formed envelope with
    // success:false and a code. Only fall back to the HTTP status when the body
    // is not the envelope at all (e.g. a proxy error page).
    if (!json_is_true(res.body, "success"))
    {
        const std::string code = json_field(res.body, "code");
        publish(auth_status::failed,
                code.empty() ? "The licence server refused the key. Try again in a moment."
                             : explain_platform(code));
        g_running.store(false);
        return;
    }

    // expiresAt is an ISO-8601 string, or null for a lifetime licence.
    const std::string expiry_text = json_field(res.body, "expiresAt");
    long long expiry = 0;
    if (!expiry_text.empty() && expiry_text != "null")
        expiry = parse_iso8601_utc(expiry_text);

    // Second opinion on expiry, mirroring the KeyAuth path: catch a clock or
    // timezone surprise before handing the user a shell they cannot use.
    if (expiry > 0 && expiry <= static_cast<long long>(::time(nullptr)))
    {
        publish(auth_status::failed, "This key has expired. Renew it to sign in again.");
        g_running.store(false);
        return;
    }

    // The activate response carries no plan name, only status; leave the
    // subscription blank rather than showing the status enum to the user.
    publish_success(/*subscription=*/std::string(), expiry);
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

    if constexpr (license_backend::active == license_backend::kind::platform)
    {
        if (!platform_config::is_configured())
        {
            publish(auth_status::failed,
                    "This build has no licence server configured. Set SZK_PLATFORM_HOST and "
                    "SZK_PLATFORM_APP_ID in src/backend/keyauth_secrets.h and rebuild.");
            return false;
        }
    }
    else
    {
        if (!keyauth_config::is_configured())
        {
            publish(auth_status::failed,
                    "This build has no licence credentials compiled in. Fill in "
                    "src/backend/keyauth_config.h and rebuild.");
            return false;
        }
    }

    if (g_worker.joinable())
        g_worker.join();

    publish(auth_status::working, "Checking your key...");
    g_running.store(true);

    if constexpr (license_backend::active == license_backend::kind::platform)
        g_worker = std::thread(run_check_platform, key);
    else
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
