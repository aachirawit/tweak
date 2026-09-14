#include "core/i18n.h"

#include <string_view>
#include <unordered_map>

namespace szk::i18n
{
namespace
{
lang g_language = lang::en;

struct entry
{
    const char* en;
    const char* th;
};

// Thai for the strings a user reads on the way through the app: navigation,
// page headings, tab names, and the buttons and labels on the surfaces those
// pages show. Anything absent falls back to English by design - see tr().
//
// Product nouns are left in English on purpose. "Dashboard", "ReShade",
// "NVIDIA", "FiveM" and the tweak names are what the rest of the ecosystem
// calls them, and translating them would make the settings harder to match
// against a guide, not easier.
constexpr entry k_table[] = {
    // ── Navigation ──────────────────────────────────────────────────────────
    {"Dashboard", "แดชบอร์ด"},
    {"Performance", "ประสิทธิภาพ"},
    {"Graphics", "กราฟิก"},
    {"Network", "เครือข่าย"},
    {"Power plan", "แผนการใช้พลังงาน"},
    {"Cleanup", "ล้างไฟล์ขยะ"},
    {"Auto ReShade", "ติดตั้ง ReShade อัตโนมัติ"},
    {"All tweaks", "การตั้งค่าทั้งหมด"},
    {"This machine", "เครื่องนี้"},
    {"Drivers", "ไดรเวอร์"},
    {"About", "เกี่ยวกับ"},
    {"OPTIMIZE", "ปรับแต่ง"},
    {"SYSTEM", "ระบบ"},

    // ── Shell chrome ────────────────────────────────────────────────────────
    {"Search", "ค้นหา"},
    {"ACTIVE VIEW", "หน้าที่เปิดอยู่"},
    {"Press Ctrl+B to toggle", "กด Ctrl+B เพื่อย่อ/ขยาย"},
    {"SETTINGS", "การตั้งค่า"},

    // ── Dashboard ───────────────────────────────────────────────────────────
    {"What this machine is doing, and what would make it quicker.",
     "เครื่องนี้กำลังทำอะไรอยู่ และอะไรจะทำให้เร็วขึ้น"},
    {"SCORE", "คะแนน"},
    {"Optimize now", "ปรับแต่งเลย"},
    {"Restore point created first", "สร้างจุดคืนค่าระบบก่อนเสมอ"},
    {"Needs attention", "ควรแก้"},
    {"Scan again", "สแกนใหม่"},
    {"Fix", "แก้"},
    {"urgent", "ด่วน"},
    {"advised", "แนะนำ"},
    {"optional", "ทำก็ได้"},
    {"Urgent", "ด่วน"},
    {"Advised", "แนะนำ"},
    {"Optional", "ทำก็ได้"},
    {"All verified", "ยืนยันครบแล้ว"},
    {"Reads the registry, changes nothing", "อ่านรีจิสทรีอย่างเดียว ไม่แก้อะไร"},
    {"CPU Usage", "การใช้ CPU"},
    {"RAM Usage", "การใช้ RAM"},
    {"Disk Usage", "การใช้ดิสก์"},
    {"Ping", "ปิง"},
    {"Verified tweaks", "การตั้งค่าที่ยืนยันแล้ว"},
    {"Uptime", "เปิดเครื่องมาแล้ว"},
    {"Unknown", "ไม่ทราบ"},

    // ── Dashboard, continued ─────────────────────────────────────────────
    // The count sentence carries a single %d: Thai has no plural, and English
    // keeps its own singular at the call site rather than in the table.
    {"Nothing left to fix here", "ไม่มีอะไรต้องแก้แล้ว"},
    {"%d tweaks would help this machine", "มี %d อย่างที่ช่วยเครื่องนี้ได้"},
    {"%d of the %d settings this app can read back are already applied. The rest are listed "
     "below, loudest first.",
     "ใช้งานแล้ว %d จาก %d อย่างที่อ่านสถานะกลับได้ ที่เหลืออยู่ข้างล่าง เรียงตามความสำคัญ"},
    {"Everything this app can verify is already applied.",
     "ทุกอย่างที่อ่านสถานะกลับได้ ใช้งานครบแล้ว"},
    {"%d of %d applied", "ใช้แล้ว %d จาก %d"},
    {"no route", "ไม่มีเส้นทาง"},
    {"live", "สด"},

    // ── Results ─────────────────────────────────────────────────────────────────
    {"%d applied, %d failed. Restore point taken first.",
     "ใช้สำเร็จ %d ไม่สำเร็จ %d สร้างจุดคืนค่าไว้ก่อนแล้ว"},
    {"Nothing was changed", "ไม่ได้เปลี่ยนอะไร"},
    {"Windows would not create a restore point. Turn System Protection on for C: and try again.",
     "Windows สร้างจุดคืนค่าไม่ได้ เปิด System Protection ของไดรฟ์ C: แล้วลองใหม่"},
    {"Windows refused the change", "Windows ไม่ยอมให้เปลี่ยน"},
    {"Not wired to a backend yet", "ยังไม่ได้ต่อ backend"},

    // ── Tweak pages ─────────────────────────────────────────────────────────
    {"Everything you can change, and what it is set to.",
     "ทุกอย่างที่ปรับได้ และตอนนี้ตั้งไว้เป็นอะไร"},
    {"This tab", "แท็บนี้"},
    {"What is already applied here", "ในแท็บนี้ใช้งานไปแล้วเท่าไหร่"},
    {"Every setting here reports its state", "ทุกตัวในแท็บนี้อ่านสถานะกลับได้"},
    {"Apply", "ใช้งาน"},
    {"Open", "เปิด"},
    {"Applied", "ใช้งานแล้ว"},
    {"Not applied", "ยังไม่ได้ใช้"},
    {"Gaming", "เกม"},
    {"ACTIVE PLAN", "แผนที่ใช้อยู่"},
    {"Renames the active Windows scheme so it's easy to spot",
     "เปลี่ยนชื่อแผนพลังงานของ Windows ให้หาเจอง่าย"},
    {"Renamed", "เปลี่ยนชื่อแล้ว"},
    {"Failed", "ไม่สำเร็จ"},

    // ── FiveM ───────────────────────────────────────────────────────────
    {"GTA 5 / FiveM In-Game Settings", "ตั้งค่ากราฟิกในเกม GTA 5 / FiveM"},
    {"Apply In-Game Settings", "ใช้ค่าในเกม"},
    {"Apply CitizenFX.ini", "ใช้ CitizenFX.ini"},
    {"Applying", "กำลังใช้..."},
    {"Re-apply", "ใช้ซ้ำ"},
    {"Written, with a .numbanine.bak of the original beside it. Restart FiveM to pick it up.",
     "เขียนแล้ว เก็บไฟล์เดิมเป็น .numbanine.bak ไว้ข้างๆ เปิด FiveM ใหม่เพื่อให้มีผล"},
    {"Could not be written - see the activity log.", "เขียนไม่สำเร็จ ดูที่ activity log"},

    // ── Sign in ─────────────────────────────────────────────────────────────
    {"Sign in to numbanine", "เข้าสู่ระบบ numbanine"},
    {"Enter the licence key from your purchase email. It binds to this machine the first time "
     "you use it.",
     "กรอกรหัสไลเซนส์จากอีเมลที่ซื้อ รหัสจะผูกกับเครื่องนี้ตั้งแต่ครั้งแรกที่ใช้"},
    {"Licence key", "รหัสไลเซนส์"},
    {"Enter your licence key.", "กรอกรหัสไลเซนส์ของคุณ"},
    {"Checking key", "กำลังตรวจสอบรหัส"},
    {"Checking your key...", "กำลังตรวจสอบรหัส..."},
    {"Unlock numbanine", "ปลดล็อก numbanine"},
    {"Unlocked", "ปลดล็อกแล้ว"},
    {"Try again", "ลองใหม่"},
    {"You accept the ", "คุณยอมรับ"},
    {" and ", " และ "},
    {"Terms", "ข้อกำหนด"},
    {"Privacy", "ความเป็นส่วนตัว"},

    // ── Sign-in stage ───────────────────────────────────────────────────────
    {"Read the machine before changing it.", "อ่านค่าเครื่องก่อนแก้"},
    {"Every tweak on the dashboard is one numbanine can verify against the registry, so the score "
     "is a measurement rather than a promise.",
     "ทุกอย่างบนแดชบอร์ดคือสิ่งที่ numbanine อ่านกลับจากรีจิสทรีได้ คะแนนจึงเป็นค่าที่วัดได้จริง ไม่ใช่คำโฆษณา"},
    {"A restore point before anything moves.", "สร้างจุดคืนค่าก่อนแตะอะไรทั้งนั้น"},
    {"Optimize now takes one first, and stops without touching a single key if Windows refuses.",
     "ปุ่มปรับแต่งเลยจะสร้างให้ก่อน ถ้า Windows ไม่ยอม จะหยุดทันทีโดยไม่แก้อะไรสักตัว"},
    {"Timer resolution, core parking, packet batching.",
     "Timer resolution, core parking, packet batching"},
    {"The settings that decide frame pacing, grouped by what they change instead of where they "
     "live in the registry.",
     "ค่าที่กำหนดความสม่ำเสมอของเฟรม จัดกลุ่มตามสิ่งที่มันเปลี่ยน ไม่ใช่ตามที่อยู่ในรีจิสทรี"},
    {"Built for FiveM, useful on any Windows box.", "ทำมาเพื่อ FiveM แต่ใช้กับ Windows เครื่องไหนก็ได้"},
    {"QoS marking and process priority target the game; the rest is plain Windows tuning.",
     "QoS marking กับ process priority เจาะจงที่ตัวเกม ที่เหลือคือการปรับ Windows ทั่วไป"},

    // ── Sign-in results ─────────────────────────────────────────────────────
    // Published in English by the licence worker and translated here when
    // drawn - see license_form.cpp. Anything the server sends that is not
    // listed stays in the words the server used.
    {"Licence valid.", "รหัสถูกต้อง"},
    {"That key was not recognised. Check for a typo, or paste it again from your purchase email.",
     "ไม่รู้จักรหัสนี้ ดูว่าพิมพ์ผิดหรือเปล่า หรือคัดลอกจากอีเมลที่ซื้อมาใหม่"},
    {"This key is locked to a different machine. Send the hardware ID shown below to support for "
     "a reset.",
     "รหัสนี้ผูกกับเครื่องอื่นอยู่ ส่ง hardware ID ข้างล่างให้ซัพพอร์ตเพื่อรีเซ็ต"},
    {"This key has expired. Renew it to sign in again.", "รหัสนี้หมดอายุแล้ว ต่ออายุเพื่อเข้าใช้อีกครั้ง"},
    {"This key has been banned. Contact support if you think that is a mistake.",
     "รหัสนี้ถูกแบน ติดต่อซัพพอร์ตถ้าคิดว่าผิดพลาด"},
    {"This key has been revoked. Contact support if you think that is a mistake.",
     "รหัสนี้ถูกยกเลิก ติดต่อซัพพอร์ตถ้าคิดว่าผิดพลาด"},
    {"Too many attempts. Wait a moment, then try again.", "ลองบ่อยเกินไป รอสักครู่แล้วลองใหม่"},
    {"The app sent a request the server rejected. Update to the latest version.",
     "แอปส่งคำขอที่เซิร์ฟเวอร์ไม่รับ อัปเดตเป็นเวอร์ชันล่าสุด"},
    {"The licence server refused the key. Try again in a moment.",
     "เซิร์ฟเวอร์ไลเซนส์ไม่รับรหัสนี้ รอสักครู่แล้วลองใหม่"},
};

const std::unordered_map<std::string_view, const char*>& table()
{
    static const std::unordered_map<std::string_view, const char*> map = []
    {
        std::unordered_map<std::string_view, const char*> m;
        m.reserve(sizeof(k_table) / sizeof(k_table[0]));
        for (const entry& e : k_table)
            m.emplace(e.en, e.th);
        return m;
    }();
    return map;
}
} // namespace

lang language()
{
    return g_language;
}

void set_language(lang value)
{
    g_language = value;
}

void toggle_language()
{
    g_language = g_language == lang::en ? lang::th : lang::en;
}

const char* tr(const char* english)
{
    if (g_language == lang::en || english == nullptr)
        return english;

    const auto& map = table();
    const auto found = map.find(std::string_view(english));
    return found == map.end() ? english : found->second;
}
} // namespace szk::i18n
