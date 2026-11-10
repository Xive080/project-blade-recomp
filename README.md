# AirBlade - recompilacion estatica (PS2 PAL SCES-50246)

Traduce el codigo MIPS R5900 del ELF a C++ con [PS2Recomp](https://github.com/ran-j/PS2Recomp) y lo enlaza con su runtime
(memoria, syscalls, VU0/VU1, GIF/VIF/DMA, GS sobre OpenGL). Objetivo: Mac, Windows y Linux.

Simbolos, matching y notas del motor (RenderWare): repo hermano `project-blade-decomp`.

## Reglas
- No se versiona el ELF, el disco ni el codigo generado: se produce en local desde el disco del propio usuario.
- Sin rutas absolutas.
- PS2Recomp es una dependencia externa (GPL-3.0) que se clona en `third_party/` con el commit fijado; nuestros cambios van en `patches/`.

## Reproducir
1. Extrae el disco completo en `disc/` (`bsdtar -xf <tu iso> -C disc`). El ELF es `disc/SCES_502.46`
   (SHA1 `1e8e5acc5e96d9123696e887c90189fd29fe6309`).
2. `scripts/translate.sh disc/SCES_502.46` clona PS2Recomp (commit `e563698`), aplica `patches/`, compila analizador y
   recompilador, y genera `work/output2/`.
3. `cmake -S runner -B build/blade -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/blade --target blade_recomp`
4. `build/blade/blade_recomp disc/SCES_502.46` (el runtime toma la raiz del CD de la carpeta del ELF).

## Parches sobre PS2Recomp
Heredados del proyecto hermano de Vampire Night, que usa el mismo runtime:
- `ps2recomp-mfifo-vif1.patch`: anillo MFIFO de SPR a VIF1 (RBSR/RBOR).
- `ps2recomp-dma-recvn.patch`: implementa `sceDmaRecvN`.

## Estado (2026-11-10)
- Traduccion: el analizador detecta 3 334 funciones en el ejecutable (un solo segmento, 0x100000-0x336280). 18 dan error
  de instruccion no soportada y son datos (texto ASCII tomado por codigo, p. ej. `0x47656373` = "secG"); se saltan con `skip`
  (`scripts/make_skip.py`) y la segunda pasada genera 3 316 ficheros C++ (99 MB) con 0 errores.
- Build: `build/blade/blade_recomp` compila y enlaza en macOS arm64 (sse2neon, sin FFmpeg), 40 MB.
- Primer arranque: carga el ELF, salta a la entrada 0x233218 y abre ventana y audio. La ventana se queda en magenta (no
  llega ningun dibujo).
- Bloqueo 1 (resuelto): `sub_00113F40` reintentaba sin fin `sceSifBindRpc(0x8000059A)`. Es `sceCdDiskReady(mode)` de libcdvd
  (el ELF no tiene simbolos y el analizador no la reconocio): espera al servidor CDVDFSV, que vive en la BIOS y aqui no existe.
  `runner/blade_overrides.cpp` la sustituye por el stub HLE del runtime. Con eso el juego carga los 7 IRX del disco
  (SIO2MAN, PADMAN, MCMAN, MCSERV, GTFSCDVD, LIBSD, TSSDRV) en el IOP emulado y arranca su servicio de audio.
- Bloqueo 2 (abierto): `jalr` a 0 en 0x1C805C, dentro de RenderWare: al crear un freelist llama a la funcion de reserva de
  memoria de la tabla del motor (`[s2+0x130]`, probablemente `RwEngineInstance->memoryFuncs.rwmalloc`) y esa tabla sigue a
  cero. Hipotesis: falta un paso de inicializacion del motor antes (otra funcion de SDK sin reconocer o un override que
  hace falta). Siguiente: localizar quien escribe esa tabla y por que no se ejecuto.
- Otras funciones de libcdvd sin nombre en el mismo bloque (0x113070, 0x1132A8, 0x113340, 0x113470, 0x1137D8, 0x113AF0): si
  alguna vuelve a esperar al IOP, se identifica igual (SID y numero de funcion RPC) y se sustituye por su stub.
