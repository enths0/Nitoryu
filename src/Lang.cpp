#include "kit/input.h"

#include "kit/log.h"

#include <cctype>
#include <cstdio>
#include <map>
#include <string>

#include "Lang.h"
#include "Stance.h"
#include "kit/diag.h"

namespace
{

    struct Default
    {
        const char* key;
        const char* text;
    };
    const Default DEFAULTS[] = {
        { "speech.two_blades", "Two blades." },
        { "speech.one_blade", "One blade." },
        { "speech.any_weapon", "Any weapon." },
        { "speech.blades_only", "Blades only." },
        { "tip.stance_button", "Two blades\nOn: the character fights with both carried weapons.\nOff: with one, as in vanilla. Saved with the character." },
        { "tip.any_weapon_button", "Any weapon\nLets this character pair blunt weapons and hackers.\nThey have no moves of their own. Saved with the character.\nOther weapon types are not supported:\nheavy weapons, polearms and bows are two-handed." },
        { "speech.pressured", "Can't find an opening!" },
        { "speech.flow", "Flow." },
        { "speech.mugen", "Mugen." },
        { "stats.ranks_title", "Ranks:" },
        { "stats.rank1_opens", "second-blade strikes" },
        { "stats.rank2_opens", "second-blade parries" },
        { "stats.rank3_opens", "twin strikes, ripostes, Cross, Dash, pressure" },
        { "stats.rank4_opens", "flow, the spin" },
        { "stats.rank5_opens", "Mugen" },
        { "stats.rank_needs", "Path {dp}, Dual Wielding {amb}" },
        { "stats.rank_needs_dp", "Path {dp}" },
        { "stats.rank_up", "{who} has reached the rank of {rank}." },
        { "stats.label", "Dual Wielding" },
        { "stats.affects_title", "Stats Affected:" },
        { "stats.affects", "-Dual wielding, and fighting left-handed with a crippled right arm" },
        { "stats.train_title", "Ways To Train:" },
        { "stats.train", "-Fighting with two weapons, or left-handed" },
        { "stats.description_title", "Description:" },
        { "stats.description", "Skill at fighting with a weapon in each hand. It is trained only by fighting with two weapons or left-handed. It learns faster while it is below your weapon skill, and slower once it is above it." },
        { "stats.next_level", "Next Level:" },
        { "stats.in_fight", "In a fight now:" },
        { "stats.overloaded", "-Overloaded" },
        { "stats.heavy_armour", "-Heavy armour" },
        { "stats.proficiency", "Path:" },
        { "stats.tier", "rank" },
        { "stats.tier_capped", "{amb} Dual Wielding for {next}" },
        { "stats.rank1", "Deshi" },
        { "stats.rank2", "Bushi" },
        { "stats.rank3", "Ronin" },
        { "stats.rank4", "Kenshi" },
        { "stats.rank5", "Kensei" },
        { "stats.speed", "Dual speed bonus:" },
        { "stats.dex_bonus", "Dexterity in a fight:" },
        { "dummy.group", "Pair Training Dummy" },
        { "dummy.name1", "Pair Training Dummy mkI" },
        { "dummy.name2", "Pair Training Dummy mkII" },
        { "dummy.name3", "Pair Training Dummy mkIII" },
        { "dummy.desc1", "A training dummy for fighting with two blades. Trains Dual Wielding (Nitoryu) up to level 10. The character needs two one-handed blades in the weapon slots; they can stay sheathed." },
        { "dummy.desc2", "A training dummy for fighting with two blades. Trains Dual Wielding (Nitoryu) up to level 20. The character needs two one-handed blades in the weapon slots; they can stay sheathed." },
        { "dummy.desc3", "A training dummy for fighting with two blades. Trains Dual Wielding (Nitoryu) up to level 30. The character needs two one-handed blades in the weapon slots; they can stay sheathed." },
        { "dummy.research1", "Pair Training Dummies" },
        { "dummy.research2", "Better Pair Training Dummies" },
        { "dummy.research3", "Advanced Pair Training Dummies" },
        { "dummy.research_desc1", "A training dummy for fighting with two blades. It trains Dual Wielding (Nitoryu) up to level 10." },
        { "dummy.research_desc2", "A better dummy for fighting with two blades. It trains Dual Wielding (Nitoryu) up to level 20." },
        { "dummy.research_desc3", "The best dummy for fighting with two blades. It trains Dual Wielding (Nitoryu) up to level 30." },
        { "dummy.research_category", "Training" },
    };

    std::map<std::string, std::string> g_text;
    std::string g_code = "en";

    std::string dirOf(const std::string& p)
    {
        size_t slash = p.find_last_of("\\/");
        return slash == std::string::npos ? std::string(".") : p.substr(0, slash);
    }

    std::string trim(const std::string& s)
    {
        size_t a = 0, b = s.size();
        while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r'))
            ++a;
        while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r'))
            --b;
        return s.substr(a, b - a);
    }

    std::string unescape(const std::string& s)
    {
        std::string r;
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n')
            {
                r += '\n';
                ++i;
            }
            else
                r += s[i];
        }
        return r;
    }

    bool readFile(const std::string& path, std::string& out)
    {
        FILE* f = NULL;
        if (fopen_s(&f, path.c_str(), "rb") != 0 || f == NULL)
            return false;
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
            out.append(buf, n);
        fclose(f);
        if (out.size() >= 3 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
            out.erase(0, 3);
        return true;
    }

    int load(const std::string& path)
    {
        std::string data;
        if (!readFile(path, data))
            return -1;
        int n = 0;
        size_t pos = 0;
        while (pos < data.size())
        {
            size_t end = data.find('\n', pos);
            std::string line = trim(data.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
            pos = end == std::string::npos ? data.size() : end + 1;
            if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[')
                continue;
            size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            std::string key = trim(line.substr(0, eq));
            std::string value = unescape(trim(line.substr(eq + 1)));
            if (key.empty() || value.empty())
                continue;
            g_text[key] = value;
            ++n;
        }
        return n;
    }

    std::string exeDir()
    {
        char path[MAX_PATH] = { 0 };
        GetModuleFileNameA(NULL, path, MAX_PATH);
        return dirOf(std::string(path));
    }

    std::string gameLanguage(const std::string& kenshiDir, std::string& used)
    {
        char cwd[MAX_PATH] = { 0 };
        GetCurrentDirectoryA(MAX_PATH, cwd);
        std::string dirs[4] = { std::string(cwd), exeDir(), dirOf(exeDir()), kenshiDir };
        std::string data;
        used = "none";
        for (int i = 0; i < 4 && used == "none"; ++i)
            if (!dirs[i].empty() && readFile(dirs[i] + "\\settings.cfg", data))
                used = dirs[i] + "\\settings.cfg";
        if (used == "none")
            return "en";
        size_t at = data.find("\nlanguage=");
        size_t start = at != std::string::npos ? at + 10 : (data.compare(0, 9, "language=") == 0 ? 9 : std::string::npos);
        if (start == std::string::npos)
            return "en";
        std::string code;
        for (size_t i = start; i < data.size() && (isalpha((unsigned char)data[i]) != 0); ++i)
            code += (char)tolower((unsigned char)data[i]);
        return code.empty() ? std::string("en") : code;
    }
}

namespace lang
{
    void init()
    {
        for (size_t i = 0; i < sizeof(DEFAULTS) / sizeof(DEFAULTS[0]); ++i)
            g_text[DEFAULTS[i].key] = DEFAULTS[i].text;
        std::string ini = stance::iniFile();
        std::string modDir = dirOf(ini);
        std::string kenshiDir = dirOf(dirOf(modDir));
        char want[32] = { 0 };
        GetPrivateProfileStringA("General", "language", "auto", want, sizeof(want), ini.c_str());
        std::string code = want;
        for (size_t i = 0; i < code.size(); ++i)
            code[i] = (char)tolower((unsigned char)code[i]);
        std::string cfg = "not read";
        if (code.empty() || code == "auto")
            code = gameLanguage(kenshiDir, cfg);
        int en = load(modDir + "\\lang\\en.ini");
        int own = code == "en" ? en : load(modDir + "\\lang\\" + code + ".ini");
        g_code = own >= 0 ? code : std::string("en");
        char b[160];
        sprintf_s(b, "language %s (asked %s), %d texts from %s.ini, en.ini %s", g_code.c_str(), want, own < 0 ? 0 : own,
                  code.c_str(), en >= 0 ? "found" : "MISSING (built-in English used)");
        kit::Diag("lang").kv("text", b).kv("settings", cfg).emit();
    }

    const std::string& text(const char* key)
    {
        std::map<std::string, std::string>::iterator it = g_text.find(key);
        if (it != g_text.end())
            return it->second;

        static const std::string missing("?");
        return missing;
    }

    const std::string& code() { return g_code; }
}
