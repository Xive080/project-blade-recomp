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
- `ps2recomp-perf-gs-vu1.patch`: rendimiento del GS por CPU y del VU1 sin cambiar la imagen. El rasterizador va en su
  propio hilo (`PS2X_GS_THREAD=0` lo desactiva) y reparte por bandas de 8 filas entre varios hilos las primitivas cuyo
  destino no solapa la textura (`PS2X_GS_RASTER_THREADS=N`); prueba Z antes de leer la textura; los triangulos solo
  recorren el tramo que cubren sus aristas; el VU1 recorre solo las entradas validas de sus colas. Con un flujo del GS
  grabado en el nivel 1: 8,8 s -> 4,9 s, con las 38 imagenes y la VRAM final identicas bit a bit.
- `ps2recomp-vu1-fast.patch`: mas rendimiento del VU1 y del IOP sin cambiar la imagen. El VU1 escribe los inmediatos
  sin pasar por la ruta general, guarda la decodificacion de cada microinstruccion, salta los eventos que no cambian
  nada y calcula las banderas MAC por la via rapida; el IOP recorre solo los hilos vivos. Comprobado con un flujo del
  VU1 grabado y reproducido: salida identica. Menu de ~25 a ~37 VSync/s y nivel 1 de ~18 a 30-37 VSync/s.
- `ps2recomp-ee-idle-iop.patch`: con todos los hilos del EE parados, `EeScheduler::waitForEvent` esperaba al siguiente
  plazo (el VSync, en tiempo real) y solo entonces daba esos ciclos al IOP de golpe: cada peticion al IOP desde un hilo
  que se queda esperando (lectura de CD por GTFSCDVD, RPC) costaba un VSync entero. Ahora adelanta el tiempo en pasos de
  32 768 ciclos del EE, haciendo correr al IOP, y vuelve en cuanto hay algo que atender en el EE (evento, invocacion SIF,
  hilo listo o temporizador). Si nada despierta, espera al plazo como antes (mismo ritmo en tiempo real).
- `ps2recomp-fpu-vu0-r5900.patch`: aritmetica del R5900 en el traductor y el runtime (hay que regenerar el codigo).
  - `SQRT.S fd, ft`: en el R5900 el operando va en el campo ft (fs no se usa, como en PCSX2); el traductor leia fs, asi
    que cada raiz cuadrada con ft != fs (63 en el ejecutable: `RwV3dLength`/`RwV3dNormalize` de RenderWare y codigo del
    juego) calculaba la raiz de otro registro.
  - `CVT.W.S` trunca siempre (GCC lo usa para `(int)f`) y satura si no cabe; el runtime redondeaba al mas cercano
    (`nearbyintf`). 287 usos.
  - Macros de VU0: `VDIV`/`VSQRT`/`VRSQRT` con la aritmetica de la FPU (divisor cero = +-max, raiz de |ft|) en vez de
    dar 0; `VRSQRT` calcula fs/sqrt(ft) (antes 1/sqrt(ft), ignoraba fs).
- `ps2recomp-dispatch-unwind.patch`: `dispatchGuestBranch` tomaba "el llamado vuelve con pc == su entrada" por "volvio
  sin tocar pc" y seguia en la vuelta. Con una llamada recursiva (A -> ... -> jalr A) interrumpida justo en la entrada
  interna por un punto seguro del planificador, el pc es la entrada de A y el llamador externo seguia con el sp y el ra
  del marco interno: salto a la pila (0x1FFF990) y el hilo principal moria. Ahora una marca indica que se esta
  deshaciendo la pila hacia el planificador y los llamadores devuelven `false` hasta llegar a el.

Arreglos del juego (sin parche, en `runner/blade_overrides.cpp`):
- Cache de texturas de RenderWare (0x1F39E0): en el modo de video 38 (640x512, entrelazado + FSAA) el driver calcula el
  inicio de la cache pasado el final de los 4 MB y un tamano negativo; el override da la vuelta al inicio (pagina 48) y
  acaba la cache donde empieza el primer buffer de dibujo (pagina 160). Ver "Pendiente 2" en Estado.

Herramientas propias (sin parche: `runner/blade_devtools.cpp`, solo se activan con variables de entorno):
- `BLADE_DUMP_FRAMES=carpeta[,cada_N[,desde]]`: guarda en PNG la imagen presentada cada N VSync (`frame_TTTTTT.png`;
  si los VSync van a saltos, al cruzar cada multiplo de N, con el VSync real en el nombre).
- `BLADE_INPUT="300:START;420+20:CROSS;500:UP,CROSS"`: pulsa botones del mando 1 en VSync concretos
  (`TICK[+DURACION]:BOTONES`, duracion por defecto 6; tambien `LX=`/`LY=` para el stick izquierdo).
  Cada paso se mantiene al menos 150 ms reales aunque los VSync corran mas que el sondeo (con el EE ocioso el tiempo
  avanza a saltos).
- `BLADE_DUMP_RAM=carpeta,VSync[,VSync...]`: guarda los 32 MB de RAM del EE en cada VSync indicado (`ram_TTTTTT.bin`)
  y escribe en la salida el estado de los hilos del EE (`entry`, `pc`, `ra`, `sp`, espera) y
  de los semaforos (`[blade:kernel]`); con `sp` se recorre la pila en el volcado.
- `BLADE_EXIT_TICK=N`: cierra el proceso en el VSync N (pruebas sin ventana atendida).
- `BLADE_HIDDEN=1`: ventana oculta y sin foco (pruebas en segundo plano; los PNG se siguen guardando).
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
- Bloqueo 8 (resuelto, LOADING sin fin): durante la escena de entrada el hilo de carga (0x16B0A0, peticion en
  0x305F50: `L1\L1.BIN`, 6,2 MB) lee el resto del nivel en segundo plano mientras el hilo principal dibuja la escena;
  el letrero LOADING sigue hasta que acaba. Iba a un sector (2 KB) por VSync: el fichero (0x334E00, posicion en +4)
  pasaba de 0x30000 a 0xA4800 en 200 VSync, unos 2 600 VSync para todo el fichero. Causa: con los dos hilos del EE
  esperando (el principal duerme en 0x16B040 hasta que le despiertan; el de carga espera la lectura de GTFSCDVD), el
  planificador del EE dormia hasta el siguiente VSync y solo entonces daba los ciclos al IOP, asi que cada lectura
  costaba un VSync. Parche `ps2recomp-ee-idle-iop.patch` (ver Parches). Con eso la carga acaba en unos cientos de
  VSync y la escena termina en "X CONTINUE". (El estado del flujo de audio no tenia que ver: el objeto de la escena
  sale de ella cuando `0x131E20` ve el flujo parado o con el tiempo hacia atras; el flujo 1 sigue en estado 4.)
- Bloqueo 9 (resuelto): al pulsar X en CONTINUE el hilo principal moria (`[guest-branch:missing-target]`, `jalr` desde
  0x1918B8 a 0x232320, tercer metodo de una tabla con 0x232D20 y 0x2328B0). Ghidra no creo esa funcion (0x590 bytes,
  pegada al `jr $ra` de 0x23231C y solo alcanzable por puntero). `filter_ghidra_map.py` acepta ahora huecos de hasta
  0x4000 bytes que terminan en `jr $ra` + delay slot (+ relleno) justo antes de la funcion siguiente; recupera 0x232320
  y 0x1E0510 (un `switch` al que apunta 0x2E4DD4). Un barrido de los punteros de los datos a codigo fuera de funciones
  no encuentra mas.
- Hasta donde llega ahora: la escena de entrada del nivel 1 se dibuja entera (hito 11) y la partida se juega: el mundo
  del nivel se ve (calles, edificios, cielo; hito 15), la camara sigue al movimiento del stick por las calles y
  callejones (hitos 16 a 18), CUADRADO da un truco que suma 200 puntos, el tiempo baja de 2:30 y salen los avisos del
  retrato ("X to jump further!", "GCP heavies are on the street"). Sin cuelgues ni hilos muertos hasta el VSync 5500
  (tiempo 1:31), unos 6 minutos reales desde el arranque. No se ha llegado al final del nivel.
- Pendiente 1 (resuelto, mundo negro en la partida): desde el VSync ~2650 (fin de la escena) solo se veia el HUD. Las 86
  tiras por imagen salian con todos los vertices en el centro de la pantalla y q = FLT_MAX porque la matriz camara x
  proyeccion que el EE sube al VU1 (qwords 0x3F0-0x3F3 del bloque de constantes, `UNPACK V4-32 num=13 addr=0x3F0`)
  llegaba a cero. Siguiendo hacia atras (RwCameraBeginUpdate: `viewMatrix` en +0x20 de la camara, LTM del marco en
  +0x50, `RwMatrixMultiply` en 0x1C7340): el marco de la camara de juego tenia la parte de rotacion a cero, porque el
  controlador de la camara la construye normalizando vectores con FPU y VU0, y tres instrucciones se traducian mal:
  - `SQRT.S fd, ft`: en el R5900 el operando esta en el campo ft (fs no se usa); el traductor leia fs (normalmente 0),
    asi que toda raiz daba 0 y cada normalizacion, 0 o infinito. Afectaba a 27 archivos generados.
  - `CVT.W.S`: el R5900 trunca y satura a INT32_MIN/INT32_MAX (como PCSX2); el runtime redondeaba al mas cercano
    (`nearbyintf`) y desbordaba con valores grandes.
  - Macros de VU0 `vdiv`/`vsqrt`/`vrsqrt`: deben escribir el registro Q (con las reglas de division por cero del
    R5900, +-MAX, y 0/0 = 0 con signo en `vrsqrt`), que es lo que lee el `vmulq` siguiente; `vrsqrt` calcula
    fs/sqrt(ft), no 1/sqrt(ft).
  Arreglo en `ps2recomp-fpu-vu0-r5900.patch` (traductor + `ps2_runtime_macros.h`) y codigo regenerado. Con eso la matriz
  sale bien en todas las imagenes y el mundo del nivel 1 se ve (hito 15).
- Bloqueo 10 (resuelto): en la primera imagen de la partida el hilo principal moria con `[guest-branch:missing-target]`
  (salto a 0x1FFF990, basura de la pila). Cadena recursiva 0x183BB0 -> 0x183C40 -> 0x184340 -> `jalr` 0x183BB0. El
  planificador expropio el hilo en un punto seguro dentro de la llamada interior; al volver, el `dispatchGuestBranch`
  exterior vio `pc == entryPc` (la misma funcion, por la recursion), creyo que la llamada habia terminado y siguio en
  la instruccion siguiente con `sp` y `ra` del marco interior. `ps2recomp-dispatch-unwind.patch` marca en un indicador
  por hilo que se esta deshaciendo la pila hasta el planificador; mientras esta puesto, ningun `dispatchGuestBranch`
  continua en la instruccion siguiente: todos devuelven hasta el planificador, que reanuda en el `pc` guardado.
- Pendiente 2 (resuelto, menu corrupto y banda de basura arriba en la escena del nivel): cada imagen el menu sube el
  video de fondo (MENUBASE.M2V, 640x512) como 40 trozos CT32 de 128x64 en los bloques 1536..6655 y las fuentes detras
  (hasta ~8032), pero los buffers de pantalla estan en las paginas 160 y 320 (bloques 5120 y 10240): los trozos 28-39 y
  las fuentes se pisaban con el dibujo. Causa (en el juego, no en el runtime): el juego elige el modo de RenderWare 38
  (0x22B7D0; tabla de modos en 0x2E46D0: 640x512, 32 bits, flags 0x13). El arranque del video del driver sky2
  (0x1E8B00-0x1E9600) llama a `sceGsSetDefDBuffDc` y, por ser entrelazado, desplaza FBP/ZBP 160 paginas: dibujo en las
  paginas 160 y 320 (entornos en 0x32ED10/0x32EE80) y Z16S en la 480, que da la vuelta a las 0-47. Guarda el final de
  la VRAM ocupada sin dar la vuelta (gp-0x7890 = 0x118000 palabras, pagina 560) y desde 0x1E95C8 inicia la cache de
  texturas (0x1F39E0, inicio y tamano en palabras) con tamano 0x100000 - inicio: negativo. La cache cree tener casi
  4 GB, nunca vuelve al principio y sus direcciones, truncadas a 14 bits en BITBLTBUF/TEX0, avanzan desde la pagina 48
  hasta pasar la 160. Override en `runner/blade_overrides.cpp`: si el inicio pasa de 4 MB o el tamano es mayor, da la
  vuelta al inicio y acaba la cache en el primer buffer de dibujo (paginas 48-159). Con eso las subidas llegan como
  mucho al bloque 5119; menu principal, seleccion de partida, un jugador, modo historia y la escena "hideaway" del
  nivel 1 salen limpios (antes con trozos del video viejos y basura de fuentes arriba). Falta saber por que en la
  consola no pasa: con el mismo codigo, algun dato de entrada del calculo (p. ej. algo que el runtime devuelve al
  driver al elegir el modo) debe de ser distinto; el override no depende de eso. El intercambio de buffers (`sceGsSwapDBuffDc`) es correcto (igual que la
  libgraph del juego en 0x101B30).
- Graficos sin revisar contra una consola: la columna de paneles blancos a la derecha del menu son abanicos sin textura
  `rgba=255,255,255,128` con `ALPHA=0x48` (Cs*As + Cd, aditivo): el blanco saturado es lo que pide el juego. En la
  escena de entrada del nivel 1 un edificio sale como silueta negra (geometria del VU1). El mundo negro de la partida era
  la matriz de camara nula (Pendiente 1, resuelto en la FPU/VU0), no el GS.
- Pendiente 4 (jugador invisible): en la partida no se dibuja el personaje (ni la tabla), aunque el mundo, el HUD y las
  estelas del boost si salen. Sin investigar. Hipotesis: la camara de juego queda dentro o muy cerca del jugador (en un
  volcado de RAM, posicion y objetivo del controlador de camara, estructura en 0x1EC9E68 con objetivo en +0x58, estaban
  a ~1.17 unidades) o el pipeline con piel (RpSkin, matrices de huesos por VU1) da geometria fuera de pantalla; otra
  instruccion de FPU/VU0 mal traducida es sospechosa, igual que en Pendiente 1.
- Pendiente 3: rendimiento. En el nivel van unos 6 VSync por segundo real (14 en el menu). Perfil de 10 s con `sample`
  durante la entrada: el rasterizador por CPU (`SampleTexture` + `WritePixel` + `Submit` + `ReadTexture`, unas 4 900
  muestras) y el interprete de VU1 (`commitReadyPipelines`, `calculatePairReadyCycle`, `execUpper`..., unas 2 800) se
  llevan casi todo; el codigo recompilado del EE apenas aparece. El SPU2 del IOP no mezcla audio, asi que no se oye la
  musica ni los efectos de LIBSD/TSSDRV.
- Metodo tomado de [hctp-recomp](https://github.com/BigDaddyJRich/hctp-recomp) (WWE SmackDown! HCTP con PS2Recomp:
  Ghidra + emotionengine-reloaded + inicios recuperados), que tambien ejecuta los IRX propios del juego.
- Otras funciones de libcdvd sin nombre en el mismo bloque (0x113070, 0x1132A8, 0x113340, 0x113470, 0x1137D8, 0x113AF0): si
  alguna vuelve a esperar al IOP, se identifica igual (SID y numero de funcion RPC) y se sustituye por su stub.
