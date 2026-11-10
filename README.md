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
   (SHA1 `1e8e5acc5e96d9123696e887c90189fd29fe6309`). Pon tambien la imagen del disco como `disc/game.iso` (copia o
   enlace) o en la variable `BLADE_ISO`: el lector propio del juego lee por LSN del disco original (ver Estado).
2. `scripts/translate.sh disc/SCES_502.46` clona PS2Recomp (commit `e563698`), aplica `patches/`, compila analizador y
   recompilador, y genera `work/output2/`.
   Recomendado: antes, `GHIDRA=<carpeta de Ghidra> scripts/ghidra_map.sh` (Ghidra 12.1.x con la extension
   [ghidra-emotionengine-reloaded](https://github.com/chaoticgd/ghidra-emotionengine-reloaded) y JDK 21) para que
   `translate.sh` use los limites de funcion de Ghidra (ver Estado).
3. Para los videos (sceMpeg) hace falta FFmpeg con pkg-config (en Mac: `brew install ffmpeg pkgconf`); si no lo
   encuentra, compila sin FFmpeg y los videos no avanzan.
   `cmake -S runner -B build/blade -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/blade --target blade_recomp`
4. `build/blade/blade_recomp disc/SCES_502.46` (el runtime toma la raiz del CD de la carpeta del ELF).

## Parches sobre PS2Recomp
Heredados del proyecto hermano de Vampire Night, que usa el mismo runtime:
- `ps2recomp-mfifo-vif1.patch`: anillo MFIFO de SPR a VIF1 (RBSR/RBOR).
- `ps2recomp-dma-recvn.patch`: implementa `sceDmaRecvN`.

Propios:
- `ps2recomp-pad-reqstate-busy.patch`: en los stubs de libpad, cada peticion (`PortOpen`, `SetMainMode`,
  `SetActAlign`...) queda en BUSY y `scePadGetReqState` la ve BUSY una vez antes de completarla, como en la consola.

## Estado (2026-11-10)
- Traduccion: el analizador detecta 3 334 funciones en el ejecutable (un solo segmento, 0x100000-0x336280). 18 dan error
  de instruccion no soportada y son datos (texto ASCII tomado por codigo, p. ej. `0x47656373` = "secG"); se saltan con `skip`
  (`scripts/make_skip.py`) y la segunda pasada genera 3 316 ficheros C++ (99 MB) con 0 errores.
- Build: `build/blade/blade_recomp` compila y enlaza en macOS arm64 (sse2neon; FFmpeg opcional), 40 MB.
- Primer arranque: carga el ELF, salta a la entrada 0x233218 y abre ventana y audio. La ventana se queda en magenta (no
  llega ningun dibujo).
- Bloqueo 1 (resuelto): `sub_00113F40` reintentaba sin fin `sceSifBindRpc(0x8000059A)`. Es `sceCdDiskReady(mode)` de libcdvd
  (el ELF no tiene simbolos y el analizador no la reconocio): espera al servidor CDVDFSV, que vive en la BIOS y aqui no existe.
  `runner/blade_overrides.cpp` la sustituye por el stub HLE del runtime. Con eso el juego carga los 7 IRX del disco
  (SIO2MAN, PADMAN, MCMAN, MCSERV, GTFSCDVD, LIBSD, TSSDRV) en el IOP emulado y arranca su servicio de audio.
- Bloqueo 2 (resuelto): `jalr` a 0 en 0x1C805C, dentro de RenderWare. No era el motor: el analizador habia cortado la
  funcion 0x1C7FC0 en 0x1C8000, justo en el delay slot del `jalr` de 0x1C7FFC, asi que `addiu $s1,$s2,0x130` no se
  ejecutaba y el salto iba a `[0]`. (`_rwMemoryOpen` en 0x1C83F0 si rellena la tabla de memoria.) Solucion: limites de
  funcion de Ghidra con el procesador R5900 (`scripts/ghidra_map.sh`). Ghidra de serie no sirve: no decodifica MMI
  (`lq`/`sq`/`paddub`) y trunca funciones. `scripts/filter_ghidra_map.py` quita las etiquetas internas que exporta el
  script (~25 000), recupera el codigo al que solo se llega por punteros (etiquetas fuera de funcion con pinta de inicio)
  y los huecos pequenos entre funciones (p. ej. 0x1C2880, 0x1DB030). Resultado: ~3 980 funciones, 0 errores.
- Con eso el juego pasa la inicializacion de RenderWare y empieza a mandar DMA al GS (configuracion de video).
- Inicios en el relleno: Ghidra etiqueta a veces el principio de una zona `jr $ra` + delay slot + nops + funcion
  (0x219830); la funcion real empieza en 0x219840 y se llama por puntero. El filtro solo toma como relleno los nops
  que siguen al final de una funcion y parte los rangos recuperados tras `jr $ra` + relleno.
- Bloqueo 3 (resuelto): bucle en 0x177318 esperando `scePadGetReqState == 2` (BUSY) justo despues de abrir el puerto
  del mando. El stub del runtime lo dejaba en COMPLETE desde el principio. Parche `ps2recomp-pad-reqstate-busy.patch`.
- Bloqueo 4 (resuelto): el hilo de carga (0x16B0A0) esperaba el semaforo que da el comando SIF 0 que manda
  `GTFSCDVD.IRX` (servidor RPC `0x475453`) al acabar una lectura. Ese modulo lee por LSN del disco original
  (0x1E391C...) y el ISO virtual que el IOP arma desde la carpeta extraida tiene otros LSN, asi que cada `sceCdRead`
  fallaba y se reintentaba sin fin. `blade_overrides.cpp` usa la imagen del disco (`game.iso`/`BLADE_ISO`).
- Con eso las lecturas se completan y el juego manda cientos de paquetes al GIF. Siguiente punto: un video (`sceMpeg`),
  que sin FFmpeg se queda esperando frames; el runner activa FFmpeg si lo encuentra.
- Metodo tomado de [hctp-recomp](https://github.com/BigDaddyJRich/hctp-recomp) (WWE SmackDown! HCTP con PS2Recomp:
  Ghidra + emotionengine-reloaded + inicios recuperados), que tambien ejecuta los IRX propios del juego.
- Otras funciones de libcdvd sin nombre en el mismo bloque (0x113070, 0x1132A8, 0x113340, 0x113470, 0x1137D8, 0x113AF0): si
  alguna vuelve a esperar al IOP, se identifica igual (SID y numero de funcion RPC) y se sustituye por su stub.
