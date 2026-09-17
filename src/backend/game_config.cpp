#include "backend/game_config.h"

#include "backend/activity_log.h"
#include "core/product_info.h"

#include <windows.h>

#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace szk::backend
{
namespace
{
// ── Small file helpers ──────────────────────────────────────────────────────

bool read_file(const std::filesystem::path& path, std::string& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;

    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

bool write_file(const std::filesystem::path& path, const std::string& text)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;

    out.write(text.data(), (std::streamsize)text.size());
    return out.good();
}

// One backup per file, taken the first time it is touched. Repeating it on
// every apply would overwrite the only copy of the original with a copy of our
// own output the second time round.
void backup_once(const std::filesystem::path& path)
{
    std::filesystem::path backup = path;
    backup += std::wstring(L".") + product_info::name_wide + L".bak";

    std::error_code ec;
    if (std::filesystem::exists(backup, ec))
        return;

    std::filesystem::copy_file(path, backup, ec);
}

std::vector<std::string> split_lines(const std::string& text)
{
    std::vector<std::string> lines;
    size_t start = 0;

    while (start <= text.size())
    {
        const size_t nl = text.find('\n', start);
        if (nl == std::string::npos)
        {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, nl - start));
        start = nl + 1;
    }

    return lines;
}

std::string trim(const std::string& s)
{
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos)
        return std::string();
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool iequals(const std::string& a, const std::string& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
            return false;
    return true;
}

std::wstring env(const wchar_t* name)
{
    wchar_t buffer[MAX_PATH] = {};
    const DWORD n = ::GetEnvironmentVariableW(name, buffer, MAX_PATH);
    return (n == 0 || n >= MAX_PATH) ? std::wstring() : std::wstring(buffer);
}

// ── CitizenFX.ini ───────────────────────────────────────────────────────────

struct ini_entry
{
    const char* section;
    const char* key;
    const char* value;
};

// Everything the supplied FiveM profile sets that is a property of the tuning
// rather than of the machine it came from. Deliberately absent: IVPath (that
// profile's GTA V lives on another drive), SavedBuildNumber and DefaultBuild
// (pinning a build the installed FiveM may not have), UpdateChannel (a choice
// about betas, not a performance setting), PoolSizesIncrease (sized against
// that machine's mods) and the [Addons] ReShade acknowledgement (a record that
// a specific user dismissed a specific warning).
constexpr ini_entry k_citizenfx[] = {
    {"Game", "DisableLauncher", "true"},
    {"Renderer", "DisableShadowOptimizations", "false"},
    {"Renderer", "EnablePresentationOptimizations", "true"},
    {"Renderer", "ForceRenderAheadLimit", "1"},
    {"Renderer", "DisableNvLowLatency", "false"},
    {"Renderer", "SwapChainUseWaitableSwapChain", "true"},
    {"Streaming", "MaxStreamingRequests", "50"},
    {"Streaming", "MaxStreamingMemory", "2000"},
    {"Streaming", "StreamerMode", "0"},
};

// The section header a line opens, or empty if the line is not one.
std::string section_of(const std::string& line)
{
    const std::string t = trim(line);
    if (t.size() < 2 || t.front() != '[' || t.back() != ']')
        return std::string();
    return t.substr(1, t.size() - 2);
}

// The key a "key=value" line sets, or empty. Comments do not count, so a
// commented-out key is left alone and the real one is appended.
std::string key_of(const std::string& line)
{
    const std::string t = trim(line);
    if (t.empty() || t[0] == ';' || t[0] == '#' || t[0] == '[')
        return std::string();

    const size_t eq = t.find('=');
    if (eq == std::string::npos)
        return std::string();

    return trim(t.substr(0, eq));
}

std::string value_of(const std::string& line)
{
    const std::string t = trim(line);
    const size_t eq = t.find('=');
    return eq == std::string::npos ? std::string() : trim(t.substr(eq + 1));
}

// Set one key in one section, adding the section if it is missing and adding
// the key at the end of the section if only that is missing. Returns false when
// the file already said this, so the caller can tell a no-op from a change.
bool ini_set(std::vector<std::string>& lines, const ini_entry& entry)
{
    int section_begin = -1; // first line inside the section
    int section_end = -1;   // one past its last line

    for (size_t i = 0; i < lines.size(); i++)
    {
        const std::string section = section_of(lines[i]);
        if (section.empty())
            continue;

        if (section_begin >= 0)
        {
            section_end = (int)i;
            break;
        }
        if (iequals(section, entry.section))
            section_begin = (int)i + 1;
    }

    if (section_begin < 0)
    {
        if (!lines.empty() && !trim(lines.back()).empty())
            lines.push_back(std::string());
        lines.push_back(std::string("[") + entry.section + "]");
        lines.push_back(std::string(entry.key) + "=" + entry.value);
        return true;
    }

    if (section_end < 0)
        section_end = (int)lines.size();

    for (int i = section_begin; i < section_end; i++)
    {
        if (!iequals(key_of(lines[i]), entry.key))
            continue;

        if (value_of(lines[i]) == entry.value)
            return false;

        lines[i] = std::string(entry.key) + "=" + entry.value;
        return true;
    }

    // Past the section's trailing blank lines, so the key joins the block
    // rather than being separated from it.
    int at = section_end;
    while (at > section_begin && trim(lines[at - 1]).empty())
        at--;

    lines.insert(lines.begin() + at, std::string(entry.key) + "=" + entry.value);
    return true;
}

bool ini_reads(const std::vector<std::string>& lines, const ini_entry& entry)
{
    std::string current;

    for (const std::string& line : lines)
    {
        const std::string section = section_of(line);
        if (!section.empty())
        {
            current = section;
            continue;
        }
        if (iequals(current, entry.section) && iequals(key_of(line), entry.key))
            return value_of(line) == entry.value;
    }

    return false;
}

// ── settings.xml ────────────────────────────────────────────────────────────

struct xml_entry
{
    const char* parent; // the block the tag belongs in, for insertion
    const char* tag;
    const char* value;
};

// The supplied GTA V profile's graphics block, which is what makes it a
// performance preset. <video> is not here on purpose: resolution, refresh rate,
// adapter index and the card description describe the monitor and GPU in front
// of the user, not the tuning, and copying another machine's would leave the
// game trying to run at a mode this one may not have.
constexpr xml_entry k_gta5[] = {
    {"graphics", "Tessellation", "0"},
    {"graphics", "LodScale", "0.000000"},
    {"graphics", "PedLodBias", "0.200000"},
    {"graphics", "VehicleLodBias", "0.000000"},
    {"graphics", "ShadowQuality", "0"},
    {"graphics", "ReflectionQuality", "0"},
    {"graphics", "ReflectionMSAA", "8"},
    {"graphics", "SSAO", "0"},
    {"graphics", "AnisotropicFiltering", "16"},
    {"graphics", "MSAA", "0"},
    {"graphics", "MSAAFragments", "0"},
    {"graphics", "MSAAQuality", "0"},
    {"graphics", "SamplingMode", "0"},
    {"graphics", "TextureQuality", "2"},
    {"graphics", "ParticleQuality", "0"},
    {"graphics", "WaterQuality", "0"},
    {"graphics", "GrassQuality", "0"},
    {"graphics", "ShaderQuality", "0"},
    {"graphics", "Shadow_SoftShadows", "1"},
    {"graphics", "UltraShadows_Enabled", "false"},
    {"graphics", "Shadow_ParticleShadows", "true"},
    {"graphics", "Shadow_Distance", "1.000000"},
    {"graphics", "Shadow_LongShadows", "false"},
    {"graphics", "Shadow_SplitZStart", "0.930000"},
    {"graphics", "Shadow_SplitZEnd", "0.890000"},
    {"graphics", "Shadow_aircraftExpWeight", "0.990000"},
    {"graphics", "Shadow_DisableScreenSizeCheck", "false"},
    {"graphics", "Reflection_MipBlur", "true"},
    {"graphics", "FXAA_Enabled", "false"},
    {"graphics", "TXAA_Enabled", "false"},
    {"graphics", "Lighting_FogVolumes", "true"},
    {"graphics", "Shader_SSA", "false"},
    {"graphics", "DX_Version", "2"},
    {"graphics", "CityDensity", "0.000000"},
    {"graphics", "PedVarietyMultiplier", "0.000000"},
    {"graphics", "VehicleVarietyMultiplier", "0.000000"},
    {"graphics", "PostFX", "0"},
    {"graphics", "DoF", "false"},
    {"graphics", "HdStreamingInFlight", "false"},
    {"graphics", "MaxLodScale", "0.000000"},
    {"graphics", "MotionBlurStrength", "0.000000"},
    {"system", "maxSizeOfStreamingReplay", "0"},
    {"system", "maxFileStoreSize", "0"},
    {"audio", "Audio3d", "false"},
};

// The few settings whose being off is the whole point of the preset. check()
// asks about these rather than all forty, so a user who deliberately raised, say,
// texture quality afterwards does not see the row flip back to "not applied".
constexpr const char* k_gta5_signature[] = {
    "ShadowQuality", "MSAA", "PostFX", "GrassQuality", "ParticleQuality", "SSAO",
};

// Find `<Tag value="` in the document and return the span of what follows up to
// the closing quote. npos when the tag is absent.
bool xml_find_value(const std::string& doc, const char* tag, size_t& begin, size_t& end)
{
    const std::string needle = std::string("<") + tag + " value=\"";
    const size_t at = doc.find(needle);
    if (at == std::string::npos)
        return false;

    begin = at + needle.size();
    end = doc.find('"', begin);
    return end != std::string::npos;
}

bool xml_set(std::string& doc, const xml_entry& entry)
{
    size_t begin = 0, end = 0;
    if (xml_find_value(doc, entry.tag, begin, end))
    {
        if (doc.compare(begin, end - begin, entry.value) == 0)
            return false;

        doc.replace(begin, end - begin, entry.value);
        return true;
    }

    // The whitespace in front of a closing tag, which is the indent of the
    // block it closes.
    auto indent_before = [&doc](size_t at)
    {
        size_t line_start = doc.rfind('\n', at);
        line_start = (line_start == std::string::npos) ? 0 : line_start + 1;
        return doc.substr(line_start, at - line_start);
    };

    // The tag is missing, so add it just before its block closes. The insertion
    // point already sits after the closing tag's own indent, so only the two
    // extra spaces go in front of the new tag and the closing tag's indent is
    // written back after it. Emitting the full indent here instead would add it
    // twice, and since each tag is measured against the line the last one left
    // behind, forty tags would step forty times off the page.
    const std::string close = std::string("</") + entry.parent + ">";
    const size_t at = doc.find(close);
    if (at != std::string::npos)
    {
        const std::string block = indent_before(at);
        doc.insert(at, std::string("  <") + entry.tag + " value=\"" + entry.value + "\" />\n" +
                           block);
        return true;
    }

    // The block is missing too - a settings.xml that has never had, say, an
    // <audio> section. Add the block with this tag in it rather than dropping
    // the entry.
    const size_t root = doc.find("</Settings>");
    if (root == std::string::npos)
        return false;

    const std::string root_indent = indent_before(root);
    const std::string inner = root_indent + "  ";
    doc.insert(root, std::string("  <") + entry.parent + ">\n" + inner + "  <" + entry.tag +
                         " value=\"" + entry.value + "\" />\n" + inner + "</" + entry.parent +
                         ">\n" + root_indent);
    return true;
}

bool xml_reads(const std::string& doc, const char* tag, const char* value)
{
    size_t begin = 0, end = 0;
    if (!xml_find_value(doc, tag, begin, end))
        return false;
    return doc.compare(begin, end - begin, value) == 0;
}

const char* gta5_value_for(const char* tag)
{
    for (const xml_entry& entry : k_gta5)
        if (std::strcmp(entry.tag, tag) == 0)
            return entry.value;
    return nullptr;
}

// A settings.xml with only the tuned values in it, for the case where the game
// has never written one. Everything not named here GTA V fills in on first run
// - including the whole <video> block, which is exactly the part that must come
// from this machine rather than from the preset.
std::string gta5_fresh_document()
{
    std::string doc = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\n<Settings>\n"
                      "  <version value=\"27\" />\n"
                      "  <configSource>SMC_AUTO</configSource>\n"
                      "  <graphics>\n";

    for (const xml_entry& entry : k_gta5)
        if (std::strcmp(entry.parent, "graphics") == 0)
            doc += std::string("    <") + entry.tag + " value=\"" + entry.value + "\" />\n";

    doc += "  </graphics>\n  <system>\n";
    for (const xml_entry& entry : k_gta5)
        if (std::strcmp(entry.parent, "system") == 0)
            doc += std::string("    <") + entry.tag + " value=\"" + entry.value + "\" />\n";

    doc += "  </system>\n  <audio>\n";
    for (const xml_entry& entry : k_gta5)
        if (std::strcmp(entry.parent, "audio") == 0)
            doc += std::string("    <") + entry.tag + " value=\"" + entry.value + "\" />\n";

    doc += "  </audio>\n</Settings>\n";
    return doc;
}
} // namespace

// ── Paths ───────────────────────────────────────────────────────────────────

std::wstring fivem_citizenfx_path()
{
    const std::wstring local = env(L"LOCALAPPDATA");
    if (local.empty())
        return std::wstring();
    return local + L"\\FiveM\\FiveM.app\\CitizenFX.ini";
}

std::wstring gta5_settings_path()
{
    const std::wstring local = env(L"LOCALAPPDATA");
    const std::wstring profile = env(L"USERPROFILE");

    std::vector<std::wstring> candidates;

    // FiveM redirects the game's Documents folder into its own data directory,
    // so under FiveM this is the file the game actually reads. The suffix is
    // versioned per launcher build, so the directory is searched rather than
    // spelled out.
    if (!local.empty())
    {
        const std::filesystem::path storage = local + L"\\FiveM\\FiveM.app\\data\\game-storage";
        std::error_code ec;
        for (const std::filesystem::directory_entry& dir :
             std::filesystem::directory_iterator(storage, ec))
        {
            if (!dir.is_directory(ec))
                continue;
            const std::wstring name = dir.path().filename().wstring();
            if (name.rfind(L"ros_documents", 0) != 0)
                continue;
            candidates.push_back((dir.path() / L"Rockstar Games" / L"GTA V" / L"settings.xml")
                                     .wstring());
        }
    }

    if (!profile.empty())
        candidates.push_back(profile + L"\\Documents\\Rockstar Games\\GTA V\\settings.xml");

    for (const std::wstring& candidate : candidates)
    {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec))
            return candidate;
    }

    // Nothing written yet: the standard location is where the game will look.
    return profile.empty() ? std::wstring()
                           : profile + L"\\Documents\\Rockstar Games\\GTA V\\settings.xml";
}

// ── CitizenFX.ini ───────────────────────────────────────────────────────────

bool apply_fivem_citizenfx_config()
{
    const std::wstring path = fivem_citizenfx_path();
    if (path.empty())
    {
        log("FiveM launcher config", "Could not resolve %LOCALAPPDATA%.");
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
    {
        log("FiveM launcher config",
            "CitizenFX.ini not found - FiveM writes it on first launch. Run FiveM once, then "
            "apply this again.");
        return false;
    }

    std::string text;
    if (!read_file(path, text))
    {
        log("FiveM launcher config", "CitizenFX.ini could not be read.");
        return false;
    }

    backup_once(path);

    std::vector<std::string> lines = split_lines(text);
    int changed = 0;
    for (const ini_entry& entry : k_citizenfx)
        if (ini_set(lines, entry))
            changed++;

    std::string out;
    for (size_t i = 0; i < lines.size(); i++)
    {
        out += lines[i];
        if (i + 1 < lines.size())
            out += "\n";
    }

    if (!write_file(path, out))
    {
        log("FiveM launcher config", "CitizenFX.ini could not be written - is FiveM running?");
        return false;
    }

    log("FiveM launcher config",
        changed == 0 ? "Already set; nothing to change."
                     : std::to_string(changed) + " of " +
                           std::to_string((int)(sizeof(k_citizenfx) / sizeof(k_citizenfx[0]))) +
                           " keys updated. FiveM reads this at startup, so restart it.");
    return true;
}

bool check_fivem_citizenfx_config()
{
    const std::wstring path = fivem_citizenfx_path();
    if (path.empty())
        return false;

    std::string text;
    if (!read_file(path, text))
        return false;

    const std::vector<std::string> lines = split_lines(text);
    for (const ini_entry& entry : k_citizenfx)
        if (!ini_reads(lines, entry))
            return false;

    return true;
}

// ── settings.xml ────────────────────────────────────────────────────────────

bool apply_gta5_graphics_preset()
{
    const std::wstring path = gta5_settings_path();
    if (path.empty())
    {
        log("GTA V graphics preset", "Could not resolve the user profile directory.");
        return false;
    }

    std::error_code ec;
    const bool existed = std::filesystem::exists(path, ec);

    std::string doc;
    if (existed)
    {
        if (!read_file(path, doc))
        {
            log("GTA V graphics preset", "settings.xml could not be read.");
            return false;
        }
        backup_once(path);
    }
    else
    {
        doc = gta5_fresh_document();
    }

    int changed = 0;
    if (existed)
    {
        for (const xml_entry& entry : k_gta5)
            if (xml_set(doc, entry))
                changed++;
    }

    if (!write_file(path, doc))
    {
        log("GTA V graphics preset", "settings.xml could not be written - is the game running?");
        return false;
    }

    log("GTA V graphics preset",
        existed ? (changed == 0 ? "Already set; nothing to change."
                                : std::to_string(changed) +
                                      " graphics values lowered. The game reads this at startup.")
                : "No settings.xml existed, so one was created with the preset. GTA V fills in "
                  "the display mode itself on first launch.");
    return true;
}

bool check_gta5_graphics_preset()
{
    const std::wstring path = gta5_settings_path();
    if (path.empty())
        return false;

    std::string doc;
    if (!read_file(path, doc))
        return false;

    for (const char* tag : k_gta5_signature)
    {
        const char* want = gta5_value_for(tag);
        if (!want || !xml_reads(doc, tag, want))
            return false;
    }

    return true;
}
} // namespace szk::backend
