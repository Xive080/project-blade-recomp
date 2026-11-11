#!/usr/bin/env bash
# Regresion QA de AirBlade en modo determinista: entrada por tick de VSync, sin reloj de pared.
# Uso: scripts/qa_regression.sh [-b binario] [-o carpeta_salida] [-r carpeta_referencia] [-u] [-t VSync_final] [-e "VAR=1 VAR2=x"]
#   -u  guarda este volcado como nueva referencia (PNG de los hitos + hash.txt)
# Hitos: menu 650/900/1100, partida 3000/3900. Aprueba solo si PNG de hitos y hashes (RAM y frame) son identicos.
# El binario debe incluir el parche ps2recomp-z-deterministic y runner/qa_deterministic.cpp.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/qa/blade_recomp"; OUT="$ROOT/qa/run_$(date +%Y%m%d_%H%M%S)"; REF="$ROOT/qa/ref_det"
UPDATE=0; END=3950; EXTRA=""
while getopts "b:o:r:ut:e:" o; do case $o in b) BIN="$OPTARG";; o) OUT="$OPTARG";; r) REF="$OPTARG";; u) UPDATE=1;; t) END="$OPTARG";; e) EXTRA="$OPTARG";; esac; done
mkdir -p "$OUT/frames"
SCRIPT="300:START;400:START;500:START;600:START;700:START;900:CROSS;1050:CROSS;1200:CROSS;2250:CROSS;2400:CROSS"
echo "[qa] binario=$BIN salida=$OUT ref=$REF fin=$END extra='$EXTRA'" | tee "$OUT/info.txt"
START_NS=$(python3 -c 'import time;print(time.time_ns())')
( cd "$ROOT" && env PS2X_DETERMINISTIC=1 BLADE_HIDDEN=1 BLADE_DET_INPUT="$SCRIPT" BLADE_DET_HASH=50 \
    BLADE_DET_DUMP="$OUT/frames,50" BLADE_DET_EXIT="$END" $EXTRA "$BIN" disc/SCES_502.46 ) > "$OUT/run.log" 2>&1
RC=$?
END_NS=$(python3 -c 'import time;print(time.time_ns())')
echo "[qa] salida rc=$RC tiempo real $(( (END_NS-START_NS)/1000000 )) ms" | tee -a "$OUT/info.txt"
grep -a "^\[det\]" "$OUT/run.log" > "$OUT/hash.txt"
grep -aE "guest-branch|missing-target|Unimplemented|Syscall TODO|Segmentation|abort" "$OUT/run.log" | sort | uniq -c | head -20 > "$OUT/errores.txt"
if [ "$UPDATE" = 1 ]; then
  mkdir -p "$REF"; for t in 650 900 1100 3000 3900; do cp "$OUT/frames/frame_$(printf %06d $t).png" "$REF/" 2>/dev/null || echo "falta hito $t"; done
  cp "$OUT/hash.txt" "$REF/hash.txt"; echo "[qa] referencia actualizada en $REF"; exit 0
fi
FAIL=0
for t in 650 900 1100 3000 3900; do f=$(printf frame_%06d.png $t)
  if cmp -s "$OUT/frames/$f" "$REF/$f"; then echo "hito $t: IGUAL"; else echo "hito $t: DIFERENTE"; FAIL=1; fi; done | tee "$OUT/informe.md"
if [ -f "$REF/hash.txt" ]; then
  # solo se comparan los ticks que existen en ambos (por si -t es menor)
  N=$(wc -l < "$OUT/hash.txt")
  if head -n "$N" "$REF/hash.txt" | cmp -s - "$OUT/hash.txt"; then echo "hashes RAM/frame: IGUALES ($N puntos)" | tee -a "$OUT/informe.md"
  else echo "hashes RAM/frame: DIFERENTES" | tee -a "$OUT/informe.md"; diff <(head -n "$N" "$REF/hash.txt") "$OUT/hash.txt" | head -5 | tee -a "$OUT/informe.md"; FAIL=1; fi
fi
echo "[qa] informe: $OUT/informe.md (rc=$FAIL; errores del juego: $(wc -l < "$OUT/errores.txt") lineas)"
exit $FAIL
