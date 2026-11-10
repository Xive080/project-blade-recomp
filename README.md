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

## Controles
Teclado y ratón hacen de mando 1 (`runner/blade_input.cpp`). Un mando conectado sigue funcionando y se suma.

| PS2 | Teclado / ratón |
|---|---|
| Stick izquierdo | W A S D (con Alt, a medias) |
| Cruceta | flechas |
| X | Espacio, K, clic izquierdo |
| Cuadrado | J, clic derecho |
| Círculo | L, clic central |
| Triángulo | I, Esc, Retroceso |
| L1 / R1 | Q / E, rueda arriba / abajo |
| L2 / R2 | 1 / 3, Mayús izquierda / derecha |
| L3 / R3 | Z / C |
| Start / Select | Enter / Tab |
| Stick derecho | movimiento del ratón (`BLADE_MOUSE_GAIN`, por defecto 6) |

Esc ya no cierra la ventana. `BLADE_KBM=0` lo desactiva; con `BLADE_INPUT` (guion de pruebas) no se activa.

## Parches sobre PS2Recomp
Heredados del proyecto hermano de Vampire Night, que usa el mismo runtime:
- `ps2recomp-mfifo-vif1.patch`: anillo MFIFO de SPR a VIF1 (RBSR/RBOR).
- `ps2recomp-dma-recvn.patch`: implementa `sceDmaRecvN`.

Propios:
- `ps2recomp-pad-reqstate-busy.patch`: en los stubs de libpad, cada peticion (`PortOpen`, `SetMainMode`,
  `SetActAlign`...) queda en BUSY y `scePadGetReqState` la ve BUSY una vez antes de completarla, como en la consola.
- `ps2recomp-mpeg-ipu-dma.patch`: videos alimentados por DMA al IPU. El juego no da datos a sceMpeg por la API
  (`sceMpegDemuxPss`/`AddBs`): registra el callback NODATA (0x172600) y alimenta el IPU por el canal 4 (toIPU) desde un
  anillo de bloques de 32 KB con tags `ref`/`refe` (rutinas tipo `vibuf` de las muestras de Sony, 0x1B3610-0x1B4290). El
  parche completa en el acto el DMA del canal 4 (modo normal o cadena: MADR/QWC/TADR/CHCR como al acabar, D_STAT.CIS4) y
  guarda los bytes; el stub de `sceMpegGetPicture`, si hay callback NODATA y no hubo demux, los pasa al decodificador
  FFmpeg y, cuando no hay imagen, llama al callback NODATA en el propio hilo (como libmpeg) y reintenta, esperando un
  VSync si la lectura del CD aun no ha llegado.
- `ps2recomp-gs-texture-direct.patch`: el rasterizador por CPU lee las texturas directamente de la VRAM. Antes copiaba
  la pagina de 8 KB en una cache que solo se invalidaba con TEXFLUSH (podia quedar obsoleta si el juego subia o
  dibujaba en esa pagina sin TEXFLUSH) y, con texturas de varias paginas, repetia la copia en cada cambio de pagina: era
  el mayor coste del rasterizador en el menu (videos de fondo de 640x512 en 40 trozos).
- `ps2recomp-iop-heap-spu2.patch`: memoria del IOP y SPU2 para el driver de audio (TSSDRV).
  - Monton: `allocate` buscaba siempre a partir de un cursor que solo crecia, asi que lo liberado no se reutilizaba; ahora
    busca el primer hueco libre (como `AllocSysMemory` en modo FIRST) y `maxFreeMemory` da el mayor hueco real. Ademas
    los modulos IRX se cargaban en una zona fija (0x10000-0x120000) y el monton se quedaba en 0x120000-0x1F0000 (832 KB);
    ahora los modulos se reservan en el mismo monton desde 0x10000, como hace loadcore con sysmem.
  - SPU2: modelo minimo de voces. Guarda la RAM de sonido (DMA de los canales 4/7 y puerto de datos 0x1AC con TSA),
    y por voz la direccion actual (NAX), el bucle (LSAX, o la bandera "loop start" del bloque ADPCM) y ENDX; KON/KOFF
    arrancan y paran voces y el tono (PITCH) avanza NAX segun el reloj del IOP (48 kHz, 28 muestras por bloque de 16
    bytes). ENVX vale 0x7FFF con la voz activa y 0 al acabar. No mezcla audio: sirve para que el driver vea avanzar la
    reproduccion (posicion de los flujos via `sceSdGetAddr`, fin de los efectos).

Herramientas propias (sin parche: `runner/blade_devtools.cpp`, solo se activan con variables de entorno):
- `BLADE_DUMP_FRAMES=carpeta[,cada_N[,desde]]`: guarda en PNG la imagen presentada cada N VSync (`frame_TTTTTT.png`).
- `BLADE_INPUT="300:START;420+20:CROSS;500:UP,CROSS"`: pulsa botones del mando 1 en VSync concretos
  (`TICK[+DURACION]:BOTONES`, duracion por defecto 6; tambien `LX=`/`LY=` para el stick izquierdo).
- `BLADE_DUMP_RAM=carpeta,VSync[,VSync...]`: guarda los 32 MB de RAM del EE en cada VSync indicado (`ram_TTTTTT.bin`).
- `BLADE_EXIT_TICK=N`: cierra el proceso en el VSync N (pruebas sin ventana atendida).
- Guion que llega hasta el modo historia: `BLADE_INPUT="300:START;400:START;500:START;600:START;700:START;900:CROSS;1050:CROSS;1200:CROSS;2250:CROSS;2400:CROSS"`.

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
- Con eso las lecturas se completan y el juego manda cientos de paquetes al GIF hasta llegar al primer video (`sceMpeg`).
- Bloqueo 5 (resuelto): el hilo principal esperaba en `sceMpegGetPicture` (0x107E08, llamada desde 0x171E94) aun con
  FFmpeg. El juego no usa `sceMpegDemuxPss`/`AddBs`: lee `MPEG/*.M2V` (video elemental, sin PSS) a un anillo de bloques
  de 32 KB y lo manda al IPU por DMA canal 4 desde su callback NODATA (0x172600), que solo llama libmpeg. Ni el canal 4
  ni ese callback existian en el runtime. Parche `ps2recomp-mpeg-ipu-dma.patch` (ver Parches). Con eso SPLASH.M2V se
  decodifica a 640x512 a su ritmo (una imagen cada 2 VSync), termina, y el juego arranca un segundo video (por tamano y momento, MENUBASE.M2V, fondo del menu; sin confirmar);
  con START (tecla Enter) salta el video de inicio, asi que lee el mando.
- Bloqueo 6 (resuelto): `jalr` a 0x260FE0 (desde 0x24D040, tabla de metodos) sin funcion. Es una hoja de 0x34 bytes, la
  ultima del texto, pegada al `jr $ra` de 0x260F70 y con datos detras; Ghidra no la crea y el hueco hasta la siguiente
  funcion es demasiado grande para el paso 3 del filtro. `filter_ghidra_map.py` acepta ahora en huecos grandes una hoja
  corta (`jr $ra` en < 0x100 bytes, sin otros saltos, relleno detras y sin `eret`) y la cierra en su delay slot. Hay
  que regenerar el mapa (`filter_ghidra_map.py build/ghidra/ghidra_map.csv work/game.elf work/ghidra_map.csv`) y el
  codigo.
- Hasta donde llega (con el guion de `BLADE_INPUT` de Parches): videos de inicio (SCEE, Criterion, RenderWare, Dolby,
  patrocinadores), pantalla de titulo "Press START", menu principal, seleccion de partida (la tarjeta de memoria
  funciona: crea y guarda GAME1 en `mc0`, autoguardado en la ranura 1), un jugador, modo historia, CONTINUE, pantalla de
  carga "hideaway" limpia y carga del primer nivel (TSSDRV carga los efectos: board, jump, kickflip...). El nivel 3D
  se dibuja (cielo, suelo, decorado, letrero LOADING); la pantalla quedaba negra hasta el bloqueo 7.
- El VSync del runtime sale de ciclos EE contados, no del reloj: en este Mac van unos 8-13 VSync por segundo real.
- Bloqueo 7 (resuelto, nivel negro): el fundido a negro del nivel (20 sprites `rgba=0,0,0,126`, `ALPHA=0x44`) espera a
  que avance el flujo de audio de la escena de entrada. TSSDRV (servidor RPC 0x701) abre los flujos con
  `TssStreamsOpen` y escribe su estado en el EE (0x317540, 8 entradas {estado, tiempo en ms}) por SIF DMA; el flujo del
  nivel quedaba en estado 2 (fallo al abrir) porque `RwaStreamOpen` no podia reservar su buffer (0x40000 bytes) en el
  monton del IOP: el monton no reutilizaba lo liberado y solo tenia 832 KB. Arreglado en el IOP emulado (sin HLE), parche
  `ps2recomp-iop-heap-spu2.patch`: monton de primer hueco libre compartido con los modulos. Con eso los dos flujos se
  abren ("RwaStreamOpen : Opened and prepared") y el fundido se abre. El tiempo del flujo sale de la posicion de la voz
  en el SPU2 (`RwaVoiceGetWavePos` -> `sceSdGetAddr`, NAX), que no existia: el mismo parche anade un modelo minimo de
  voces del SPU2 y el tiempo avanza a ritmo real (12,6 s en el VSync 3000, 21,0 s en el 3500).
- Ahora: la escena de entrada del nivel 1 se ve y la camara recorre el escenario (escalera, pasillo, puerta con carteles),
  con barras de cine y el letrero LOADING abajo. Sigue igual en el VSync 5200 (unos 2 800 VSync despues de entrar), asi
  que aun no se llega a controlar al personaje.
- Pendiente 1: la entrada no termina (LOADING fijo durante todo el recorrido). Falta ver que espera el juego para dar paso
  al control: fin del flujo de audio (estado del flujo en 0x317540 al llegar al final del fichero, ENDX de la voz con el
  ultimo bloque), lecturas de CD pendientes de GTFSCDVD o el boton para saltar la escena. La banda superior (filas
  0-95, bajo la barra de cine) muestra texturas: es el mismo solape de VRAM que el menu (Pendiente 2).
- Pendiente 2 (menu corrupto): cada imagen el menu sube el video de fondo (MENUBASE.M2V, 640x512) como 40 trozos CT32
  de 128x64 en los bloques 1536..6655 y las fuentes detras (hasta ~8032), pero los buffers de pantalla estan en las
  paginas 160 y 320 (bloques 5120 y 10240) y el Z en la 480 (Z16S, 80 paginas, da la vuelta a 0-47). Los trozos 28-39 y
  las fuentes caen dentro del buffer de la pagina 160 y se pisan con el dibujo: bloques del video viejos arriba y una
  banda de basura de fuentes. Traza de las subidas (TRXDIR): todas son `dbw=2` (128 de ancho), CT32, trozos de 128x64
  (128 bloques cada uno) de 1536 a 6528, y luego 6656 (128x128), 6912/7456/8000 (16x16) y 6944/7488 (128x256): el propio
  juego pide esas direcciones, el runtime no las mueve. El hueco libre real son las paginas 48-159. Falta ver por que la cache de texturas de
  RenderWare no vuelve al principio en el limite (hipotesis: calcula el limite con buffers de 640x256). El intercambio de
  buffers (`sceGsSwapDBuffDc`) es correcto (igual que la libgraph del juego en 0x101B30).
- Pendiente 3: rendimiento. En el nivel van unos 6 VSync por segundo real (14 en el menu). Perfil de 10 s con `sample`
  durante la entrada: el rasterizador por CPU (`SampleTexture` + `WritePixel` + `Submit` + `ReadTexture`, unas 4 900
  muestras) y el interprete de VU1 (`commitReadyPipelines`, `calculatePairReadyCycle`, `execUpper`..., unas 2 800) se
  llevan casi todo; el codigo recompilado del EE apenas aparece. El SPU2 del IOP no mezcla audio, asi que no se oye la
  musica ni los efectos de LIBSD/TSSDRV.
- Metodo tomado de [hctp-recomp](https://github.com/BigDaddyJRich/hctp-recomp) (WWE SmackDown! HCTP con PS2Recomp:
  Ghidra + emotionengine-reloaded + inicios recuperados), que tambien ejecuta los IRX propios del juego.
- Otras funciones de libcdvd sin nombre en el mismo bloque (0x113070, 0x1132A8, 0x113340, 0x113470, 0x1137D8, 0x113AF0): si
  alguna vuelve a esperar al IOP, se identifica igual (SID y numero de funcion RPC) y se sustituye por su stub.
