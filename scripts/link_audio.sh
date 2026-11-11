#!/bin/sh
# Enlaza build/audio/blade_recomp reutilizando los objetos del codigo generado ya compilados en build/blade (el codigo
# generado no cambia con el audio), y recompilando solo el runtime, el IOP y runner/ en build/audio.
set -e
cd "$(dirname "$0")/.."
ninja -C build/audio ps2recomp/ps2xIOP/libps2_iop.a ps2recomp/ps2xRuntime/libps2_runtime.a \
  CMakeFiles/blade_recomp.dir/blade_overrides.cpp.o CMakeFiles/blade_recomp.dir/blade_devtools.cpp.o \
  CMakeFiles/blade_recomp.dir/blade_input.cpp.o CMakeFiles/blade_recomp.dir/blade_audio.cpp.o \
  "CMakeFiles/blade_recomp.dir/$(pwd)/third_party/PS2Recomp/ps2xRuntime/src/main.cpp.o"
cmd=$(ninja -C build/audio -t commands blade_recomp | tail -1 | sed -e 's/^: && //' -e 's/ && :$//')
OLD="CMakeFiles/blade_recomp.dir$(pwd)/work/output2/"
NEW="../blade/CMakeFiles/blade_recomp.dir$(echo "$(pwd)" | sed 's/-audio$//')/work/output2/"
cmd=$(printf '%s' "$cmd" | sed "s#$OLD#$NEW#g")
cd build/audio
eval "$cmd"
