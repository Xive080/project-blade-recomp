// Overrides propios de Project Blade para el ELF SCES_502.46 (PAL), con el mecanismo PS2_REGISTER_GAME_OVERRIDE
// de PS2Recomp. Cada override explica por que existe. De momento no hace falta ninguno.
#include "game_overrides.h"
#include "ps2_runtime.h"

namespace
{
    void applyBlade(PS2Runtime &) {}
}

PS2_REGISTER_GAME_OVERRIDE("Project Blade (SCES_502.46)", "SCES_502.46", 0x00233218u, 0u, applyBlade)
