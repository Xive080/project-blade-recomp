#!/bin/sh
# Graba el corpus de llamadas al VU1 del nivel 1: una corrida del juego (oculta, modo normal) por escena, una a la vez.
# Cada corrida se para sola al escribir "[vu1rec] ... grabadas". Uso: scripts/vu1_record_corpus.sh [carpeta_salida] [llamadas_por_escena]
# Las escenas son indices de llamada al VU1 (aprox. 890 llamadas por VSync en el nivel; la llamada 1 300 000 cae hacia el tick 2830).
# Las grabaciones (*.rec) y work/ estan ignorados por git: son datos del juego.
cd "$(dirname "$0")/.." || exit 1
OUT=${1:-work/vu1gen/corpus}
N=${2:-15000}
mkdir -p "$OUT"
SCRIPT="300:START;400:START;500:START;600:START;700:START;900:CROSS;1050:CROSS;1200:CROSS;2250:CROSS;2400:CROSS"
# escena:desde  (tick aproximado: e1 2605, e2 2830, e3 3110, e4 3450, e5 3790)
for sc in e1:1100000 e2:1300000 e3:1550000 e4:1850000 e5:2150000; do
  name=${sc%%:*}; from=${sc##*:}
  log="$OUT/$name.log"
  PS2X_VU1_RECORD="$OUT/$name.rec,$from,$N" BLADE_HIDDEN=1 BLADE_STATS=1 BLADE_EXIT_TICK=3950 BLADE_INPUT="$SCRIPT" \
    build/blade/blade_recomp disc/SCES_502.46 > "$log" 2>&1 &
  pid=$!
  while kill -0 $pid 2>/dev/null; do
    if grep -q "\[vu1rec\].*grabadas" "$log" 2>/dev/null; then kill $pid 2>/dev/null; break; fi
    sleep 2
  done
  wait $pid 2>/dev/null
  echo "$name desde=$from: $(grep '\[vu1rec\]' "$log" | tail -1); ultimo tick: $(grep -o 'VSync [0-9]*' "$log" | tail -1)"
done
