// Herramientas de desarrollo opcionales (solo se activan con variables de entorno; sin ellas no hacen nada):
//   BLADE_DUMP_FRAMES=carpeta[,cada_N[,desde]]  guarda en PNG la imagen que el runtime presenta en la ventana, cada N
//                                        VSync (por defecto 30), a partir del VSync 'desde' (por defecto 0). Ficheros carpeta/frame_TTTTTT.png con TTTTTT = VSync.
//   BLADE_INPUT="300:START;420+20:CROSS;500:UP,CROSS"
//                                        pulsa botones del mando 1 en VSync concretos. Cada paso es TICK[+DURACION]:BOTONES
//                                        (duracion por defecto 6 VSync; botones separados por comas; prefijo "t=" opcional).
//                                        Mientras el guion tiene pasos pendientes el teclado no cuenta; al acabar se suelta.
//   BLADE_DUMP_RAM=carpeta,VSync[,VSync...] guarda la RAM del EE (32 MB) en carpeta/ram_VSync.bin al llegar a cada VSync.
//   BLADE_EXIT_TICK=N                    cierra el proceso al llegar al VSync N (para pruebas sin ventana atendida).
//   BLADE_STATS=1                        escribe cada segundo real el VSync actual y los VSync por segundo
//                                        ("[blade:stats] VSync N  V/s") y, justo antes, "[blade:frames]" con la media, el minimo y el p1% de
//                                        los tiempos de fotograma de ese segundo; al salir, el total.
//   BLADE_VU1_DIFF=fichero[,pc=hex][,img=hex][,max=N]  banco diferencial A (interprete) contra B (compilado) sobre una grabacion; sale.
//   BLADE_VU1_REPLAY=fichero             reproduce y compara una grabacion del VU1 (PS2X_VU1_RECORD=fichero[,desde[,cuantas]]) y sale.
//   BLADE_HIDDEN=1                       crea la ventana oculta y sin foco (pruebas en segundo plano; los volcados
//                                        de marcos siguen funcionando porque leen la imagen del GS, no la ventana).
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
    // Se evalua antes de main: SetConfigFlags solo anade banderas que InitWindow lee despues (el runtime anade
    // FLAG_WINDOW_RESIZABLE con otra llamada, que no borra estas).
    const bool kHiddenWindow = []()
    {
        const char *env = std::getenv("BLADE_HIDDEN");
        if (!env || !*env || *env == '0')
            return false;
        SetConfigFlags(FLAG_WINDOW_HIDDEN | FLAG_WINDOW_UNFOCUSED);
        return true;
    }();

    struct InputStep
    {
        uint64_t start = 0;
        uint64_t end = 0;  // exclusivo
        uint16_t mask = 0; // bits activos (1 = pulsado)
        int lx = -1, ly = -1;
        // momento (reloj del anfitrion) en que se activo: un paso dura al menos kMinHoldMs aunque los VSync corran
        // mas que el sondeo (con el EE ocioso el tiempo avanza a saltos y el juego podria no ver la pulsacion)
        std::chrono::steady_clock::time_point since{};
        bool seen = false;
    };
    constexpr auto kMinHoldMs = std::chrono::milliseconds(150);

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

    // estado de los hilos y semaforos del EE (con el volcado de RAM: sp sirve para recorrer la pila a mano)
    void printKernel(PS2Runtime &rt, uint64_t tick)
    {
        static const char *kStatus[] = {"run", "ready", "wait", "waitsusp", "susp", "dormant"};
        static const char *kWait[] = {"-", "sleep", "sema", "evflag", "vsync", "ext", "mpeg"};
        const EeKernelSnapshot snap = rt.eeScheduler().snapshot();
        std::printf("[blade:kernel] VSync %llu, hilo en curso %d\n", static_cast<unsigned long long>(tick),
                    snap.runningThreadId);
        for (const EeThreadSnapshot &t : snap.threads)
            std::printf("[blade:kernel]  hilo %d entry=%08x pc=%08x ra=%08x sp=%08x prio=%d %s %s %d\n", t.id, t.entry,
                        t.pc, t.ra, t.sp, t.currentPriority, kStatus[static_cast<int>(t.status) % 6],
                        kWait[static_cast<int>(t.waitReason) % 7], t.waitId);
        for (const EeSemaphoreSnapshot &s : snap.semaphores)
            std::printf("[blade:kernel]  sema %d count=%d max=%d waiters=%u\n", s.id, s.count, s.maxCount, s.waiters);
        for (const EeEventFlagSnapshot &e : snap.eventFlags)
            std::printf("[blade:kernel]  evflag %d bits=%08x waiters=%u\n", e.id, e.bits, e.waiters);
        std::fflush(stdout);
    }

    void devLoop(PS2Runtime *rt, std::filesystem::path dumpDir, uint64_t dumpEvery, uint64_t dumpFrom, std::vector<InputStep> steps,
                 uint64_t exitTick, std::filesystem::path ramDir, std::vector<uint64_t> ramTicks, bool stats)
    {
        uint64_t last = ~0ull;
        auto statsSince = std::chrono::steady_clock::now();
        uint64_t statsTick = 0;
        // Tiempos de fotograma (ms reales entre VSync; si el tiempo avanza a saltos, el salto se reparte).
        std::vector<float> frameMs, allFrameMs;
        auto frameSince = statsSince;
        uint64_t frameTick = 0;
        const auto percentile = [](std::vector<float> &v, double q)
        {
            if (v.empty())
                return 0.0f;
            const size_t i = std::min(v.size() - 1, static_cast<size_t>(q * static_cast<double>(v.size())));
            std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(i), v.end());
            return v[i];
        };
        const auto summary = [&](const char *tag, std::vector<float> &v)
        {
            if (v.empty())
                return;
            double sum = 0.0;
            for (const float f : v)
                sum += f;
            const float mn = *std::min_element(v.begin(), v.end());
            const float p99 = percentile(v, 0.99);
            std::printf("[blade:frames] %s n=%zu media %.1f ms (%.1f/s) min %.1f ms p1%% %.1f ms (%.1f/s)\n", tag, v.size(),
                        sum / static_cast<double>(v.size()), 1000.0 / (sum / static_cast<double>(v.size())), mn, p99,
                        1000.0 / std::max(p99, 0.001f));
            std::fflush(stdout);
        };
        bool overrideOn = false;
        const uint64_t lastStepEnd = steps.empty() ? 0u
                                                   : std::max_element(steps.begin(), steps.end(),
                                                                      [](const InputStep &a, const InputStep &b)
                                                                      { return a.end < b.end; })->end;
        for (;;)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(250));
            const uint64_t tick = rt->eeScheduler().currentVSyncTick();
            const auto now = std::chrono::steady_clock::now();
            if (stats && tick != frameTick)
            {
                if (tick > frameTick && frameTick != 0u)
                {
                    const float per = std::chrono::duration<float, std::milli>(now - frameSince).count() /
                                      static_cast<float>(tick - frameTick);
                    for (uint64_t i = frameTick; i < tick && frameMs.size() < 4096u; ++i)
                        frameMs.push_back(per);
                    allFrameMs.push_back(per);
                }
                frameTick = tick;
                frameSince = now;
            }
            if (stats && now - statsSince >= std::chrono::seconds(1))
            {
                summary("1s", frameMs);
                frameMs.clear();
                const double secs = std::chrono::duration<double>(now - statsSince).count();
                std::printf("[blade:stats] VSync %llu %.1f\n", static_cast<unsigned long long>(tick),
                            static_cast<double>(tick - statsTick) / secs);
                std::fflush(stdout);
                statsSince = now;
                statsTick = tick;
            }
            bool holding = false;
            for (InputStep &s : steps)
            {
                if (tick >= s.start && !s.seen)
                {
                    s.seen = true;
                    s.since = now;
                }
                holding = holding || (s.seen && now - s.since < kMinHoldMs);
            }
            if (tick == last && !holding)
                continue;
            const uint64_t previous = last;
            last = tick;
            if (!steps.empty())
            {
                if (tick < lastStepEnd || holding)
                {
                    uint16_t mask = 0;
                    uint8_t lx = 0x80, ly = 0x80;
                    for (const InputStep &s : steps)
                        if (s.seen && (tick < s.end || now - s.since < kMinHoldMs))
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
            // (si los VSync corren mas que el sondeo, se guarda al cruzar cada multiplo de N)
            if (!dumpDir.empty() && tick > dumpFrom && tick != previous &&
                (previous == ~0ull || (tick - 1u) / dumpEvery != (previous - 1u) / dumpEvery))
                writeFrame(dumpDir, tick - 1u, *rt);
            if (!ramTicks.empty() && tick >= ramTicks.front() && !ramDir.empty())
            {
                char name[64];
                std::snprintf(name, sizeof(name), "ram_%06llu.bin", static_cast<unsigned long long>(tick));
                if (FILE *f = std::fopen((ramDir / name).string().c_str(), "wb"))
                {
                    std::fwrite(rt->memory().getRDRAM(), 1, PS2_RAM_SIZE, f);
                    std::fclose(f);
                    std::cout << "[blade] RAM guardada en VSync " << tick << std::endl;
                }
                printKernel(*rt, tick);
                ramTicks.erase(ramTicks.begin());
            }
            if (exitTick && tick >= exitTick)
            {
                if (stats)
                    summary("total", allFrameMs);
                std::cout << "[blade] BLADE_EXIT_TICK alcanzado (" << tick << ")" << std::endl;
                std::fflush(stdout);
                std::_Exit(0);
            }
        }
    }
}

int ps2xVu1Replay(const char *path); // third_party/PS2Recomp, ps2_vu1_core.cpp
int ps2xVu1Gen(const char *spec);
int ps2xVu1Diff(const char *spec); // banco diferencial (patch ps2recomp-zzz-vu1-diffbench)

void bladeStartDevTools(PS2Runtime &runtime)
{
    // BLADE_VU1_REPLAY=fichero: reproduce un fichero grabado con PS2X_VU1_RECORD, compara y sale (0 = identico).
    if (const char *replay = std::getenv("BLADE_VU1_REPLAY"); replay && *replay)
        std::exit(ps2xVu1Replay(replay));
    // BLADE_VU1_DIFF=fichero[,pc=hex][,img=hex][,max=N][,show=N]: banco diferencial interprete contra implementacion compilada.
    if (const char *diff = std::getenv("BLADE_VU1_DIFF"); diff && *diff)
        std::exit(ps2xVu1Diff(diff));
    if (const char *gen = std::getenv("BLADE_VU1_GEN"); gen && *gen)
        std::exit(ps2xVu1Gen(gen));
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
    std::vector<uint64_t> ramTicks;
    if (const char *env = std::getenv("BLADE_DUMP_RAM"); env && *env)
    {
        std::string v = env;
        if (const size_t comma = v.find(','); comma != std::string::npos)
        {
            std::stringstream ts(v.substr(comma + 1));
            std::string t;
            while (std::getline(ts, t, ','))
                if (const uint64_t n = std::strtoull(t.c_str(), nullptr, 10))
                    ramTicks.push_back(n);
            std::sort(ramTicks.begin(), ramTicks.end());
            ramDir = v.substr(0, comma);
            std::error_code ec;
            std::filesystem::create_directories(ramDir, ec);
        }
    }
    const char *statsEnv = std::getenv("BLADE_STATS");
    const bool stats = statsEnv && *statsEnv && *statsEnv != '0';
    if (dumpDir.empty() && steps.empty() && !exitTick && ramTicks.empty() && !stats)
        return;
    std::thread(devLoop, &runtime, dumpDir, dumpEvery, dumpFrom, std::move(steps), exitTick, ramDir, std::move(ramTicks),
                stats).detach();
}
