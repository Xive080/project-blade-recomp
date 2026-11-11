// Overrides propios de Project Blade para el ELF SCES_502.46 (PAL), con el mecanismo PS2_REGISTER_GAME_OVERRIDE
// de PS2Recomp. Cada override explica por que existe.
#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "ps2_stubs.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>

void bladeStartDevTools(PS2Runtime &runtime); // blade_devtools.cpp
void bladeStartKeyboardMouse(PS2Runtime &runtime); // blade_input.cpp

extern unsigned long long gBladeQaTick; // qa_deterministic.cpp

namespace
{
    // Traza de depuracion (AB-004): BLADE_RT_TRACE=desde,hasta (VBlank). Registra cada llamada a la funcion de dibujo de
    // atomicos de RenderWare (0x200740 = RpAtomicRender por defecto: usa el pipeline del atomico y llama a 0x1D83B0).
    uint32_t readGuest32(const uint8_t *rdram, uint32_t address);
    PS2Runtime::RecompiledFunction gRtAtomicRender = nullptr;
    unsigned long long gRtFrom = 0, gRtTo = 0;
    uint32_t gRtSkipAtomic = 0;
    // BLADE_RT_FUNCS=0xDIR,0xDIR...: ademas, registra entrada (a0-a3, ra) de esas funciones durante la ventana de BLADE_RT_TRACE.
    PS2Runtime::RecompiledFunction gRtOrig[8] = {};
    uint32_t gRtAddr[8] = {};
    template <int N>
    void ovTraceGeneric(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (gRtTo && gBladeQaTick >= gRtFrom && gBladeQaTick <= gRtTo && gRtAddr[N] != 0x179CF0u)
        {
            uint32_t stamp = 0;
            if (const uint8_t *spr = ps2GetScratchpadHostPtr())
                std::memcpy(&stamp, spr + 0x3FB8u, 4);
            std::printf("[rtf] tick %llu fn=%08x a0=%08x a1=%08x a2=%08x a3=%08x ra=%08x stamp=%08x\n", gBladeQaTick, gRtAddr[N],
                        GPR_U32(ctx, 4), GPR_U32(ctx, 5), GPR_U32(ctx, 6), GPR_U32(ctx, 7), GPR_U32(ctx, 31), stamp);
        }
        const bool traceWin = gRtTo && gBladeQaTick >= gRtFrom && gBladeQaTick <= gRtTo;
        const uint32_t a0 = GPR_U32(ctx, 4);
        const uint32_t a3 = GPR_U32(ctx, 7);
        if (traceWin && gRtAddr[N] == 0x128910u && a0 == 0x0104F224u)
        {
            uint32_t sp[12] = {};
            if (const uint8_t *spr = ps2GetScratchpadHostPtr())
                std::memcpy(sp, spr + 0x10, sizeof(sp));
            std::printf("[rtl] tick %llu spr10:", gBladeQaTick);
            for (uint32_t v : sp) { float f; std::memcpy(&f, &v, 4); std::printf(" %08x(%g)", v, f); }
            std::printf("\n");
            int guard = 0;
            for (uint32_t n = readGuest32(rdram, a0 + 0xCu); n && guard < 64; n = readGuest32(rdram, n + 8u), ++guard)
            {
                const uint32_t o = readGuest32(rdram, n + 4u), vt = readGuest32(rdram, o + 0xCu);
                std::printf("[rtl] tick %llu node=%08x obj=%08x vt=%08x m7=%08x +10=%08x +48=%08x +cc=%08x\n", gBladeQaTick, n, o, vt,
                            readGuest32(rdram, vt + 0x1Cu), readGuest32(rdram, o + 0x10u), readGuest32(rdram, o + 0x48u),
                            readGuest32(rdram, o + 0xCCu));
            }
        }
        const uint32_t ra = GPR_U32(ctx, 31), s2 = GPR_U32(ctx, 18), s5 = GPR_U32(ctx, 21);
        const bool distCall = traceWin && gRtAddr[N] == 0x179CF0u && (ra == 0x128C78u || ra == 0x128C8Cu) &&
                              (s2 == 0xBF5350u || s5 == 0x0104F224u);
        gRtOrig[N](rdram, ctx, runtime);
        if (distCall)
        {
            uint32_t f0b, f20b, f21b;
            std::memcpy(&f0b, &ctx->f[0], 4); std::memcpy(&f20b, &ctx->f[20], 4); std::memcpy(&f21b, &ctx->f[21], 4);
            std::printf("[rtd] tick %llu obj=%08x cell=%08x a0=%08x f0=%08x(%g) f20=%08x(%g) f21=%08x(%g) s3=%u s4=%u fcr31=%08x\n",
                        gBladeQaTick, s2, s5, a0, f0b, ctx->f[0], f20b, ctx->f[20], f21b, ctx->f[21], GPR_U32(ctx, 19),
                        GPR_U32(ctx, 20), ctx->fcr31);
        }
        if (traceWin && gRtAddr[N] == 0x128910u && a0 == 0x0104F224u)
        {
            float sp[14] = {};
            if (const uint8_t *spr = ps2GetScratchpadHostPtr())
                std::memcpy(sp, spr + 0x10, sizeof(sp));
            std::printf("[rtp] tick %llu a3=%u player+48=%08x +10=%08x flagsw=%08x sp10=%g 14=%g 18=%g 1c=%g 20=%g 24=%g 28=%g 2c=%g 30=%g 34=%g\n",
                        gBladeQaTick, a3, readGuest32(rdram, 0xBF5350u + 0x48u), readGuest32(rdram, 0xBF5350u + 0x10u),
                        readGuest32(rdram, 0x30A368u), sp[0], sp[1], sp[2], sp[3], sp[4], sp[5], sp[6], sp[7], sp[8], sp[9]);
        }
    }
    void ovTraceAtomicRender(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t a = GPR_U32(ctx, 4);
        const bool on = gRtTo && gBladeQaTick >= gRtFrom && gBladeQaTick <= gRtTo;
        if (on)
        {
            const uint32_t fr = readGuest32(rdram, a + 4u), pipe = readGuest32(rdram, a + 0x6Cu);
            float px, py, pz; uint32_t b;
            b = readGuest32(rdram, fr + 0x80u); std::memcpy(&px, &b, 4);
            b = readGuest32(rdram, fr + 0x84u); std::memcpy(&py, &b, 4);
            b = readGuest32(rdram, fr + 0x88u); std::memcpy(&pz, &b, 4);
            std::printf("[rt] tick %llu atomic=%08x flags=%08x geom=%08x pipe=%08x pos=%.2f,%.2f,%.2f ra=%08x\n", gBladeQaTick, a,
                        readGuest32(rdram, a), readGuest32(rdram, a + 0x18u), pipe, px, py, pz, GPR_U32(ctx, 31));
        }
        if (gRtSkipAtomic && a == gRtSkipAtomic)
        {
            // BLADE_RT_SKIP=0xATOMICO: no dibuja ese atomico (para saber que pinta cada uno)
            SET_GPR_U32(ctx, 2, a);
            ctx->pc = GPR_U32(ctx, 31);
            return;
        }
        gRtAtomicRender(rdram, ctx, runtime);
    }

    // 0x113F40 = sceCdDiskReady(mode) de libcdvd. El ELF no tiene simbolos y el analizador no la reconocio, asi que se
    // traducia el codigo original: reintenta sceSifBindRpc(0x8000059A) hasta que responda el servidor CDVDFSV del IOP,
    // que en este runtime no existe (vive en la BIOS). Identificada por la forma: modo 8 = no bloqueante (devuelve 6 si
    // esta ocupado), envia el modo con 4 bytes y devuelve la palabra de respuesta. Se usa el stub HLE del runtime.
    void ovCdDiskReady(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ctx->pc = GPR_U32(ctx, 31);
        ps2_stubs::sceCdDiskReady(rdram, ctx, runtime);
    }

    // GTFSCDVD.IRX (el lector propio del juego en el IOP) pide sectores por su LSN del disco original (p. ej.
    // 0x1E391C, ~3,8 GB dentro del DVD). Con solo la carpeta extraida el IOP arma un ISO virtual con otros LSN, esas
    // lecturas fallan y el modulo reintenta sin fin. Con la imagen del propio disco, las lecturas por LSN son exactas.
    // Se busca en BLADE_ISO o en game.iso junto al ELF; sin rutas fijas.
    void useDiscImage()
    {
        PS2Runtime::IoPaths paths = PS2Runtime::getIoPaths();
        if (!paths.cdImage.empty())
            return;
        std::filesystem::path image;
        if (const char *env = std::getenv("BLADE_ISO"); env && *env)
            image = env;
        else
            image = paths.elfDirectory / "game.iso";
        std::error_code ec;
        if (std::filesystem::is_regular_file(image, ec))
        {
            paths.cdImage = image;
            PS2Runtime::setIoPaths(paths);
            std::cout << "[blade] imagen del disco: " << image.string() << std::endl;
        }
        else
        {
            std::cout << "[blade] sin imagen del disco (BLADE_ISO o game.iso junto al ELF): las lecturas por LSN de "
                         "GTFSCDVD fallaran" << std::endl;
        }
    }

    // 0x1F39E0 = inicio de la cache de texturas de RenderWare (driver sky2): recibe el principio de la VRAM libre y su
    // tamano, en palabras de 32 bits. El juego pide el modo 38 (640x512, 32 bits, entrelazado + FSAA): el driver aparta
    // las paginas 0-159, pone los buffers de dibujo en las paginas 160 y 320 y el Z (Z16S, 80 paginas) en la 480, que
    // pasa del final de los 4 MB y da la vuelta a las paginas 0-47. Calcula el inicio de la cache como fin del Z sin
    // dar la vuelta (0x118000 palabras = pagina 560) y el tamano como 4 MB menos eso: negativo. La cache cree tener
    // casi 4 GB y nunca vuelve al principio; las direcciones se truncan a 14 bits en BITBLTBUF/TEX0 y caen en la
    // pagina 48 en adelante, de modo que cada imagen las subidas (video del menu en 40 trozos, fuentes, texturas del
    // nivel) pasan de la pagina 160 y pisan los buffers de pantalla. Aqui se da la vuelta al inicio y la cache acaba
    // donde empieza el primer buffer de dibujo (env. de dibujo de sceGsDBuffDc del driver en 0x32ED10/0x32EE80).
    PS2Runtime::RecompiledFunction gRwTexCacheInit = nullptr;
    void ovRwTexCacheInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        constexpr uint32_t kVramWords = 0x100000u; // 4 MB
        uint32_t start = GPR_U32(ctx, 4);
        const uint32_t size = GPR_U32(ctx, 5);
        if (start >= kVramWords || size > kVramWords)
        {
            start %= kVramWords;
            uint32_t end = kVramWords;
            for (const uint32_t frameReg : {0x32ED10u, 0x32EE80u})
            {
                uint32_t value = 0;
                std::memcpy(&value, rdram + frameReg, sizeof(value));
                const uint32_t fbWords = (value & 0x1FFu) * 2048u; // FRAME.FBP en paginas de 8 KB
                if (fbWords > start && fbWords < end)
                    end = fbWords;
            }
            SET_GPR_U32(ctx, 4, start);
            SET_GPR_U32(ctx, 5, end - start);
        }
        gRwTexCacheInit(rdram, ctx, runtime);
    }

    // 0x1DF500 = espera del driver sky2 de RenderWare a que la cola de imagenes pendientes de mostrar baje del limite:
    //   while (*(gp-0x7EE8) + 1 < *(gp-0x79C8)) {
    //       if (gp-0x7930 || gp-0x79D0 || gp-0x79CC) { DI; if (gp-0x79CC) gp-0x79CC |= 4; EI; }  // ocupado
    //       else 0x1DEE30();                                                                     // lanza el siguiente
    //   }
    // Mientras esta "ocupado" el bucle solo espera a que una interrupcion (el VBlank que da la vuelta a la imagen)
    // cambie esas variables. En el runtime las interrupciones solo entran cuando el hilo vuelve al planificador, asi que
    // el bucle original quemaba ciclos del EE de 8 en 8 (y hacia correr el IOP un ciclo cada vez) hasta llegar al
    // plazo del VBlank: ~30 % del hilo del EE en la partida. Aqui, mientras esta ocupado, el tiempo avanza en pasos de
    // kIdleStepCycles ciclos (mismo reloj del EE e IOP que antes) hasta que toca atender algo y se vuelve al planificador.
    // Mismo marco de pila que el original (para volver a entrar tras ceder o tras una expropiacion dentro de 0x1DEE30).
    constexpr uint32_t kSkyWaitEntry = 0x001DF500u;
    constexpr uint32_t kSkyWaitLoop = 0x001DF514u;    // reentrada: marco ya creado
    constexpr uint32_t kSkyWaitAfterKick = 0x001DF540u; // vuelta de 0x1DEE30
    constexpr uint32_t kSkyKick = 0x001DEE30u;
    constexpr uint32_t kIdleStepCycles = 1024u;

    uint32_t readGuest32(const uint8_t *rdram, uint32_t address)
    {
        uint32_t value = 0;
        std::memcpy(&value, rdram + (address & (PS2_RAM_SIZE - 1u)), sizeof(value));
        return value;
    }

    void writeGuest32(uint8_t *rdram, uint32_t address, uint32_t value)
    {
        std::memcpy(rdram + (address & (PS2_RAM_SIZE - 1u)), &value, sizeof(value));
    }

    void ovSkyWaitQueue(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t sp0 = GPR_U32(ctx, 29);
        if (ctx->pc == kSkyWaitEntry)
        {
            // addiu sp,-0x20; sq ra,0x10(sp); sq s0,0(sp); addiu s0,gp,-0x7EE8
            const uint32_t sp = sp0 - 0x20u;
            SET_GPR_S32(ctx, 29, static_cast<int32_t>(sp));
            WRITE128(sp + 0x10u, GPR_VEC(ctx, 31));
            WRITE128(sp, GPR_VEC(ctx, 16));
        }
        const uint32_t gp = GPR_U32(ctx, 28);
        SET_GPR_S32(ctx, 16, static_cast<int32_t>(gp - 0x7EE8u));
        for (;;)
        {
            const int32_t done = static_cast<int32_t>(readGuest32(rdram, gp - 0x7EE8u)) + 1;
            const int32_t queued = static_cast<int32_t>(readGuest32(rdram, gp - 0x79C8u));
            if (done >= queued)
            {
                // registros como los deja el original: v1, a0, at; lq ra/s0; addiu sp,0x20; jr ra
                SET_GPR_S32(ctx, 3, done);
                SET_GPR_S32(ctx, 4, queued);
                SET_GPR_S32(ctx, 1, 0);
                const uint32_t sp = GPR_U32(ctx, 29);
                SET_GPR_VEC(ctx, 31, READ128(sp + 0x10u));
                SET_GPR_VEC(ctx, 16, READ128(sp));
                SET_GPR_S32(ctx, 29, static_cast<int32_t>(sp + 0x20u));
                ctx->pc = GPR_U32(ctx, 31);
                return;
            }
            const uint32_t busy79CC = readGuest32(rdram, gp - 0x79CCu);
            if (readGuest32(rdram, gp - 0x7930u) || readGuest32(rdram, gp - 0x79D0u) || busy79CC)
            {
                if (busy79CC)
                    writeGuest32(rdram, gp - 0x79CCu, busy79CC | 4u);
                SET_GPR_S32(ctx, 2, static_cast<int32_t>(readGuest32(rdram, gp - 0x79CCu)));
                // nada cambia hasta que el hilo ceda: avanzar el reloj hasta el siguiente punto que atender, en pasos
                // cortos (con pasos de 4096 ciclos o mas el menu perdia imagenes del video de fondo; probablemente el IOP
                // adelantaba varias respuestas sin que el EE las atendiera entre medias) y sin pasar de largo una interrupcion de los temporizadores
                for (;;)
                {
                    const uint64_t timer = runtime->memory().cyclesUntilNextEeTimerInterrupt();
                    const uint32_t cycles =
                        static_cast<uint32_t>(std::max<uint64_t>(1u, std::min<uint64_t>(kIdleStepCycles, timer)));
                    if (runtime->eeCheckpointDue(cycles))
                        break;
                }
                ctx->pc = kSkyWaitLoop;
                return;
            }
            SET_GPR_U32(ctx, 31, kSkyWaitAfterKick);
            if (!runtime->dispatchGuestBranch(rdram, ctx, kSkyKick, 0x1DF538u, kSkyWaitAfterKick,
                                              PS2Runtime::GuestBranchKind::DirectCall, "JAL"))
                return;
        }
    }

    void applyBlade(PS2Runtime &runtime)
    {
        gRwTexCacheInit = runtime.lookupFunction(0x001F39E0u);
        if (gRwTexCacheInit)
            runtime.replaceFunction(0x001F39E0u, ovRwTexCacheInit);
        runtime.replaceFunction(0x00113F40u, ovCdDiskReady);
        if (std::getenv("BLADE_NO_SKYWAIT") == nullptr)
            for (const uint32_t pc : {kSkyWaitEntry, kSkyWaitLoop, kSkyWaitAfterKick, 0x001DF550u, 0x001DF570u})
                runtime.replaceFunction(pc, ovSkyWaitQueue);
        if (const char *t = std::getenv("BLADE_RT_TRACE"); t && *t)
        {
            gRtFrom = std::strtoull(t, nullptr, 10);
            const char *c = std::strchr(t, ',');
            gRtTo = c ? std::strtoull(c + 1, nullptr, 10) : gRtFrom;
            gRtAtomicRender = runtime.lookupFunction(0x00200740u);
            if (const char *sk = std::getenv("BLADE_RT_SKIP"); sk && *sk)
                gRtSkipAtomic = static_cast<uint32_t>(std::strtoul(sk, nullptr, 16));
            if (gRtAtomicRender)
                runtime.replaceFunction(0x00200740u, ovTraceAtomicRender);
            if (const char *fl = std::getenv("BLADE_RT_FUNCS"); fl && *fl)
            {
                static const PS2Runtime::RecompiledFunction stubs[8] = {ovTraceGeneric<0>, ovTraceGeneric<1>, ovTraceGeneric<2>, ovTraceGeneric<3>,
                                                                       ovTraceGeneric<4>, ovTraceGeneric<5>, ovTraceGeneric<6>, ovTraceGeneric<7>};
                int n = 0;
                for (const char *q = fl; *q && n < 8;)
                {
                    gRtAddr[n] = static_cast<uint32_t>(std::strtoul(q, const_cast<char **>(&q), 16));
                    gRtOrig[n] = runtime.lookupFunction(gRtAddr[n]);
                    if (gRtOrig[n]) { runtime.replaceFunction(gRtAddr[n], stubs[n]); ++n; }
                    if (*q == ',') ++q;
                }
            }
        }
        useDiscImage();
        bladeStartDevTools(runtime);
        bladeStartKeyboardMouse(runtime);
    }
}

PS2_REGISTER_GAME_OVERRIDE("Project Blade (SCES_502.46)", "SCES_502.46", 0x00233218u, 0u, applyBlade)
