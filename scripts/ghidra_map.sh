#!/bin/sh
# Limites de funcion con Ghidra + ghidra-emotionengine-reloaded (procesador r5900). El analizador de PS2Recomp corta
# funciones en medio en este ELF sin simbolos (p. ej. 0x1C8000, en el delay slot de un jalr de RenderWare).
# Requiere: Ghidra 12.1.x con la extension instalada y un JDK 21 (JAVA_HOME). GHIDRA = carpeta de instalacion de Ghidra.
# Uso: GHIDRA=/ruta/a/ghidra scripts/ghidra_map.sh   -> work/ghidra_map.csv
set -e
cd "$(dirname "$0")/.."
: "${GHIDRA:?pon GHIDRA=<carpeta de instalacion de Ghidra>}"
D=build/ghidra; mkdir -p $D/scripts $D/proj
cp third_party/PS2Recomp/ps2xRecomp/tools/ghidra/ExportPS2Functions.java $D/scripts/
printf 'Choose output TOML config file Save = %s/ghidra.toml\nChoose output CSV file Save = %s/ghidra_map.csv\n' "$PWD/$D" "$PWD/$D" > $D/scripts/ExportPS2Functions.properties
"$GHIDRA/support/analyzeHeadless" $D/proj blade -import work/game.elf -overwrite -processor "r5900:LE:32:default" \
  -scriptPath $D/scripts -postScript ExportPS2Functions.java > $D/ghidra.log 2>&1
python3 scripts/filter_ghidra_map.py $D/ghidra_map.csv work/game.elf work/ghidra_map.csv
