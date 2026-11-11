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
