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
