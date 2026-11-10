// Overrides propios de Project Blade para el ELF SCES_502.46 (PAL), con el mecanismo PS2_REGISTER_GAME_OVERRIDE
// de PS2Recomp. Cada override explica por que existe.
#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "ps2_stubs.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>

void bladeStartDevTools(PS2Runtime &runtime); // blade_devtools.cpp

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

    void applyBlade(PS2Runtime &runtime)
    {
        runtime.replaceFunction(0x00113F40u, ovCdDiskReady);
        useDiscImage();
        bladeStartDevTools(runtime);
    }
}

PS2_REGISTER_GAME_OVERRIDE("Project Blade (SCES_502.46)", "SCES_502.46", 0x00233218u, 0u, applyBlade)
