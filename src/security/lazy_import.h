#pragma once

#include <windows.h>

// ─── Dynamic API resolution (lazy imports) ──────────────────────────────────
//
// Calling an API the ordinary way puts its name in the Import Address Table,
// where `dumpbin /imports` reads it straight off. Resolving it at runtime by
// name keeps it out of the IAT, so a static look at the binary does not reveal
// that numbanine calls, say, NtQueryInformationProcess.
//
// Worth doing for the handful of calls that give away a technique - the
// anti-debug syscalls, VirtualProtect for the anti-dump step. Not worth doing
// for everything: hiding GetTickCount fools nobody and only slows the code.
// And it hides names from a STATIC view only - a debugger or an API monitor
// watching live calls sees the real function regardless.
//
// Usage:
//   using NtQIP_t = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
//   auto NtQIP = szk::sec::resolve<NtQIP_t>(SK("ntdll.dll"), SK("NtQueryInformationProcess"));
//   if (NtQIP) NtQIP(h, cls, buf, len, ret);
//
// Pair it with SK() so the module and function names are not plaintext either;
// resolve() takes const char*, which SK() converts to.

namespace szk::sec
{
// Resolves a function by module + export name at call time. Modules already
// loaded (ntdll, kernel32) are found with GetModuleHandleA and never appear as
// a LoadLibrary either; anything else is loaded on demand. Returns nullptr if
// the module or export is missing, so every caller must null-check - which is
// also the graceful path when a future Windows drops an export.
template <typename Fn> Fn resolve(const char* module_name, const char* function_name)
{
    HMODULE module = ::GetModuleHandleA(module_name);
    if (!module)
        module = ::LoadLibraryA(module_name);
    if (!module)
        return nullptr;

    // GetProcAddress by name still touches the export table at runtime, but the
    // string is ours (SK-decrypted on the stack), not a static IAT entry.
    FARPROC proc = ::GetProcAddress(module, function_name);
    return reinterpret_cast<Fn>(proc);
}

// Caches a resolved pointer so repeated calls do not re-walk the export table.
// Declare one static instance per API at its call site:
//   static szk::sec::lazy<NtQIP_t> NtQIP{SK("ntdll.dll"), SK("NtQueryInformationProcess")};
//   if (NtQIP) NtQIP(...);
template <typename Fn> class lazy
{
  public:
    lazy(const char* module_name, const char* function_name)
        : module_(module_name), function_(function_name)
    {
    }

    Fn get()
    {
        if (!resolved_)
        {
            fn_ = resolve<Fn>(module_, function_);
            resolved_ = true;
        }
        return fn_;
    }

    explicit operator bool()
    {
        return get() != nullptr;
    }

    template <typename... Args> auto operator()(Args... args) -> decltype(Fn()(args...))
    {
        return get()(args...);
    }

  private:
    const char* module_;
    const char* function_;
    Fn fn_ = nullptr;
    bool resolved_ = false;
};
} // namespace szk::sec
