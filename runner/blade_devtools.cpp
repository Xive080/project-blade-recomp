// Herramientas de desarrollo opcionales (solo se activan con variables de entorno; sin ellas no hacen nada):
//   BLADE_DUMP_FRAMES=carpeta[,cada_N[,desde]]  guarda en PNG la imagen que el runtime presenta en la ventana, cada N
//                                        VSync (por defecto 30), a partir del VSync 'desde' (por defecto 0). Ficheros carpeta/frame_TTTTTT.png con TTTTTT = VSync.
//   BLADE_INPUT="300:START;420+20:CROSS;500:UP,CROSS"
//                                        pulsa botones del mando 1 en VSync concretos. Cada paso es TICK[+DURACION]:BOTONES
//                                        (duracion por defecto 6 VSync; botones separados por comas; prefijo "t=" opcional).
//                                        Mientras el guion tiene pasos pendientes el teclado no cuenta; al acabar se suelta.
//   BLADE_DUMP_RAM=carpeta,VSync         guarda la RAM del EE (32 MB) en carpeta/ram_VSync.bin al llegar a ese VSync.
//   BLADE_EXIT_TICK=N                    cierra el proceso al llegar al VSync N (para pruebas sin ventana atendida).
// Leen el marco ya presentado (GS::copyLatchedHostPresentationFrame, protegido por su mutex) y usan el estado forzado
// de libpad (setPadOverrideState), asi que no tocan el runtime.
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"

#include "raylib.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace ps2_stubs
{
    void setPadOverrideState(uint16_t buttons, uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry);
    void clearPadOverrideState();
}

namespace
{
    struct InputStep
    {
        uint64_t start = 0;
        uint64_t end = 0;  // exclusivo
        uint16_t mask = 0; // bits activos (1 = pulsado)
        int lx = -1, ly = -1;
    };

    uint16_t buttonBit(const std::string &name)
    {
        static const struct { const char *n; uint16_t b; } kTable[] = {
            {"SELECT", 1u << 0}, {"L3", 1u << 1}, {"R3", 1u << 2}, {"START", 1u << 3},
            {"UP", 1u << 4}, {"RIGHT", 1u << 5}, {"DOWN", 1u << 6}, {"LEFT", 1u << 7},
            {"L2", 1u << 8}, {"R2", 1u << 9}, {"L1", 1u << 10}, {"R1", 1u << 11},
            {"TRIANGLE", 1u << 12}, {"CIRCLE", 1u << 13}, {"CROSS", 1u << 14}, {"X", 1u << 14},
            {"SQUARE", 1u << 15},
        };
        for (const auto &e : kTable)
            if (name == e.n)
                return e.b;
        return 0;
    }

    std::vector<InputStep> parseInput(const std::string &text)
    {
        std::vector<InputStep> steps;
        std::stringstream ss(text);
        std::string item;
        while (std::getline(ss, item, ';'))
        {
            if (item.rfind("t=", 0) == 0)
                item = item.substr(2);
            const size_t colon = item.find(':');
            if (colon == std::string::npos)
                continue;
            InputStep step;
            const std::string when = item.substr(0, colon);
            const size_t plus = when.find('+');
            step.start = std::strtoull(when.c_str(), nullptr, 10);
            const uint64_t dur = (plus == std::string::npos) ? 6u : std::strtoull(when.c_str() + plus + 1, nullptr, 10);
            step.end = step.start + std::max<uint64_t>(1u, dur);
            std::stringstream bs(item.substr(colon + 1));
            std::string b;
            while (std::getline(bs, b, ','))
            {
                for (char &c : b)
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                // ejes del stick izquierdo: LX=0..255, LY=0..255
                if (b.rfind("LX=", 0) == 0)
                    step.lx = std::atoi(b.c_str() + 3);
                else if (b.rfind("LY=", 0) == 0)
                    step.ly = std::atoi(b.c_str() + 3);
                else if (uint16_t bit = buttonBit(b))
                    step.mask |= bit;
                else if (!b.empty())
                    std::cout << "[blade:input] boton desconocido: " << b << std::endl;
            }
            steps.push_back(step);
        }
        return steps;
    }

    void writeFrame(const std::filesystem::path &dir, uint64_t tick, PS2Runtime &rt)
    {
        std::vector<uint8_t> pixels;
        uint32_t w = 0, h = 0;
        if (!rt.gs().copyLatchedHostPresentationFrame(pixels, w, h, nullptr, nullptr, nullptr) || !w || !h ||
            pixels.size() < static_cast<size_t>(w) * h * 4u)
            return;
        for (size_t i = 3; i < pixels.size(); i += 4)
            pixels[i] = 0xFF; // el alfa del GS no es transparencia de pantalla
        Image img{};
        img.data = pixels.data();
        img.width = static_cast<int>(w);
        img.height = static_cast<int>(h);
        img.mipmaps = 1;
        img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        char name[64];
        std::snprintf(name, sizeof(name), "frame_%06llu.png", static_cast<unsigned long long>(tick));
        ExportImage(img, (dir / name).string().c_str());
    }

    void devLoop(PS2Runtime *rt, std::filesystem::path dumpDir, uint64_t dumpEvery, uint64_t dumpFrom, std::vector<InputStep> steps,
                 uint64_t exitTick, std::filesystem::path ramDir, uint64_t ramTick)
    {
        uint64_t last = ~0ull;
        bool overrideOn = false;
        const uint64_t lastStepEnd = steps.empty() ? 0u
                                                   : std::max_element(steps.begin(), steps.end(),
                                                                      [](const InputStep &a, const InputStep &b)
                                                                      { return a.end < b.end; })->end;
        for (;;)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            const uint64_t tick = rt->eeScheduler().currentVSyncTick();
            if (tick == last)
                continue;
            last = tick;
            if (!steps.empty())
            {
                if (tick < lastStepEnd)
                {
                    uint16_t mask = 0;
                    uint8_t lx = 0x80, ly = 0x80;
                    for (const InputStep &s : steps)
                        if (tick >= s.start && tick < s.end)
                        {
                            mask |= s.mask;
                            if (s.lx >= 0) lx = static_cast<uint8_t>(s.lx);
                            if (s.ly >= 0) ly = static_cast<uint8_t>(s.ly);
                        }
                    ps2_stubs::setPadOverrideState(static_cast<uint16_t>(~mask), lx, ly, 0x80, 0x80);
                    overrideOn = true;
                }
                else if (overrideOn)
                {
                    ps2_stubs::clearPadOverrideState();
                    overrideOn = false;
                    std::cout << "[blade:input] guion terminado en VSync " << tick << std::endl;
                }
            }
            // el marco presentado se fija en el hilo de la ventana al cambiar de VSync: se lee uno despues
            if (!dumpDir.empty() && tick > dumpFrom && (tick % dumpEvery) == (1u % dumpEvery))
                writeFrame(dumpDir, tick - 1u, *rt);
            if (ramTick && tick >= ramTick && !ramDir.empty())
            {
                char name[64];
                std::snprintf(name, sizeof(name), "ram_%06llu.bin", static_cast<unsigned long long>(tick));
                if (FILE *f = std::fopen((ramDir / name).string().c_str(), "wb"))
                {
                    std::fwrite(rt->memory().getRDRAM(), 1, PS2_RAM_SIZE, f);
                    std::fclose(f);
                    std::cout << "[blade] RAM guardada en VSync " << tick << std::endl;
                }
                ramTick = 0;
            }
            if (exitTick && tick >= exitTick)
            {
                std::cout << "[blade] BLADE_EXIT_TICK alcanzado (" << tick << ")" << std::endl;
                std::fflush(stdout);
                std::_Exit(0);
            }
        }
    }
}

void bladeStartDevTools(PS2Runtime &runtime)
{
    std::filesystem::path dumpDir;
    uint64_t dumpEvery = 30;
    uint64_t dumpFrom = 0;
    if (const char *env = std::getenv("BLADE_DUMP_FRAMES"); env && *env)
    {
        std::string v = env;
        if (const size_t comma = v.find(','); comma != std::string::npos)
        {
            dumpEvery = std::max<uint64_t>(1u, std::strtoull(v.c_str() + comma + 1, nullptr, 10));
            if (const size_t comma2 = v.find(',', comma + 1); comma2 != std::string::npos)
                dumpFrom = std::strtoull(v.c_str() + comma2 + 1, nullptr, 10);
            v = v.substr(0, comma);
        }
        dumpDir = v;
        std::error_code ec;
        std::filesystem::create_directories(dumpDir, ec);
        std::cout << "[blade] volcado de marcos en " << dumpDir.string() << " cada " << dumpEvery << " VSync"
                  << std::endl;
    }
    std::vector<InputStep> steps;
    if (const char *env = std::getenv("BLADE_INPUT"); env && *env)
    {
        steps = parseInput(env);
        std::cout << "[blade:input] " << steps.size() << " pasos" << std::endl;
    }
    uint64_t exitTick = 0;
    if (const char *env = std::getenv("BLADE_EXIT_TICK"); env && *env)
        exitTick = std::strtoull(env, nullptr, 10);
    std::filesystem::path ramDir;
    uint64_t ramTick = 0;
    if (const char *env = std::getenv("BLADE_DUMP_RAM"); env && *env)
    {
        std::string v = env;
        if (const size_t comma = v.find(','); comma != std::string::npos)
        {
            ramTick = std::strtoull(v.c_str() + comma + 1, nullptr, 10);
            ramDir = v.substr(0, comma);
            std::error_code ec;
            std::filesystem::create_directories(ramDir, ec);
        }
    }
    if (dumpDir.empty() && steps.empty() && !exitTick && !ramTick)
        return;
    std::thread(devLoop, &runtime, dumpDir, dumpEvery, dumpFrom, std::move(steps), exitTick, ramDir, ramTick).detach();
}
