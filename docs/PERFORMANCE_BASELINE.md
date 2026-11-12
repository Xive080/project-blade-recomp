# PERFORMANCE_BASELINE (QA independiente)

Fecha: 2026-11-11. Equipo: Mac arm64, 8 nucleos, compartido con 3 desarrolladores (carga del sistema 8-20 en las
medidas "carga baja" y 50-100 en las "carga alta"; la carga se anota en cada fila). Build: Release, `build/qa`.
Referencia = commit 795b79a + `ps2recomp-mmi-permute.patch` (sin commitear) + restos de depuracion del dev 1 (inertes).
Guion: `BLADE_INPUT="300:START;400:START;500:START;600:START;700:START;900:CROSS;1050:CROSS;1200:CROSS;2250:CROSS;2400:CROSS"`,
`BLADE_HIDDEN=1`. Herramientas: `scripts/qa_regression.sh`, `scripts/qa_frametimes.py`, `scripts/qa_sample_split.py`.
Los artefactos estan en `qa/` (run_baseline1, cpu_base, ft_menu2, ft_level2, ...).

## 1. VSync/s (simulacion) de la linea base
| Tramo (VSync) | Escena | Carga baja (8-20) | Carga alta (60-100) |
|---|---|---|---|
| 650-1100 | menu principal / un jugador | 34,8 V/s | 8-10 V/s |
| 1250-1750 | pantalla "hideaway" (carga en segundo plano) | 36,4 V/s | 7-10 V/s |
| 1850-2250 | escena de entrada (interior) | 13,4 V/s | 3,6-4,9 V/s |
| 2300-2900 | partida, camara por las calles | 7,5 V/s | 1,9-2,7 V/s |
| 3000-3450 | partida | 12,7 V/s | 2,7-3,7 V/s |
Total 0-3500 VSync: 217,9 s reales (16,1 V/s medios) con carga baja; 0-3950: 967,6 s (4,1 V/s) con carga alta.
Objetivo PAL: 50 V/s. **La partida corre a 0,25 x tiempo real con carga baja.**

## 2. Tiempo de CPU (muestra de 8-10 s con `sample`, VSync ~3100, carga 13)
| Hilo | Uso | Detalle |
|---|---|---|
| EE (`GameThread`) | 97,6 % de un nucleo (saturado) | VIF1/DMA 66 % (VU1 interprete 61 %); espera de RenderWare 0x1DF500 28 % (incluye el IOP); IOP `runCycles` 18 %; temporizadores EE 3 % |
| IOP | sin hilo propio: se ejecuta dentro del hilo del EE | ver arriba |
| GS (hilo del rasterizador) | 83,7 % de un nucleo (92 % de las muestras activas) | `SubmitMany`/`DrawPrimitive`/`SampleTexture`/`WritePixel` |
| 3 hilos de bandas del GS | 8-19 % cada uno | |
| Hilo principal | ocioso, espera a `waitIdle` del GS | |
Proceso entero (`time -l`, 0-3500 VSync): 460 s usuario + 23 s sistema en 217,9 s reales = 2,2 nucleos.
Memoria maxima: 2,6 GB (run_baseline1), 6,6 GB (cpu_base), 7,8 GB (run_dev2): **varia 3x sin explicacion** (AB-015).
GPU: el GS es por CPU; la GPU solo presenta la textura. UNVERIFIED (no medida).

## 3. Distribucion de tiempos de VSync (volcado de cada VSync; incluye el coste del PNG, ver limitaciones)
| Escena | VSync medidos | media | mediana | minimo | mejor 1 % (p1) | peor 1 % (p99) | maximo | V/s medio | 1 % low |
|---|---|---|---|---|---|---|---|---|---|
| Menu 700-1000 (carga ~17) | 299 | 33,6 ms | 33,8 | 14,6 | 15,1 | 71,9 | 75,6 | 29,8 | 13,9 V/s |
| Partida 3000-3300 (carga ~20) | 299 | 70,0 ms | 69,0 | 45,3 | 47,0 | 128,5 (p99 111,7) | 128,5 | 14,3 | 9,0 V/s |
Con carga alta (carga ~55): menu 125 ms de media (8,0 V/s, 1 % low 3,8 V/s); partida 83 ms (12,0 V/s, 1 % low 3,9).
Limitaciones: el instrumento no es el juego sino el instante en que aparece el PNG (mtime), con un hilo de volcado que
sondea cada 250 us y escribe 640x512 PNG por VSync, lo que puede bajar los V/s a tasas altas. Los tiempos son de la simulacion, no del
refresco de la ventana.

## 4. Modelo de tiempo PAL
- El cronometro del HUD baja 18,00 s entre los VSync 3000 (2:19,44) y 3900 (2:01,44): **50 VSync por segundo de juego**
  (comprobado tambien en 2500-2950: 9,0 s en 450 VSync). El juego es PAL (50 Hz).
- El runtime de la linea base fija el VBlank del EE en 16 667 us (`EeScheduler.cpp`, `kVBlankPeriod`): 60 Hz. A velocidad
  completa el juego iria 1,2 veces rapido (musica, FMV, cronometro). A las velocidades actuales va lento (0,25-0,7 x) y el
  efecto contrario domina. Pendiente de arreglo: dev 2 (`ps2recomp-pal-vblank.patch`).
- Relacion cronometro de juego / tiempo real en la partida (carga baja): 50 VSync = 1 s de juego; a 12,7 V/s son 0,25 s de juego por
  segundo real.

## 5. Dev 2 (rendimiento + PAL) - ver QA_REPORT.md
| Medida (build dev 2, carga 8-25) | Valor |
|---|---|
| Menu/arranque 0-1500 VSync, con override `0x1DF500` | 50,0 V/s estables (2 ejecuciones, contador propio `[blade:stats]`) |
| Mismo recorrido con `BLADE_NO_SKYWAIT=1` | 33,5 V/s |
| Intro/partida (log de `qa/run_dev2`) | 11-25 V/s (intro 11-12; resto 14-25) |
| Muestra de CPU en partida (escena fija del cronometro 2:30) | VU1 84 % del hilo EE; espera 0x1DF500 y IOP ya no aparecen (<2 %) |
El `[blade:frames]` del dev 2 da media 45,1 ms (22,2/s), minimo 1,2 ms y p1 137 ms en 3947 VSync: coherente con lo anterior.
No he repetido la medida de distribucion por VSync con el volcado en el build del dev 2 (UNVERIFIED).

## Modo determinista (ronda 2)
Con PS2X_DETERMINISTIC=1 no hay ritmo de reloj de pared: 3950 VSync en 354 s con la maquina libre (11 V/s, el limite lo pone el coste de simulacion/GS, no el pacing). Las cifras de V/s en tiempo real de arriba siguen siendo la referencia de rendimiento; el modo determinista es para reproducibilidad, no para medir fluidez. Dos corridas: RAM y fotogramas identicos.

## Build dev 2 con vu1-record/vu1-z-compiled (ronda 2)
Binario: qa/blade_recomp.vu1gen (serie de 19 parches sobre e563698, sin shims, codigo VU1 compilado con la receta del README a partir de vu1b.rec). Carga del Mac al empezar 16 y al acabar 11,6 (load average 1 min), modo normal, BLADE_STATS=1 y volcado PNG cada VSync (que anade coste), guion BLADE_INPUT de la linea base, 3500 VSync en 110 s (media 32 V/s).
- Menu (650-1100): 50,0 V/s (limite PAL), 1%low 27,4 V/s, p99 36,4 ms/VSync.
- Partida (2600-3500): 22,1 V/s de media, 1%low 10,7 V/s, mediana 43 ms, p99 93,6 ms.
- Linea base anterior: menu 35 V/s, partida 12,7 V/s (carga distinta, no estrictamente comparable).
Replay VU1: vu1b.rec 20000/20000 identicas (0,95 s con compilado, 1,69 s con PS2X_VU1_NOGEN=1), vu1.rec 6000/6000; negativa vu1_bad.rec: DIFERENCIA en 2964 y 2965 (5998 identicas, 2 distintas).

## Linea base limpia AB-001 (2026-11-12, commit 1ac41fd + banco diferencial)
Binario `build/blade/blade_recomp` (Release, ninja), **sin VU1 compilado** (`PS2X_VU1GEN_DIR` vacio): interprete puro del VU1. Mac arm64 de 8 nucleos en reposo
(load average 2-3 al empezar; solo la sesion de Claude y Chrome, ~25 % de un nucleo entre los dos), una sola corrida a la vez, ventana oculta.
Modo normal (reloj de pared), `BLADE_HIDDEN=1 BLADE_STATS=1 BLADE_EXIT_TICK=3950` y el guion de la linea base
`BLADE_INPUT="300:START;400:START;500:START;600:START;700:START;900:CROSS;1050:CROSS;1200:CROSS;2250:CROSS;2400:CROSS"`
(el mismo que `runner/README_DETERMINISTIC.md` usa en modo determinista; el nivel 1 es jugable desde ~2500). Un unico proceso; sin volcado de PNG.

### VSync/s (de `[blade:stats]` y `[blade:frames]`, un registro por segundo real)
| Tramo (VSync) | s reales | V/s medio | minimo / maximo (1 s) | fotograma medio | fotograma minimo | p1 % (peor 1 %), medio de los segundos |
|---|---|---|---|---|---|---|
| 650-1100 (menu) | 10 | 50,0 | 50 / 50 | 20,0 ms | 12,4 ms | 24,5 ms (peor 28,0) |
| **2600-3900 (partida, nivel 1)** | 114 | **11,4** | **9 / 15** | 87,8 ms | 66,8 ms | 92,3 ms (peor 163,6) |
| 2600-3000 | 39 | 10,1 | 9 / 12 | 98,8 ms | 78,5 ms | 102,4 ms |
| 3000-3500 | 41 | 12,3 | 10 / 15 | 81,6 ms | 66,8 ms | 87,3 ms (peor 163,6) |
| 3500-3900 | 34 | 11,8 | 11 / 12 | 84,8 ms | 83,3 ms | 86,8 ms |
Toda la pasada (0-3949): fotograma medio 68,3 ms (14,6/s), minimo 6,6 ms, p1 % 212,4 ms. Objetivo PAL 50 V/s (20 ms): hay que dividir el fotograma de la partida por ~4,4.
Segunda corrida con el VU1 compilado de la ruta `BLADE_VU1_GEN` (generado de la grabacion del apartado siguiente, 0x0-0x1f00, 2 imagenes por tramo; `PS2X_VU1GEN_DIR` apuntando a `work/vu1gen`,
build 55 s): 2600-3900 **14,6 V/s** de media (minimo 11, maximo 18, fotograma 68,7 ms, p1 % medio 72,6 ms), es decir **x1,28** frente al interprete (11,4); menu 50. Despues se restauro el build sin VU1 compilado.
Las corridas estan en el rango de lo medido antes (10-15 V/s en esos tramos con carga baja); la cifra de 22 V/s de la tabla "Build dev 2" no se reproduce aqui (ver mas abajo, cobertura).

### Reparto de CPU por hilo (`sample <pid> 10` hacia el VSync ~3200-3400, interprete puro; % del tiempo de un nucleo en esos 10 s, solo muestras fuera de espera)
| Hilo | Uso | Detalle |
|---|---|---|
| EE (`GameThread`; el IOP corre dentro) | **100 %** | `FUN_001dd730` (DMA de VIF1 por escritura a registro) 95 %; de ahi `processVIF1Data` 85 % y las llamadas al VU1 (`resume` 6 113 + `execute` 567 muestras de 7 231) = **~92 % del hilo EE es el interprete del VU1**; GIF/`Submit` <2 %, `UploadImage` ~0 %, IOP <2 % |
| Rasterizador del GS (hilo propio) | 26,5 % | `sampleT<19>`, `DrawTriangle`, `WritePixelFast` |
| 3 hilos de bandas del GS | 18,1 / 19,3 / 19,3 % | igual reparto |
| Hilo principal (presentar) | 6,7 % | `PresentFromLocalMemory`, `mach_absolute_time` |
| Audio y resto | <1 % | |
Total del proceso (`top`): 194 % de un nucleo (~1,9 nucleos); el cuello es el hilo EE al 100 %, el GS va holgado. Con el VU1 compilado (muestra aparte, 7 901 muestras, 14,6 V/s) el EE sigue al 100 %
y el VU1 es el ~87 % de el (6 847 muestras de funciones `VU1*` incluidos los trozos compilados), y el GS sube a 42,5 % + 25-28 % por banda (el hilo EE alimenta mas deprisa).
**Las cifras antiguas de este fichero y del brief (UploadImage 13 %, Submit 10 %, GS al 90 %) ya no son ciertas en este build**: UploadImage no aparece (1 muestra) y Submit/GIF suman ~1-2 % del EE.

### 10 principales sumideros de CPU (suma de todos los hilos, % de un nucleo en 10 s; interprete puro)
| # | Funcion | % | Hilo |
|---|---|---|---|
| 1 | `VU1Interpreter::execUpper` | 37,3 | EE |
| 2 | `VU1Interpreter::run` (bucle y paso por par, inlinado) | 28,1 | EE |
| 3 | `sampleT<19>` (textura del rasterizador) | 25,8 | 4 hilos GS |
| 4 | `GSCpuBackend::DrawTriangle` | 21,7 | 4 hilos GS |
| 5 | `GSCpuBackend::WritePixelFast` | 17,2 | 4 hilos GS |
| 6 | `VU1Interpreter::commitReadyPipelinesSlow` | 9,9 | EE |
| 7 | `VU1Interpreter::execLower` | 6,7 | EE |
| 8 | `VU1Interpreter::getDecodedInstructionPairForPc` | 6,2 | EE |
| 9 | `combineTexture` | 4,0 | GS |
| 10 | `sampleT<0>` | 2,9 | GS |
Las funciones 1, 2, 6, 7 y 8 (todo en el hilo EE) suman el 88 % de ese hilo. Con el VU1 compilado: `sampleT<19>` 35,7, `DrawTriangle` 30,9, `WritePixelFast` 25,7, `execUpper` 15,9 (pares que caen al interprete),
`VU1Gen::chunk<1>` 13,6, `run` 12,6, `commitReadyPipelinesSlow` 9,7, `chunk<2>` 7,5, `chunk<3>` 7,1, `sampleT<0>` 6,4.
Muestras (para repetirlas): `sample <pid> 10 -file ...` con la corrida en marcha; el analisis esta en la sesion, no en el repo (arbol de llamadas con auto-tiempo por hilo).

### Inventario del VU1 en el nivel 1
Herramientas: `PS2X_VU1_PROFILE=1` (perfil por (PC de arranque, hash de 16 KB), acumulado cada 30 s, se resta entre informes) y el banco diferencial `BLADE_VU1_DIFF` (ver README), que agrupa por **contenido ejecutado**
(PC de entrada + hash de los pares que de verdad se ejecutan; imagenes de 16 KB que solo difieren fuera de ellos cuentan como el mismo microprograma). Ciclos = ciclos del VU1 del propio modelo.

**Ventana completa del nivel (VSync 2556-3903, `PS2X_VU1_PROFILE`, 1 347 VSync):** 111 089 llamadas `execute` + 1 090 063 `resume` (~890 llamadas por VSync); 2,41 G ciclos (1,79 M por VSync) en 111,6 s de CPU del VU1 = 83 ms por VSync (`~46 ns` por ciclo del VU1;
a 50 V/s harian falta 89 M ciclos/s y ~12-13 ms por VSync para el VU1 si el resto del EE cabe en ~7 ms: **~6,5x en el VU1**). 13 PC de arranque distintos, 60 imagenes de 16 KB distintas, 70 filas (PC, imagen) con ciclos,
90 % de los ciclos en 12 filas. Reparto por PC: 0x1810 33,8 %, 0x1e98 28,5 %, 0x1e58 12,5 %, 0x0000 10,4 %, 0x1ee0 8,3 %, 0x2da8 4,1 %, 0x0388 1,2 %, resto 1,2 %. Los PC 0x1810/0x1e98/0x1e58/0x1ee0/0x2da8 son **reanudaciones** (`resume`/MSCNT):
el programa entra una vez por 0x0000 y vuelve a su bucle en esos puntos; casi todo el coste esta en las reanudaciones (1,09 M frente a 111 k `execute`).

**Muestra de 30 000 llamadas consecutivas (grabadas hacia el VSync 2830, ~60 fotogramas; `work/vu1gen/nivel.rec`, 152 MB, no versionado):** 36 filas (entrada, imagen), **32 microprogramas distintos por contenido ejecutado**.
Los 8 primeros son el 96 % de los ciclos; los 3 primeros el 80 %:
| PC entrada | pares distintos ejecutados | llamadas | ciclos | % | acum. % | imagenes de 16 KB |
|---|---|---|---|---|---|---|
| r1810 | 562 | 13 956 | 28 141 402 | 44,4 | 44,4 | 1 |
| r1e98 | 297 | 6 045 | 17 183 734 | 27,1 | 71,5 | 1 |
| r1e58 | 173 | 2 385 | 5 510 610 | 8,7 | 80,2 | 2 |
| 0000 (execute, imagen A) | 1 148 | 2 019 | 3 974 419 | 6,3 | 86,5 | 1 |
| r2da8 | 280 | 930 | 2 296 263 | 3,6 | 90,1 | 1 |
| 0000 (imagen B) | 1 148 | 144 | 2 282 249 | 3,6 | 93,7 | 1 |
| r1e98 (variante) | 297 | 2 494 | 1 023 314 | 1,6 | 95,3 | 1 |
| r0388 | 92 | 217 | 674 405 | 1,1 | 96,4 | 2 |
| r0168, 0000 (x18 imagenes), r1ee0, r1cd8, r0ae0, r27b8, r2ac0 | 46-1 148 | 2-450 c/u | <0,5 % c/u | 3,6 en total | 100 | |
(Todas las filas, con sus hashes, salen de `BLADE_VU1_DIFF=work/vu1gen/nivel.rec`; "r" = reanudacion. Cada llamada ejecuta ~1 500-3 000 ciclos: son bucles por lotes de vertices de 160-560 pares, no programas largos. El punto de entrada 0x0000 cubre ~20 imagenes
distintas (programas cargados por el juego segun el objeto), la mayoria con muy pocas llamadas.)

**Que hace ya la ruta `VU1_GEN` y por que no basta** (medido con la grabacion de arriba y el build con VU1 compilado):
- Genera los pares de 0x0-0x1f00 (992 pares, 16 trozos de 512 bytes en unidades de compilacion separadas) para las 2 imagenes de ese tramo mas usadas (de 15 que aparecen en la muestra: 15 975 y 6 076 llamadas = 73,5 % de las llamadas);
  las otras 13 imagenes (26,5 % de las llamadas) y todo lo que esta fuera de 0x0-0x1f00 (0x2da8, 0x1ee0 completo...) caen al interprete: **14,1 M de pares interpretados** dentro de la muestra (~22 % de los 63,4 M de ciclos).
- Cada par compilado es el mismo paso por par del interprete con la decodificacion como literal y un guarda de 8 bytes (`stepPairT<true>`): sigue pagando la cola de banderas MAC/estado/clip por FMAC, el modelo de ciclos y las tuberias (`commitReadyPipelines*`, `advanceOneCycle`), sin asignacion de registros ni eliminacion de banderas muertas.
- Resultado: replay de la muestra 3,05 s (interprete) -> 2,12 s (compilado), x1,44; en el juego **x1,28** (11,4 -> 14,6 V/s) y el VU1 sigue siendo ~87 % del hilo EE. No alcanza: faltan ~4,4x en total y ~6,5x en el VU1.
- Identico bit a bit: banco diferencial interprete contra compilado, 30 000/30 000 llamadas sin diferencia en registros, ciclos, memoria de datos y paquetes GIF.

## Banco diferencial del VU1 (2026-11-12)
`BLADE_VU1_DIFF` (patch `ps2recomp-zzz-vu1-diffbench.patch`, gancho en `runner/blade_devtools.cpp`; ver README "Banco diferencial del VU1"). Sin la variable, el modo normal no cambia
(solo se anaden una bandera global y dos comprobaciones de puntero nulo, una por par en el bucle del interprete y otra por paquete GIF si hay captura).
Pruebas hechas (30 000 llamadas del nivel 1, ~6 s por corrida, un solo hilo):
- Interprete contra si mismo (B = interprete): 30 000 comparadas, 0 diferencias, 0 casos en que A no reproduce lo grabado. Interprete contra codigo compilado real: 30 000/30 000 identicas.
- Fallo inyectado (en la salida de B, no en el compilador: prueba el comparador): vf, vi, acc, mac, status, clip, pc, cycles, mem, gif (un bit del primer paquete) y gifdrop: las 11 clases se detectan,
  salen con codigo 1 y nombran el campo, la llamada, el PC y la imagen (p. ej. `vf05.z A=47402f87 B=47402f86`, `paquete 0 distinto (byte 16; ... NLOOP=16 EOP=1 PRE=1 PRIM=05c ...)`).
Sin verificar: un fallo real dentro de un compilador (aqui el compilado coincide, asi que no hay ninguno que detectar), imagenes y llamadas fuera de la muestra, y comparacion contra PCSX2.

## M0 medir: techo del GS, corpus y histograma de pares (2026-11-12, sobre c6d6c69 + a1eedd5)
Binario `build/blade/blade_recomp` (Release, interprete puro del VU1, `PS2X_VU1GEN_DIR` vacio), Mac arm64 de **8 nucleos logicos (4 P + 4 E)**, una sola corrida a la vez, ventana oculta, `BLADE_STATS=1`, guion de la linea base.
Instrumentacion nueva, apagada por defecto y sin cambiar el modo normal: parche `patches/ps2recomp-zzzz-gs-bench-vu1-hist.patch` (`PS2X_GS_BENCH` en el hilo del GS y `BLADE_VU1_HIST` en el banco del VU1; la unica
huella en el modo normal es una comparacion mas con un puntero nulo por par en el bucle del interprete). Scripts: `scripts/vu1_record_corpus.sh` y `scripts/vu1_hist_report.py`.

### M0-b. Hilos del GS y techo del GS
**Hilos**: el GS usa 1 hilo propio (`GSThreadedBackend`, el "rasterizador": recibe las ordenes del EE por una cola y reparte las primitivas) + un `BandPool` de 4 bandas por filas (por defecto `clamp(nucleos/2, 1, 4)` = 4): el propio rasterizador hace de banda 0 y hay **3 hilos de bandas**. Son **4 hilos de trabajo** (mas el EE al 100 %, el hilo principal que presenta y el audio) en 8 nucleos logicos.

**Trabajo frente a espera** (`PS2X_GS_PROFILE=1`, nivel 1 2600-3900, 11,3 V/s con el perfil = igual que sin el: el contador no pesa). Por VSync, medianas de 114 intervalos (`[gsprof]` cada 2000 llamadas, restando acumulados):
| Tramo | CPU del hilo rasterizador | CPU de las 3 bandas (suma) | tiempo de pared del rasterizador dentro de `SubmitMany` |
|---|---|---|---|
| 2600-3900 (todo) | 18,2 ms | 35,3 ms (11,8 por banda) | **26,4 ms** (min 21,3, max 56,3) |
| 2600-2780 | 18,2 | 35,9 | 26,4 |
| 2780-2870 (escena pesada) | 34,3 | 59,5 | **46,7** (36,5-56,3) |
| 2870-3000 | 22,7 | 37,1 | 31,0 |
| 3150-3560 | 17,5 | 34,2 | 24,9 |
Es decir, de los 26,4 ms que el rasterizador esta ocupado por VSync, 18,2 son CPU propia (trabajo) y ~8,2 (31 %) es **espera de las bandas** (sale del bloqueo en `condition_variable::wait`, no del giro: no hay espera activa).
Comprobacion con `sample <pid> 10` hacia los ticks 3014-3146 (7 220 muestras por hilo; la muestra encarece el GS ~25 %, asi que solo valen los reparto, no los ms): el hilo del rasterizador esta **52 % esperando trabajo** (`cvwait` de la cola, EE-limitado),
y 46 % dentro de `SubmitMany`: 17,6 % su parte de banda 0 (`DrawRunParallel`), 13,7 % **esperando a que acaben las otras bandas**, 13,3 % **primitivas en serie** (dibujadas solo por el rasterizador con las tres bandas paradas). Las 3 bandas: 21-22 % trabajando
(`sampleT<19>` 35 %, `DrawTriangle`/`WritePixelFast` el resto) y 78 % durmiendo. Cifras que cuadran: espera de bandas 31 % (perfil) frente a 30 % (`sample`: 991/3 346).
Detalle del perfil por VSync (ticks 3048-3134): 209 llamadas a `SubmitMany`, 119 tramos paralelos con 41 Mpx y **~8 000 primitivas en serie** (3 Mpx, 12 ms de pared; todas por "textura solapa destino"; 0 por destino/z solapados).
El hilo EE esta al 100 % (11,3 V/s), asi que el GS **no es el cuello hoy**: 52 % de su tiempo esta ocioso. Lo sera cuando el EE suba de ~25-35 V/s.

**Prueba de techo** (GS aislado sobre las primitivas capturadas). Instrumentacion minima `PS2X_GS_BENCH=primer_Present,cuantos[,repeticiones[,paso,ventanas]]` (en `GSThreadedBackend`): desde el Present dado graba las ordenes que ejecuta el hilo del GS (con la VRAM de partida)
y, tras `cuantos` Present, las repite N veces **en el propio hilo del GS** sobre esa VRAM, sin EE ni VU1, y mide la pared (el juego se para durante la repeticion). Se descarto repetir en el hilo que llama a Present (47-56 ms/V en vez de 35: otro hilo, otros nucleos).
Es "el GS aislado", con el VU1 sin tocar y sin saltarse dibujos (`BLADE_RT_SKIP` solo omite un atomico, no cambia el coste del VU1 de forma util). Un Present es ~1 VSync en la partida (100 Present = 97 ticks).
Resultados (4 bandas, repeticiones estables a +-1 %; los ticks son aproximados porque el EE esta parado durante cada repeticion):
| Ventana (40 Present salvo la primera de 100) | primitivas por VSync | GS aislado, ms por VSync | techo del GS |
|---|---|---|---|
| ticks ~3043-3140 (100 Present) | 31 000 | **35,5** | **28 V/s** |
| ~2465-2503 (entrada al nivel) | 78 700 | 44,1 | 22,7 V/s |
| ~2693-2733 | 52 600 | 44,1 | 22,7 V/s |
| ~2937-2977 | 38 600 | 39,3 | 25,5 V/s |
| ~3197-3237 | 31 600 | 44,5 | 22,5 V/s |
| ~3464-3504 | 33 100 | 33,2 | 30,2 V/s |
| ~3687-3727 | 32 900 | 32,6 | 30,6 V/s |
Escalado con las bandas (misma ventana 3043-3140): **1 banda 58,4 ms/V, 4 bandas 35,5, 8 bandas 34,8**. Con T(n)=s+p/n salen s~28 ms de parte que no escala (primitivas en serie, preparacion por primitiva, subidas ~1 ms, sincronizacion) y p~30 ms paralelizable: pasar de 4 a 8 hilos no gana nada,
y con infinitas bandas el techo seria ~36 V/s. Las subidas y CLUT (resto de ordenes) son ~0,9-1,4 ms/V; las primitivas 32-43.

**Techo en V/s con el criterio explicado**: techo = 1000 / (ms que necesita el GS por VSync trabajando solo, con las 4 bandas). Con el GS aislado: **23-30 V/s** segun la escena (central ~28). Con el tiempo de pared del rasterizador dentro del juego en vivo (sin perfil de CPU, sin repeticion):
**26,4 ms de mediana = 38 V/s** (escena pesada 46,7 ms = 21 V/s). Las dos cifras NO cuadran dentro de +-15 % (35,5 frente a 26,4 ms en tramos parecidos: la repeticion es una carga sostenida de 4 hilos y puede estar limitada por reloj/termica; el vivo mide un GS que a veces descansa),
asi que doy el intervalo: **techo del GS actual ~23-38 V/s, mejor estimacion ~30 V/s, en todo caso por debajo de 50** (confirma que G1 va antes de P6 y que el 40-55 V/s estimado en el plan depende de bajar la parte serie del GS, no de mas hilos).
Lo que no pude medir: el techo con el VU1 realmente mas rapido (el EE es el cuello a 11-15 V/s, no hay forma de que el GS se sature sin acelerar el EE; el banco aislado lo sustituye), y variantes con 2 y 3 bandas; el efecto de la frecuencia/termica en la repeticion.

### M0-c. Corpus: 5 escenas del nivel 1
Grabadas con `scripts/vu1_record_corpus.sh` (`PS2X_VU1_RECORD=work/vu1gen/corpus/eN.rec,desde,15000`, una corrida oculta por escena, modo normal con el guion de la linea base; 15 000 llamadas = ~17 VSync, 75 MB cada una; `work/` y `*.rec` fuera de git).
El guion llega a las 5 zonas del recorrido (el recorrido es el del guion: el jugador avanza y la camara se pega a una pared despues de ~3500, asi que e4 y e5 son zonas de poco movimiento). Banco: `BLADE_VU1_DIFF_B=interp BLADE_VU1_HIST=1 BLADE_VU1_DIFF=eN.rec`: **15 000/15 000 comparadas, 0 diferencias, 0 no reproducibles en las 5** (2,6-3,5 s cada una).
| Escena | ticks reales | llamadas desde | microprogramas distintos (contenido ejecutado) | solo en esta escena (de las 5) | % ciclos de programas que NO salen en las otras 4 | ciclos VU1 | pares ejecutados |
|---|---|---|---|---|---|---|---|
| e1 | 2640-2657 | 1 100 000 | 26 | 15 | 12,4 % | 25,3 M | 20,5 M |
| e2 | 2810-2826 | 1 300 000 | 24 | 14 | 12,3 % | 31,5 M | 27,8 M |
| e3 | 3077-3093 | 1 550 000 | 25 | 14 | 14,1 % | 29,2 M | 24,7 M |
| e4 | 3467-3483 | 1 850 000 | 23 | 0 | 0 % | 33,6 M | 29,9 M |
| e5 | 3892-3908 | 2 150 000 | 23 | 0 | 0 % | 33,8 M | 30,1 M |
Union: **72 microprogramas distintos**; salen en las 5 escenas solo 4, en 4 escenas 1, en 3 escenas 6, en 2 escenas 18, **en 1 sola 43**. Pero el peso esta concentrado: los 8 primeros de cada escena son el 96,5-97,3 % de sus ciclos, y los PC de reanudacion (r0168, r0388, r0ae0, r1810, r1cd8, r1e58, r1e98, r27b8, r2da8;
r1ee0 en 4 escenas, r2ac0 en 2) estan en todas. Lo que cambia entre escenas es sobre todo el punto de entrada **0000** (programas por objeto, cada uno con pocas llamadas: 6-7 % de los ciclos cada uno de los dos mayores) y la ruta de ejecucion dentro de cada bucle.
Leave-one-out: las otras 4 escenas cubren el 87,6 % (e1), 87,7 % (e2), 85,9 % (e3), 100 % (e4) y 100 % (e5) de los ciclos de la escena que falta; lo que no cubren son programas de entrada 0000 (mayores 6,8 %, 4,1 % en e1; 6,2 %, 3,8 % en e2; 6,1 %, 6,1 % en e3).
Precaucion: la identidad del microprograma es "PC de entrada + hash de los pares que se ejecutan", asi que un mismo programa con otra ruta (otro recorte, otro bucle) cuenta como otro; por eso el "solo en una" (43 de 72) sobrestima los programas realmente nuevos (la tabla por imagen de 16 KB da 57 filas y 30 imagenes distintas, 3 filas en las 5).
**Conclusion para el generador**: un generador desde grabaciones cubre bien los ~12 bucles de reanudacion (>=85 % de los ciclos de cualquier escena con solo 4 grabaciones de otras partes) pero deja 10-15 % de ciclos en programas de entrada 0000 que cambian con la escena; hace falta que el generador compile por contenido del microcodigo
(todas las imagenes y rutas vistas, no solo las 2 mas frecuentes) y mantenga el interprete como respaldo, o desensamblar las imagenes desde el disco en lugar de depender de lo ejecutado. No he cubierto los niveles 2+ ni el menu (el corpus es solo nivel 1, ticks ~2640-3908).

**Regrabacion tras D1 (2026-11-12, build de 0ba0b06, oraculo FP sin FMA).** Las grabaciones de arriba se hicieron con el oraculo antiguo (con FMA) y ya no se reproducian (29 999/30 000 en `nivel.rec`, 15 000/15 000 en e1-e5). Regrabadas con `scripts/vu1_record_corpus.sh` (mismas ventanas: llamadas 1,10/1,30/1,55/1,85/2,15 M, 15 000 cada una; ticks 2654/2838/3103/3483/3896 al acabar; mismo guion) y `nivel.rec` con `PS2X_VU1_RECORD=work/vu1gen/nivel.rec,1300000,30000` y el mismo guion. Las antiguas quedan como `*.rec.old` (y `eN.diff.old.txt`), fuera de git.
Banco `BLADE_VU1_DIFF_B=interp BLADE_VU1_HIST=1` sobre las 6: e1-e5 **15 000/15 000 comparadas, 0 A!=B, 0 A!=grabado**; `nivel.rec` **30 000/30 000, 0, 0**. Microprogramas distintos por escena: **26, 24, 25, 23, 23; union 72; 4 en las 5, 43 en una sola: sin cambios** (misma tabla de arriba; los ciclos de e2-e5 coinciden al ciclo y e1 difiere en 13 557 ciclos de 25,3 M, 0,05 %: la corrida normal no es determinista al 100 % en el ritmo de los lotes).

### M0-d (parcial). Histograma dinamico de pares sobre el corpus
`BLADE_VU1_HIST=1` con el banco diferencial cuenta, por par realmente ejecutado por el interprete (A), la clase de tuberia y el opcode; `scripts/vu1_hist_report.py` lo resume. Las 5 escenas suman **133,0 M de pares en 153,5 M de ciclos** (1,16 ciclos por par: las esperas por riesgo/Q/P son ~15 % de los ciclos; por escena 1,12-1,24).
Reparto por mitad (% de los pares; escenas casi iguales, e1-e5 +-3 puntos):
| Mitad | Clase | Pares | % |
|---|---|---|---|
| superior | FMAC (ADD/SUB/MUL/MADD..., incl. ITOF/FTOI/CLIP/OPMULA) | 116,2 M | **87,3** |
| superior | NOP | 16,8 M | 12,7 |
| inferior | load/store (LQ, SQ, SQI, LQI, ILW, ISW...) | 70,1 M | **52,7** |
| inferior | IALU y banderas (IADDIU, IADD, FCAND, FCOR, MFIR...) | 25,9 M | 19,4 |
| inferior | branch (IBEQ, IBNE, IBGEZ...) | 15,6 M | 11,7 |
| inferior | **NOP** (se codifica como MOVE con dest 0; el recuento por opcode lo cuenta como MOVE) | 12,6 M | 9,5 |
| inferior | MOVE/MR32 reales (tuberia FMAC) | 2,7 M | 2,0 |
| inferior | FDIV/SQRT (DIV 3,5 %, SQRT/RSQRT el resto) | 5,2 M | 3,9 |
| inferior | otros (MTIR, WAITQ...) | 0,6 M | 0,5 |
| inferior | XGKICK | 0,1 M | 0,1 |
| inferior | EFU | 2 484 | ~0 |
Pares con LOI (constante en la inferior): 0,1 %. Aviso: la NOP inferior es `MOVE.0 vf0,vf0` (0x8000033C), por eso en las listas por opcode `MOVE` (10,3 %) es en realidad casi toda NOP; los MOVE reales son ~0,8 %.
Opcodes de la mitad superior (todos los componentes bc juntos): MADDbc 14,6 %, MADDAbc 14,4 %, NOP 12,7 %, MULAbc 12,0 %, ADDbc 8,5 %, MUL 5,1 %, FTOI 4,5 %, MULq 3,5 %, CLIP 3,3 %, MADDq 2,9 %. De la inferior: LQ 29,4 %, SQI 10,5 %, MOVE 10,3 % (casi todo NOP, ver aviso), IADDIU 9,0 %, SQ 7,0 %, IBEQ 6,0 %, IBNE 3,7 %, DIV 3,5 %, FCAND 3,0 %, ILW 2,9 %.
Lectura: el programa es una **transformacion de vertices**: ~87 % de pares con una FMAC util (multiplicar-acumular por componente) y mas de la mitad de las inferiores son cargas/almacenes; las lecturas de banderas (FCAND/FCOR) son el 3,9 % de los pares (FMAND casi no sale), los saltos el 11,7 %,
las divisiones el 3,9 % (cada una bloquea la cola Q ~7 ciclos). Dominan MADD/MADDA/MULA con difusion de componente; el que el 12,7 % de las superiores sean NOP y el 9,5 % de las inferiores indica poco emparejado extra que ganar por reordenar. Sin ponderar por esperas: los ciclos de espera antes de cada par no estan separados
(M0-d completo lo pedia; esto es solo el recuento de pares, que ya da el mix pedido).

## G0: clasificacion de las primitivas que el GS por CPU dibuja en serie (2026-11-12)
Instrumentacion: `patches/ps2recomp-zzzzzz-gs-g0.patch` (en `gs_cpu_backend.cpp`; **apagada** salvo `PS2X_GS_G0=desde,hasta`, indices de Present; sin la variable la huella es una comparacion por Present y una por `SubmitMany`; no cambia el dibujo ni el reparto real). Para cada primitiva
que `SubmitManyImpl` dibuja en serie calcula los **bloques de 256 B de la VRAM (direccion de swizzle exacta) que lee realmente** (rectangulo UV de los vertices, con margen de 1 texel, modos CLAMP/region, formato PSM de TEX0) y **que escribe realmente** (rectangulo XY recortado al scissor; mas ZBUF si se usa) y los
compara con la prueba actual (intervalos de paginas); ademas simula el reparto en tramos con la prueba exacta (bloques leidos frente a escritos). Script: `scripts/g0_classify.sh [desde hasta tick_fin]`; logs en `work/g0/` (ignorado por git). Corrida: modo determinista, guion de `runner/README_DETERMINISTIC.md`, `build/blade/blade_recomp`
(interprete puro), ventana oculta. En modo determinista el indice de Present va ~1650 por detras del tick desde el tick 2500: **Present 950-2250 = ticks ~2600-3900** (1 300 Present con dibujo); segunda corrida Present 1400-1500 = la ventana de M0-b (ticks ~3048-3150). Los ms son tiempo de pared del dibujo
de esas primitivas en el hilo rasterizador (con 4 bandas paradas), medidos por primitiva con `steady_clock` (encarece algo; load average 2,7 y 3,2 al empezar, un poco por encima del 3 pedido en la segunda).

### Hallazgo principal: la hipotesis de M0-b era falsa
M0-b dijo «~8 000 primitivas en serie por VSync, todas por "textura solapa destino"». **No es asi.** Esas primitivas son casi todas **tramos pequeños** (<4 096 px en total, ~18-25 px por primitiva: particulas y sprites) que se dibujan en serie porque la llamada a `SubmitMany` acaba, no por solape. Por motivo de serie (primitivas por Present, media de ticks 2600-3900):
| Motivo de serie | prim/Present | % de las prim en serie | ms/Present | % del tiempo serie | ¿Desaparece con prueba exacta? |
|---|---|---|---|---|---|
| **0 «destino/z solapados»** (intervalos de paginas de FRAME y ZBUF se tocan; no tiene que ver con texturas) | 3 742 | 35 % | 6,99 | **64,6 %** | **100 %**: 0 con bloques compartidos realmente (FRAME fbp=320 psm 0x00, ZBUF zbp=480 psm 0x3A, 640x512) |
| **1 «textura solapa destino»** | 471 | 4,5 % | 2,77 | 25,6 % | 99,6 % (ver abajo) |
| 2 «tramo pequeño» (<4 096 px) | 6 359 | 60 % | 1,06 | 9,8 % | **no**: 6 173 cortados por fin de `SubmitMany` (58 tramos/Present de ~106 prim), 158 por una textura que solapa, resto por cambio de FBP/Z |
| **Total serie** | **10 572** | 100 % | **10,82** | 100 % | |
Total de primitivas por Present: 36 583 (la ventana de M0-b, 29 127: ahi la serie son 6 757: 54 motivo 0 con 5,9 ms, 33 motivo 1 con 3,7 ms y 6 670 tramos pequeños con 1,1 ms: M0-b etiqueto como «textura solapa destino» el grueso, que es el motivo 2; el tiempo serie 10,7 ms/Present coincide con los 12 ms de pared de M0-b).
Lo grave no es el numero (las 6 359 pequeñas son el 60 % de las primitivas y solo el 10 % del tiempo) sino el **tiempo**: el motivo 0 (3 742 prim de ~1 400 px) y unas 50 primitivas grandes de la cat. 0 (~25 000 px) suman el 89 % del tiempo serie.

### Tabla G0: las 471 de «textura solapa destino» (ticks 2600-3900) por categoria
| Cat. | Significado | prim/Present | ms/Present | % del tiempo serie | ¿Desaparece con G1-a (prueba exacta)? |
|---|---|---|---|---|---|
| (a) falso positivo | **0** ni comparten pagina (rangos de paginas solapan, rectangulos reales a >1 pagina) | 50 | 2,59 | 23,9 % | si, 100 % |
| (a) falso positivo | 1 comparten pagina pero no bloque | 0 | 0 | 0 | — |
| (a) falso positivo | 6 sin escritura: la primitiva cae fuera del scissor (no dibuja nada) | 419 | 0,02 | 0,2 % | si, 100 % (ni hace falta dibujarlas) |
| (b) copia entre buffers distintos | 4 otro buffer, comparten bloques | 0 | 0 | 0 | — |
| (c) realimentacion 1:1 (sprite lee su propio pixel) | 2 | <0,1 (5-6 px) | ~0 | ~0 | — (ya valdria «lectura del propio pixel»; no pesa) |
| (d) solape real parcial | 3 mismo buffer, UV desplazado 3-6 px (sprites de 15x15 con ALPHA que leen el propio FRAME, `tbp`=`fbp`*32, TFX=1) | 2 | 0,16 | 1,5 % | **no**: es dependencia real |
| (e) otro | 5 solo con el ZBUF / 7 UV sin rectangulo | 0 | 0 | 0 | — |
| **Suma** | | **471** | **2,77** | **25,6 %** | **469 de 471 (99,6 %)**; con ellas, 2,61 ms |
Ventana M0-b (ticks ~3048-3150): 33 prim/Present, 3,73 ms (16 de la cat. 0 con 3,72 ms, 14 sin escritura, 2 de la cat. 3 con 0,01 ms).

Grupos que mas pesan (textura -> destino; rectangulo UV leido frente a rectangulo XY escrito, ejemplo):
| Grupo | prim/Present | ms/Present | Rectangulos (ejemplo) |
|---|---|---|---|
| cat 0: TEX0 tbp=4096 PSMCT32 tbw=4 256x256 -> FRAME fbp=160 fbw=10 PSMCT32, ZBUF zbp=480 | 7,5 | 2,51 | UV [156,155]-[255,255] frente a XY [0,336]-[277,511]; ALPHA activo, TFX=0: **efecto de pantalla (post-proceso/«bloom»)**, 25 000 px por primitiva; la textura (256 KB) acaba exactamente donde empieza el FRAME: no comparten ni pagina |
| cat 3: tbp=10240 (o 5120) PSMCT32 1024x512 (= el otro FRAME, 320*32 / 160*32) -> FRAME fbp=320 (o 160) | 0,8+0,8 | 0,08+0,08 | UV [25,134]-[28,137] frente a XY [20,129]-[34,143], 2 600 px: lectura del buffer propio con desplazamiento |
| cat 0: tbp=1536 PSMT8 (psm 0x13) 512x256 -> fbp=160 | 0,5 | 0,04 | UV [0,0]-[511,255] frente a XY [240,0]-[639,460]: 1 M de pixeles por primitiva, 0,5 por Present (fondo/cielo): falso positivo por rangos |
| cat 0: tbp=1536 PSMCT32 o PSMT8 128x128 -> fbp=160 o 320 (varios grupos) | ~50 | ~0,05 | sprites de 100-370 px |
| cat 6: tbp=1536 128x128 -> fbp=160 | 417 | 0,02 | fuera del scissor (XY x>=374, y>=512) |
El resto de grupos (top 40) esta en `work/g0/g0_950_2250.log`. Las 3 742 del motivo 0 son un solo grupo: FRAME fbp=320 / ZBUF zbp=480 psm 0x3A, 1 415 px/prim, 6,99 ms/Present; la prueba de paginas se queda corta porque FRAME y ZBUF se calculan «hasta la fila mas baja que toca la primitiva» y los intervalos se tocan sin compartir ningun bloque.

### Simulacion con prueba exacta de bloques (solo textura; el motivo 0 sigue con la prueba de paginas)
Serie: 10 572 -> **9 541 prim/Present** (1,37 Mpx); paralelo 26 011 -> 27 043 prim/Present (44,0 Mpx), 152 tramos paralelos/Present. (M0-b window: serie 6 757 -> 6 693.) La simulacion NO incluye aplicar tambien la prueba exacta al motivo 0 ni agrupar tramos pequeños entre llamadas.

### Lo que se desprende
1. **Aplicando la prueba exacta a «textura solapa destino» (G1-a como esta escrito) desaparece el 24 % del tiempo serie** (2,6 ms de 10,8 por Present), no la mayoria: son 469 primitivas pero pocas, muy grandes. Con 4 bandas el ahorro de pared es ~2,6x(1-1/4) = ~2 ms por VSync sobre ~35,5 aislados (~6 %: de ~28 a ~30 V/s de techo) **[H]**.
2. **El grueso es el motivo 0** (65 % del tiempo serie, 3 742 prim, todas falsas): G1-a debe **incluir la prueba exacta FRAME/ZBUF** (bloques o, mas barato, filas exactas del rectangulo escrito en vez de «hasta la fila mas baja»). Con textura + FRAME/ZBUF exactos desaparece el **~89 % del tiempo serie** (9,6 ms de 10,8) y 4 211 de las 10 572 primitivas: ~7 ms de pared por VSync **[H]** (35,5 -> ~28 ms aislados: techo ~36 V/s, en la linea alta del 30-40 del plan).
3. Lo que queda (6 359 tramos pequeños, 1,2 ms con la real parcial) **no es solape**: son particulas de ~20 px cortadas porque cada una de las 209 llamadas a `SubmitMany` por VSync acaba con un tramo corto. Solo se arregla juntando tramos entre llamadas (o con GPU, donde no importa).
4. **Para G2 (barreras)**: realimentacion real (cat. 2, 3) = 2 primitivas/Present, copias entre buffers (cat. 4) = 0 y FRAME/ZBUF compartiendo bloques = 0. Es la rama optima de §0.5: el numero de barreras por «textura solapa destino» sale de decenas como mucho (2 de cat. 3 + cortes por cambio de FRAME/ZBUF), no de miles; las cat. 3 piden **copia previa del rango compartido** (regla 6 de la referencia), las de cat. 0 y 6 van sin barrera. Salvedad: solo se ha medido el nivel 1 (ticks 2600-3900); otras escenas pueden tener post-proceso con copia real.
5. Cautela para G1-a: la prueba de bloques usada aqui (bitset de 16 384 bits por primitiva) es para medir, no para producir; para la CPU hace falta una prueba barata (rectangulos de paginas exactos) y verificarla con `PS2X_GS_AB=1` y `qa_regression.sh`.
Comprobacion de que el modo normal no cambia: ver la linea de hashes del tick 3000 en la nota de G0 del plan.
