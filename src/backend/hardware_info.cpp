#include "backend/hardware_info.h"

#include "core/product_info.h"

#include "backend/activity_log.h"

#include <windows.h>

#include <Wbemidl.h>
#include <comdef.h>
#include <shellapi.h>
#include <srrestoreptapi.h>

#include <cstdio>
#include <string>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "srclient.lib")

namespace szk::backend
{
namespace
{
struct com_guard
{
    HRESULT init_result;
    com_guard() : init_result(::CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~com_guard()
    {
        if (SUCCEEDED(init_result))
            ::CoUninitialize();
    }
    bool ok() const
    {
        // S_OK / S_FALSE = we (or someone else) initialized COM fine.
        return init_result == S_OK || init_result == S_FALSE;
    }
};
} // namespace

motherboard_info motherboard_query()
{
    motherboard_info info;

    com_guard com;
    if (!com.ok())
        return info;

    IWbemLocator* locator = nullptr;
    if (FAILED(::CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IWbemLocator, reinterpret_cast<LPVOID*>(&locator))) ||
        !locator)
        return info;

    IWbemServices* services = nullptr;
    HRESULT hr = locator->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr, nullptr, 0,
                                        nullptr, nullptr, &services);
    if (FAILED(hr) || !services)
    {
        locator->Release();
        return info;
    }

    ::CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

    IEnumWbemClassObject* enumerator = nullptr;
    hr = services->ExecQuery(_bstr_t(L"WQL"), _bstr_t(L"SELECT Product FROM Win32_BaseBoard"),
                             WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
                             &enumerator);
    if (SUCCEEDED(hr) && enumerator)
    {
        IWbemClassObject* object = nullptr;
        ULONG returned = 0;
        if (enumerator->Next(WBEM_INFINITE, 1, &object, &returned) == S_OK && returned > 0)
        {
            VARIANT value;
            ::VariantInit(&value);
            if (SUCCEEDED(object->Get(L"Product", 0, &value, nullptr, nullptr)) &&
                value.vt == VT_BSTR && value.bstrVal)
            {
                ::WideCharToMultiByte(CP_UTF8, 0, value.bstrVal, -1, info.product,
                                      sizeof(info.product), nullptr, nullptr);
                info.available = info.product[0] != '\0';
            }
            ::VariantClear(&value);
            object->Release();
        }
        enumerator->Release();
    }

    services->Release();
    locator->Release();
    return info;
}

void open_google_search(const char* query)
{
    std::string encoded;
    for (const char* p = query; *p; p++)
    {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9'))
            encoded += *p;
        else if (*p == ' ')
            encoded += '+';
        else
        {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned char>(*p));
            encoded += buf;
        }
    }

    const std::string url = "https://www.google.com/search?q=" + encoded;
    int wide_len = ::MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    std::wstring wide_url(static_cast<size_t>(wide_len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wide_url.data(), wide_len);

    ::ShellExecuteW(nullptr, L"open", wide_url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void open_windows_recovery_settings()
{
    ::ShellExecuteW(nullptr, L"open", L"ms-settings:recovery", nullptr, nullptr, SW_SHOWNORMAL);
}

bool create_system_restore_point()
{
    RESTOREPOINTINFOW info{};
    info.dwEventType = BEGIN_SYSTEM_CHANGE;
    info.dwRestorePtType = APPLICATION_INSTALL;
    wcsncpy_s(info.szDescription, (std::wstring(product_info::name_wide) + L" checkpoint").c_str(),
              _TRUNCATE);

    STATEMGRSTATUS status{};
    const bool ok = ::SRSetRestorePointW(&info, &status) != FALSE;
    log("System Restore",
        ok ? "Restore point created" : "Failed (System Restore may be turned off for this drive)");
    return ok;
}

void open_system_restore_wizard()
{
    ::ShellExecuteW(nullptr, L"open", L"rstrui.exe", nullptr, nullptr, SW_SHOWNORMAL);
}

void open_discord()
{
    ::ShellExecuteW(nullptr, L"open", L"https://discord.gg/nhb6v5pRtE", nullptr, nullptr,
                    SW_SHOWNORMAL);
}
} // namespace szk::backend
