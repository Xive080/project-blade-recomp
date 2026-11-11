// Herramienta de QA (no es parte del port): ejecucion reproducible. Requiere PS2X_DETERMINISTIC=1 (plazos por ciclos del EE, sin
// ritmo de reloj de pared) y el gancho ps2xQaVSyncHook del planificador, que corre en el hilo del EE al empezar cada VBlank.
//   BLADE_DET_INPUT="300:START;420+20:CROSS"   igual que BLADE_INPUT pero exacto: el estado del mando solo cambia en un VBlank
//   BLADE_DET_DUMP=carpeta,cada_N[,desde]      PNG del fotograma presentado en ese VBlank (con hash)
//   BLADE_DET_HASH=cada_N[,desde]              hash FNV-1a de la RAM del EE (32 MB) y del fotograma
//   BLADE_DET_EXIT=N                           termina en el VBlank N
#include "ps2_runtime.h"
#include "raylib.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

extern void (*ps2xQaVSyncHook)(PS2Runtime &, uint64_t);

unsigned long long gBladeQaTick = 0; // VBlank actual, para trazas de depuracion de otros ficheros del runner

namespace ps2_stubs
{
    void setPadOverrideState(uint16_t buttons, uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry);
    void clearPadOverrideState();
}

namespace
{
    struct Step { uint64_t start, end; uint16_t mask; int lx, ly; };
    std::vector<Step> gSteps;
    std::filesystem::path gDumpDir;
    uint64_t gDumpEvery = 0, gDumpFrom = 0, gHashEvery = 0, gHashFrom = 0, gExit = 0;
    bool gPadOn = false;

    uint16_t bit(const std::string &n)
    {
        static const struct { const char *n; uint16_t b; } t[] = {
            {"SELECT", 1u << 0}, {"L3", 1u << 1}, {"R3", 1u << 2}, {"START", 1u << 3}, {"UP", 1u << 4}, {"RIGHT", 1u << 5},
            {"DOWN", 1u << 6}, {"LEFT", 1u << 7}, {"L2", 1u << 8}, {"R2", 1u << 9}, {"L1", 1u << 10}, {"R1", 1u << 11},
            {"TRIANGLE", 1u << 12}, {"CIRCLE", 1u << 13}, {"CROSS", 1u << 14}, {"X", 1u << 14}, {"SQUARE", 1u << 15}};
        for (const auto &e : t) if (n == e.n) return e.b;
        return 0;
    }

    uint64_t fnv(const uint8_t *p, size_t n)
    {
        uint64_t h = 1469598103934665603ull;
        for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ull;
        return h;
    }

    void parseSteps(const std::string &text)
    {
        std::stringstream ss(text); std::string item;
        while (std::getline(ss, item, ';'))
        {
            const size_t c = item.find(':'); if (c == std::string::npos) continue;
            Step s{}; s.lx = s.ly = -1;
            const std::string when = item.substr(0, c); const size_t plus = when.find('+');
            s.start = std::strtoull(when.c_str(), nullptr, 10);
            s.end = s.start + (plus == std::string::npos ? 6u : std::max<uint64_t>(1u, std::strtoull(when.c_str() + plus + 1, nullptr, 10)));
            std::stringstream bs(item.substr(c + 1)); std::string b;
            while (std::getline(bs, b, ','))
            {
                for (char &ch : b) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                if (b.rfind("LX=", 0) == 0) s.lx = std::atoi(b.c_str() + 3);
                else if (b.rfind("LY=", 0) == 0) s.ly = std::atoi(b.c_str() + 3);
                else s.mask |= bit(b);
            }
            gSteps.push_back(s);
        }
    }

    void hook(PS2Runtime &rt, uint64_t tick)
    {
        gBladeQaTick = tick;
        uint16_t mask = 0; uint8_t lx = 0x80, ly = 0x80; bool any = false;
        for (const Step &s : gSteps)
            if (tick >= s.start && tick < s.end)
            {
                any = true; mask |= s.mask;
                if (s.lx >= 0) lx = static_cast<uint8_t>(s.lx);
                if (s.ly >= 0) ly = static_cast<uint8_t>(s.ly);
            }
        if (any) { ps2_stubs::setPadOverrideState(static_cast<uint16_t>(~mask), lx, ly, 0x80, 0x80); gPadOn = true; }
        else if (gPadOn) { ps2_stubs::clearPadOverrideState(); gPadOn = false; }

        const bool doHash = gHashEvery && tick >= gHashFrom && (tick - gHashFrom) % gHashEvery == 0;
        const bool doDump = gDumpEvery && tick >= gDumpFrom && (tick - gDumpFrom) % gDumpEvery == 0;
        if (doHash || doDump)
        {
            std::vector<uint8_t> px; uint32_t w = 0, h = 0;
            rt.gs().latchHostPresentationFrame();
            const bool ok = rt.gs().copyLatchedHostPresentationFrame(px, w, h, nullptr, nullptr, nullptr) && w && h &&
                            px.size() >= static_cast<size_t>(w) * h * 4u;
            if (ok) for (size_t i = 3; i < px.size(); i += 4) px[i] = 0xFF;
            const uint64_t fh = ok ? fnv(px.data(), static_cast<size_t>(w) * h * 4u) : 0;
            if (doHash)
                std::printf("[det] tick %llu ram=%016llx frame=%016llx\n", (unsigned long long)tick,
                            (unsigned long long)fnv(rt.memory().getRDRAM(), PS2_RAM_SIZE), (unsigned long long)fh);
            if (doDump && ok)
            {
                Image img{}; img.data = px.data(); img.width = int(w); img.height = int(h); img.mipmaps = 1;
                img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
                char name[64]; std::snprintf(name, sizeof(name), "frame_%06llu.png", (unsigned long long)tick);
                ExportImage(img, (gDumpDir / name).string().c_str());
                if (!doHash) std::printf("[det] tick %llu frame=%016llx\n", (unsigned long long)tick, (unsigned long long)fh);
            }
            std::fflush(stdout);
        }
        if (gExit && tick >= gExit) { std::fflush(stdout); std::_Exit(0); }
    }

    struct Init
    {
        Init()
        {
            const char *in = std::getenv("BLADE_DET_INPUT"), *du = std::getenv("BLADE_DET_DUMP"),
                       *ha = std::getenv("BLADE_DET_HASH"), *ex = std::getenv("BLADE_DET_EXIT");
            if (!(in && *in) && !(du && *du) && !(ha && *ha) && !(ex && *ex)) return;
            if (in && *in) parseSteps(in);
            if (du && *du)
            {
                std::string v = du; size_t a = v.find(',');
                gDumpDir = v.substr(0, a); gDumpEvery = a == std::string::npos ? 50 : std::strtoull(v.c_str() + a + 1, nullptr, 10);
                if (a != std::string::npos) { size_t b = v.find(',', a + 1); if (b != std::string::npos) gDumpFrom = std::strtoull(v.c_str() + b + 1, nullptr, 10); }
                std::filesystem::create_directories(gDumpDir);
            }
            if (ha && *ha)
            {
                std::string v = ha; size_t a = v.find(',');
                gHashEvery = std::strtoull(v.c_str(), nullptr, 10); if (a != std::string::npos) gHashFrom = std::strtoull(v.c_str() + a + 1, nullptr, 10);
            }
            if (ex && *ex) gExit = std::strtoull(ex, nullptr, 10);
            ps2xQaVSyncHook = hook;
        }
    } gInit;
}
