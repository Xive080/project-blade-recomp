#!/bin/sh
# G0: clasifica las primitivas que el GS por CPU dibuja en serie (ver docs/PERFORMANCE_BASELINE.md, seccion G0).
# Uso: scripts/g0_classify.sh [desde_Present] [hasta_Present] [tick_fin]   (por defecto 950 2250 3950 = nivel 1, ticks ~2600-3900)
# En modo determinista el indice de Present va ~1650 por detras del tick desde el tick 2500. Un solo proceso a la vez.
# Salida: work/g0/g0_<desde>_<hasta>.log (lineas "[g0]"). Instrumentacion: patches/ps2recomp-zzzzzz-gs-g0.patch (apagada sin PS2X_GS_G0).
set -e
cd "$(dirname "$0")/.."
FROM=${1:-950}; TO=${2:-2250}; END=${3:-3950}
mkdir -p work/g0
SC="300:START;400:START;500:START;600:START;700:START;900:CROSS;1050:CROSS;1200:CROSS;2250:CROSS;2400:CROSS"
PS2X_DETERMINISTIC=1 PS2X_GS_G0="$FROM,$TO" BLADE_HIDDEN=1 BLADE_DET_INPUT="$SC" BLADE_DET_EXIT="$END" \
  build/blade/blade_recomp disc/SCES_502.46 > "work/g0/g0_${FROM}_${TO}.log" 2>&1
grep -a '^\[g0\]' "work/g0/g0_${FROM}_${TO}.log" | cut -c1-400
