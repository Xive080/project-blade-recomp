// Overrides propios de Project Blade para el ELF SCES_502.46 (PAL), con el mecanismo PS2_REGISTER_GAME_OVERRIDE
// de PS2Recomp. Cada override explica por que existe.
#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "ps2_stubs.h"

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

    void applyBlade(PS2Runtime &runtime)
    {
        runtime.replaceFunction(0x00113F40u, ovCdDiskReady);
    }
}

PS2_REGISTER_GAME_OVERRIDE("Project Blade (SCES_502.46)", "SCES_502.46", 0x00233218u, 0u, applyBlade)
