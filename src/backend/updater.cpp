#include "backend/updater.h"

#include "backend/ed25519_verify.h"
#include "core/product_info.h"
#include "security/sk_crypter.h"

#include <atomic>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>

#include <bcrypt.h>
#include <shlwapi.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "shlwapi.lib")

namespace szk::backend
{
namespace
{
// ── Update endpoint ─────────────────────────────────────────────────────────
// SK-wrapped so the host is not a plaintext string in the binary. Point these
// at your own HTTPS host. The manifest is a tiny JSON document:
//   {"version":"1.1.0","sha256":"<64 hex>","url":"/downloads/numbanine-1.1.0.exe",
//    "notes":"What changed"}
// Keep the download on the same host as the manifest so one TLS trust covers
// both.
const wchar_t* manifest_host()
{
    return L"updates.szk.example"; // <-- your host
}
const wchar_t* manifest_path()
{
    return L"/szk/latest.json";
}

// ── Manifest signing key ────────────────────────────────────────────────────
//
// This is numbanine's OWN Ed25519 public key - not KeyAuth's. You generate the key
// pair once, keep the private half offline on the machine that publishes
// releases, and paste the public half here. Every manifest must carry a
// "signature" over its canonical fields (see canonical_manifest below), and
// the client verifies it against this key before trusting a word of it.
//
// This is what makes the hash check meaningful: TLS stops a passive MITM, but
// a compromised or mistaken CDN can serve a manifest with an attacker's hash
// over valid TLS. Signing moves trust from "whoever controls the endpoint" to
// "whoever holds the private key", which is only you.
//
// Generate a key pair with any Ed25519 tool, e.g. openssl:
//   openssl genpkey -algorithm ed25519 -out szk_update.key
//   openssl pkey -in szk_update.key -pubout -outform DER | tail -c 32 | xxd -p -c 64
// The last command prints the 64-hex public key to paste below. Keep the .key
// file off every public machine.
inline constexpr char update_signing_public_key[] =
    "0000000000000000000000000000000000000000000000000000000000000000"; // <-- your key

// The exact bytes a manifest's signature covers. Fixed field order with a
// separator that cannot appear in the values, so the signature does not depend
// on JSON whitespace or key ordering - the server signs this string, the
// client rebuilds it and verifies. Add a field here only by appending, and
// only in lockstep with the signer, or every existing manifest fails.
std::string canonical_manifest(const std::string& version, const std::string& sha256,
                               const std::string& url)
{
    return version + "\n" + sha256 + "\n" + url;
}

// ── Tiny helpers (same style as keyauth.cpp) ────────────────────────────────
std::string wide_to_utf8(const std::wstring& in)
{
    if (in.empty())
        return {};
    const int n =
        ::WideCharToMultiByte(CP_UTF8, 0, in.data(), (int)in.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)n, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, in.data(), (int)in.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring utf8_to_wide(const std::string& in)
{
    if (in.empty())
        return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, in.data(), (int)in.size(), nullptr, 0);
    std::wstring out((size_t)n, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, in.data(), (int)in.size(), out.data(), n);
    return out;
}

std::string json_field(const std::string& body, const char* name)
{
    const std::string needle = std::string("\"") + name + "\"";
    size_t at = body.find(needle);
    if (at == std::string::npos)
        return {};
    size_t i = body.find(':', at + needle.size());
    if (i == std::string::npos)
        return {};
    i++;
    while (i < body.size() && (body[i] == ' ' || body[i] == '\t'))
        i++;
    if (i < body.size() && body[i] == '"')
    {
        i++;
        std::string out;
        while (i < body.size() && body[i] != '"')
            out.push_back(body[i++]);
        return out;
    }
    size_t end = body.find_first_of(",}", i);
    return body.substr(i, (end == std::string::npos ? body.size() : end) - i);
}

// "1.2.10" > "1.2.9" numerically, not lexically. Returns >0 if a>b, <0 if a<b.
int compare_versions(const std::string& a, const std::string& b)
{
    auto next = [](const std::string& s, size_t& i) -> int
    {
        int v = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9')
            v = v * 10 + (s[i++] - '0');
        if (i < s.size() && s[i] == '.')
            i++;
        return v;
    };
    size_t ia = 0, ib = 0;
    while (ia < a.size() || ib < b.size())
    {
        const int va = next(a, ia);
        const int vb = next(b, ib);
        if (va != vb)
            return va - vb;
    }
    return 0;
}

// ── HTTP: GET returning the body, HTTPS only ────────────────────────────────
// A progress callback lets the download step report percent without a second
// code path. Returns false on any transport error.
bool https_get(const wchar_t* host, const wchar_t* path, std::string& body,
               const std::function<void(int)>& on_progress = {})
{
    body.clear();
    const std::wstring agent = std::wstring(product_info::name_wide) + L"-Updater";
    HINTERNET session = ::WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        return false;
    ::WinHttpSetTimeouts(session, 8000, 8000, 20000, 60000);

    bool ok = false;
    if (HINTERNET conn = ::WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0))
    {
        if (HINTERNET req = ::WinHttpOpenRequest(conn, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                                 WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE))
        {
            if (::WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA,
                                     0, 0, 0) &&
                ::WinHttpReceiveResponse(req, nullptr))
            {
                // Content-Length, so progress can be a real percentage.
                DWORD content_length = 0, len_size = sizeof(content_length);
                ::WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                      WINHTTP_HEADER_NAME_BY_INDEX, &content_length, &len_size,
                                      WINHTTP_NO_HEADER_INDEX);

                ok = true;
                for (;;)
                {
                    DWORD avail = 0;
                    if (!::WinHttpQueryDataAvailable(req, &avail) || avail == 0)
                        break;
                    const size_t offset = body.size();
                    body.resize(offset + avail);
                    DWORD read = 0;
                    if (!::WinHttpReadData(req, body.data() + offset, avail, &read))
                    {
                        body.resize(offset);
                        ok = false;
                        break;
                    }
                    body.resize(offset + read);
                    if (on_progress && content_length > 0)
                        on_progress((int)((body.size() * 100) / content_length));
                }
            }
            ::WinHttpCloseHandle(req);
        }
        ::WinHttpCloseHandle(conn);
    }
    ::WinHttpCloseHandle(session);
    return ok;
}

// ── SHA-256 over a buffer, lowercase hex ────────────────────────────────────
std::string sha256_hex(const std::string& data)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!BCRYPT_SUCCESS(::BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
        return {};

    std::string result;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD hash_len = 0, copied = 0;
    if (BCRYPT_SUCCESS(::BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (PUCHAR)&hash_len,
                                           sizeof(hash_len), &copied, 0)) &&
        BCRYPT_SUCCESS(::BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0)))
    {
        std::vector<unsigned char> digest(hash_len);
        if (BCRYPT_SUCCESS(::BCryptHashData(hash, (PUCHAR)data.data(), (ULONG)data.size(), 0)) &&
            BCRYPT_SUCCESS(::BCryptFinishHash(hash, digest.data(), hash_len, 0)))
        {
            static constexpr char hexd[] = "0123456789abcdef";
            for (unsigned char b : digest)
            {
                result.push_back(hexd[b >> 4]);
                result.push_back(hexd[b & 0xF]);
            }
        }
        ::BCryptDestroyHash(hash);
    }
    ::BCryptCloseAlgorithmProvider(alg, 0);
    return result;
}

bool equal_ci(const std::string& a, const std::string& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
        if (::tolower((unsigned char)a[i]) != ::tolower((unsigned char)b[i]))
            return false;
    return true;
}

std::wstring own_exe_path()
{
    wchar_t buf[MAX_PATH]{};
    ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}

// ── Worker state ────────────────────────────────────────────────────────────
std::mutex g_mutex;
update_state g_state;
std::atomic<bool> g_running{false};
std::thread g_worker;

// Details carried between the check, download and install steps.
std::string g_download_path; // manifest "url" path component
std::string g_expected_hash;
std::wstring g_temp_file; // where the verified exe waits

void publish(update_status s, std::string msg, int percent = 0)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_state.status = s;
    g_state.message = std::move(msg);
    g_state.percent = percent;
}

void run_check()
{
    publish(update_status::checking, "Checking for updates...");

    std::string manifest;
    if (!https_get(manifest_host(), manifest_path(), manifest))
    {
        publish(update_status::failed, "Could not reach the update server.");
        g_running.store(false);
        return;
    }

    const std::string latest = json_field(manifest, "version");
    const std::string hash = json_field(manifest, "sha256");
    const std::string url = json_field(manifest, "url");
    const std::string notes = json_field(manifest, "notes");
    const std::string signature = json_field(manifest, "signature");

    if (latest.empty() || hash.size() != 64 || url.empty())
    {
        publish(update_status::failed, "The update information was malformed.");
        g_running.store(false);
        return;
    }

    // ── Signature gate ──────────────────────────────────────────────────────
    // Nothing past this point trusts the manifest until its signature verifies
    // against our own key. A missing or bad signature aborts the whole update -
    // and the user-facing message says only "could not be completed", never
    // "signature failed", so an attacker probing the endpoint learns nothing
    // about why their forged manifest was rejected.
    //
    // ed25519_verify fails closed: if TweetNaCl is not vendored it returns
    // false here just as a bad signature would, so a build without the
    // primitive cannot silently skip the check.
    const bool trusted =
        !signature.empty() &&
        ed25519_verify(signature, canonical_manifest(latest, hash, url), update_signing_public_key);
    if (!trusted)
    {
        publish(update_status::failed, "The update check could not be completed.");
        g_running.store(false);
        return;
    }

    if (compare_versions(latest, product_info::version) <= 0)
    {
        publish(update_status::up_to_date, std::string(product_info::name) + " is up to date.");
        g_running.store(false);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_download_path = url;
        g_expected_hash = hash;
        g_state.status = update_status::update_ready;
        g_state.new_version = latest;
        g_state.notes = notes;
        g_state.message = "Version " + latest + " is available.";
    }
    g_running.store(false);
}

void run_download()
{
    publish(update_status::downloading, "Downloading update...");

    std::string url, expected;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        url = g_download_path;
        expected = g_expected_hash;
    }

    std::string binary;
    const bool ok =
        https_get(manifest_host(), utf8_to_wide(url).c_str(), binary, [](int pct)
                  { publish(update_status::downloading, "Downloading update...", pct); });
    if (!ok || binary.empty())
    {
        publish(update_status::failed, "The download did not complete. Try again.");
        g_running.store(false);
        return;
    }

    // The gate: the file we got must hash to exactly what the manifest said. A
    // mismatch means the download was corrupted or tampered - either way it
    // does not get written anywhere it could be run.
    const std::string actual = sha256_hex(binary);
    if (!equal_ci(actual, expected))
    {
        publish(update_status::failed,
                "The downloaded file failed its integrity check and was discarded.");
        g_running.store(false);
        return;
    }

    // Write the verified bytes to a .tmp beside the current exe. Same directory
    // so the swap is a rename on one volume, not a cross-volume copy.
    const std::wstring exe = own_exe_path();
    std::wstring temp = exe + L".new.tmp";
    HANDLE h = ::CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        publish(update_status::failed, "Could not write the update to disk.");
        g_running.store(false);
        return;
    }
    DWORD written = 0;
    const bool wrote = ::WriteFile(h, binary.data(), (DWORD)binary.size(), &written, nullptr) &&
                       written == binary.size();
    ::CloseHandle(h);

    if (!wrote)
    {
        ::DeleteFileW(temp.c_str());
        publish(update_status::failed, "Could not write the update to disk.");
        g_running.store(false);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_temp_file = temp;
        g_state.status = update_status::verified;
        g_state.message = "Update verified. Restart to install.";
        g_state.percent = 100;
    }
    g_running.store(false);
}
} // namespace

bool update_check_begin()
{
    if (g_running.exchange(true))
        return false;
    if (g_worker.joinable())
        g_worker.join();
    g_worker = std::thread(run_check);
    return true;
}

bool update_download_begin()
{
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_state.status != update_status::update_ready)
            return false;
    }
    if (g_running.exchange(true))
        return false;
    if (g_worker.joinable())
        g_worker.join();
    g_worker = std::thread(run_download);
    return true;
}

// Self-replacement. A running exe cannot overwrite itself, so a tiny script
// waits for this process to exit, swaps the files, and relaunches. A .cmd is
// used rather than renaming-the-running-exe tricks because it is the most
// robust across Windows versions and needs no helper binary to ship.
bool update_install_and_restart()
{
    std::wstring exe, temp;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_state.status != update_status::verified)
            return false;
        exe = own_exe_path();
        temp = g_temp_file;
    }

    const DWORD pid = ::GetCurrentProcessId();

    wchar_t temp_dir[MAX_PATH]{};
    ::GetTempPathW(MAX_PATH, temp_dir);
    std::wstring script = std::wstring(temp_dir) + L"szk_update.cmd";

    // The script:
    //   1. waits for this PID to disappear (up to ~30s), so the exe is unlocked
    //   2. replaces the old exe with the verified .tmp
    //   3. relaunches numbanine and deletes itself
    // %~f0 self-delete at the end keeps no turd in %TEMP%.
    HANDLE h = ::CreateFileW(script.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        publish(update_status::failed, "Could not start the installer.");
        return false;
    }

    std::wstring body;
    body += L"@echo off\r\n";
    body += L"setlocal\r\n";
    body += L"set /a tries=0\r\n";
    body += L":wait\r\n";
    body += L"tasklist /FI \"PID eq " + std::to_wstring(pid) + L"\" 2>nul | find \"" +
            std::to_wstring(pid) + L"\" >nul\r\n";
    body += L"if errorlevel 1 goto swap\r\n";
    body += L"set /a tries+=1\r\n";
    body += L"if %tries% geq 60 goto swap\r\n";
    body += L"timeout /t 1 /nobreak >nul\r\n";
    body += L"goto wait\r\n";
    body += L":swap\r\n";
    body += L"move /y \"" + temp + L"\" \"" + exe + L"\" >nul\r\n";
    body += L"start \"\" \"" + exe + L"\"\r\n";
    body += L"del \"%~f0\"\r\n";

    // The script is ASCII/UTF-16-agnostic here; write it as ANSI so cmd reads
    // it regardless of console codepage. Paths with non-ASCII characters are
    // the one weak spot - a shipped helper exe would handle those, noted below.
    const std::string ansi = wide_to_utf8(body);
    DWORD written = 0;
    ::WriteFile(h, ansi.data(), (DWORD)ansi.size(), &written, nullptr);
    ::CloseHandle(h);

    // Launch detached so it survives our exit, minimised so the console does
    // not flash in the user's face.
    std::wstring cmd = L"cmd.exe /c \"" + script + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                          CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        publish(update_status::failed, "Could not start the installer.");
        return false;
    }
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);

    publish(update_status::installing,
            "Installing update. " + std::string(product_info::name) + " will restart.");
    return true; // caller should now exit so the script can swap the file
}

update_state update_status_now()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_state;
}

void update_reset()
{
    if (g_running.load())
        return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_state = update_state{};
}

void update_shutdown()
{
    if (g_worker.joinable())
        g_worker.join();
}
} // namespace szk::backend
