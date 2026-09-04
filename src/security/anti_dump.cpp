#include "security/anti_dump.h"

#include "security/lazy_import.h"
#include "security/sk_crypter.h"

#include <windows.h>

namespace szk::sec
{
bool corrupt_own_pe_headers()
{
#ifndef SZK_ENABLE_ANTI_DUMP
    // Off by default. See the header for why this is the default.
    return false;
#else
    auto* base = reinterpret_cast<unsigned char*>(::GetModuleHandleW(nullptr));
    if (!base)
        return false;

    auto* dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;

    auto* nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;

    // The header region is read-only in the mapped image, so make it writable
    // first. VirtualProtect is resolved lazily so it is not a static import
    // that flags "this binary rewrites its own memory".
    using VP_t = BOOL(WINAPI*)(LPVOID, SIZE_T, DWORD, PDWORD);
    static lazy<VP_t> VP{SK("kernel32.dll"), SK("VirtualProtect")};
    if (!VP)
        return false;

    const SIZE_T header_size = nt->OptionalHeader.SizeOfHeaders;
    DWORD old_protect = 0;
    if (!VP.get()(base, header_size, PAGE_READWRITE, &old_protect))
        return false;

    // Wipe the two signatures a PE parser keys on, plus the section table it
    // needs to find the code. Leave the rest of the process untouched - this
    // is about the on-disk-shaped dump, not the running code.
    dos->e_magic = 0;
    nt->Signature = 0;

    auto* section = IMAGE_FIRST_SECTION(nt);
    const WORD section_count = nt->FileHeader.NumberOfSections;
    ::SecureZeroMemory(section, section_count * sizeof(IMAGE_SECTION_HEADER));

    // Restore protection so the wiped region does not stand out as RW among
    // otherwise-read-only header pages.
    DWORD ignored = 0;
    VP.get()(base, header_size, old_protect, &ignored);
    return true;
#endif
}
} // namespace szk::sec
