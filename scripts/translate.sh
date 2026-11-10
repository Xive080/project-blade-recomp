#!/bin/sh
# Traduce el ELF principal con PS2Recomp (dependencia externa en third_party/, GPL-3.0).
# Uso: scripts/translate.sh RUTA/AL/SCES_502.46   (el ELF sale de tu propio disco; no se versiona)
set -e
cd "$(dirname "$0")/.."
ELF="$1"; [ -f "$ELF" ] || { echo "uso: $0 SCES_502.46"; exit 1; }
B=build/ps2recomp
[ -x $B/ps2xRecomp/ps2_recomp ] || {
  [ -d third_party/PS2Recomp ] || { git clone https://github.com/ran-j/PS2Recomp third_party/PS2Recomp && (cd third_party/PS2Recomp && git checkout -q e563698 && for p in ../../patches/*.patch; do git apply "$p"; done); }
  (cd third_party/PS2Recomp && git submodule update --init --depth 1)
  cmake -S third_party/PS2Recomp -B $B -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DPS2X_BUILD_RUNTIME=OFF -DPS2X_BUILD_STUDIO=OFF -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_ANALYZER=ON
  cmake --build $B
}
mkdir -p work; cp "$ELF" work/game.elf
$B/ps2xAnalyzer/ps2_analyzer work/game.elf work/game.toml
$B/ps2xRecomp/ps2_recomp work/game.toml > work/run.log 2>&1 || true
echo "primera pasada, errores: $(grep -c '\[error\]' work/run.log)"
# segunda pasada: las funciones con error son datos tomados por codigo; se saltan
grep '\[error\]' work/run.log | sed -E 's/.*function=([^ ]+).*/\1/' | sort -u > work/errfuncs.txt
python3 scripts/make_skip.py work/game.toml work/errfuncs.txt work/game_skip.toml
$B/ps2xRecomp/ps2_recomp work/game_skip.toml > work/run2.log 2>&1 || true
echo "segunda pasada, errores: $(grep -c '\[error\]' work/run2.log)"
# tercera pasada (recomendada): limites de funcion de Ghidra (scripts/ghidra_map.sh)
if [ -f work/ghidra_map.csv ]; then
  sed 's|^ghidra_output = .*|ghidra_output = "work/ghidra_map.csv"|' work/game_skip.toml > work/game_ghidra.toml
  rm -rf work/output2
  $B/ps2xRecomp/ps2_recomp work/game_ghidra.toml > work/run3.log 2>&1 || true
  echo "pasada con mapa de Ghidra, errores: $(grep -c '\[error\]' work/run3.log)"
fi
