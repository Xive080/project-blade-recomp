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

namespace
{
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
        useDiscImage();
        bladeStartDevTools(runtime);
        bladeStartKeyboardMouse(runtime);
    }
}

PS2_REGISTER_GAME_OVERRIDE("Project Blade (SCES_502.46)", "SCES_502.46", 0x00233218u, 0u, applyBlade)
