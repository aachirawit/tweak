#include "security/anti_debug.h"

#include "security/lazy_import.h"
#include "security/sk_crypter.h"

#include <windows.h>

#include <winternl.h> // PEB, PROCESSINFOCLASS

namespace szk::sec
{
namespace
{
// ProcessDebugPort (7), ProcessDebugObjectHandle (30) and ProcessDebugFlags
// (31) are not in winternl.h's PROCESSINFOCLASS enum, so name them here.
constexpr ULONG kProcessDebugPort = 7;
constexpr ULONG kProcessDebugObjectHandle = 30;

using NtQIP_t = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);

// Reads the current process PEB. On x64 it is at gs:[0x60]; __readgsqword is
// the documented intrinsic for this and avoids an inline-asm block.
PPEB current_peb()
{
#if defined(_M_X64) || defined(_M_AMD64)
    return reinterpret_cast<PPEB>(__readgsqword(0x60));
#else
    return reinterpret_cast<PPEB>(__readfsdword(0x30));
#endif
}
} // namespace

// The loader sets PEB->BeingDebugged when a debugger started the process. This
// is exactly what IsDebuggerPresent() reads - going straight to the PEB just
// skips the API, so a breakpoint on IsDebuggerPresent does not catch it.
bool check_peb_being_debugged()
{
    const PPEB peb = current_peb();
    return peb && peb->BeingDebugged != 0;
}

// A debugged process gets heap debugging flags set in PEB->NtGlobalFlag
// (FLG_HEAP_ENABLE_TAIL_CHECK | FREE_CHECK | VALIDATE_PARAMETERS = 0x70). The
// field sits at a fixed offset past the documented PEB struct, so reach it by
// byte offset rather than a struct member that winternl.h does not name.
bool check_peb_nt_global_flag()
{
    const auto* base = reinterpret_cast<const unsigned char*>(current_peb());
    if (!base)
        return false;

#if defined(_M_X64) || defined(_M_AMD64)
    constexpr size_t kNtGlobalFlagOffset = 0xBC;
#else
    constexpr size_t kNtGlobalFlagOffset = 0x68;
#endif
    const DWORD flags = *reinterpret_cast<const DWORD*>(base + kNtGlobalFlagOffset);
    constexpr DWORD kDebugHeapFlags = 0x70;
    return (flags & kDebugHeapFlags) != 0;
}

// ProcessDebugPort returns a non-zero port when a user-mode debugger is
// attached. Resolved lazily so "NtQueryInformationProcess" is not in the IAT.
bool check_debug_port()
{
    static lazy<NtQIP_t> NtQIP{SK("ntdll.dll"), SK("NtQueryInformationProcess")};
    if (!NtQIP)
        return false;

    DWORD_PTR debug_port = 0;
    const LONG status = NtQIP.get()(::GetCurrentProcess(), kProcessDebugPort, &debug_port,
                                    sizeof(debug_port), nullptr);
    return status == 0 && debug_port != 0;
}

// ProcessDebugObjectHandle returns a valid handle when a debug object exists
// for the process - present even for some debuggers that clear the debug port.
bool check_debug_object_handle()
{
    static lazy<NtQIP_t> NtQIP{SK("ntdll.dll"), SK("NtQueryInformationProcess")};
    if (!NtQIP)
        return false;

    HANDLE debug_object = nullptr;
    const LONG status = NtQIP.get()(::GetCurrentProcess(), kProcessDebugObjectHandle, &debug_object,
                                    sizeof(debug_object), nullptr);
    return status == 0 && debug_object != nullptr;
}

// Hardware breakpoints live in the CPU debug registers Dr0-Dr3. A non-zero
// value in any of them means a data/exec breakpoint is set - the kind a
// debugger uses without patching the code, so it is invisible to a checksum.
bool check_hardware_breakpoints()
{
    CONTEXT context{};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (!::GetThreadContext(::GetCurrentThread(), &context))
        return false;

    return context.Dr0 != 0 || context.Dr1 != 0 || context.Dr2 != 0 || context.Dr3 != 0;
}

bool debugger_present()
{
    return check_peb_being_debugged() || check_peb_nt_global_flag() || check_debug_port() ||
           check_debug_object_handle() || check_hardware_breakpoints();
}
} // namespace szk::sec
