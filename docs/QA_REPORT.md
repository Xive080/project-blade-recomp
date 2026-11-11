# QA_REPORT (QA independiente, 2026-11-11)
Herramientas propias: `scripts/qa_regression.sh`, `qa_compare.py`, `qa_bestmatch.py`, `qa_frametimes.py`, `qa_sample_split.py`, `qa_wav.py`.
Actualizacion ronda 2: existe un modo determinista (runner/README_DETERMINISTIC.md, parche z-deterministic) y las comparaciones por tick de abajo se pueden repetir exactas; AB-009/010 resueltos con el. Nota de metodo (mediciones anteriores, sin modo determinista): el equipo es compartido (carga 8-100). La velocidad de ejecucion cambia el flujo del guion (AB-009), asi que
las comparaciones por VSync fijo no son concluyentes; se anota en cada veredicto.

## Fase 0/1: auditoria y linea base
Ver PERFORMANCE_BASELINE.md, GRAPHICS_VALIDATION.md, LEVEL_COMPLETION_MATRIX.md, AIRBLADE_ISSUES.md.
Hechos medidos: 3 500 VSync en 218 s (16 V/s) con carga baja; partida 12,7 V/s; EE 98 % de un nucleo; cronometro 50 VSync/s;
VBlank del runtime 60 Hz. Referencia de regresion: `qa/ref_baseline/` (5 hitos, tomados con carga alta).

## Certificacion dev 2 (rendimiento + PAL 50 Hz): PARCIAL, NO QA PASSED
| Criterio | Resultado | Evidencia |
|---|---|---|
| (1) 13 parches sobre e563698 sin diferencias con third_party | **RECHAZADO** en lo estricto: los 13 aplican limpios, pero su `third_party` tiene ademas codigo no parcheado (perfil VU1 `PS2X_VU1_PROFILE`, 63 lineas) y el `blade_devtools.cpp` entregado necesita `ps2xVu1Replay`, que no esta en los parches de la entrega: con ellos el enlace falla (reproducido). Tras mi build han anadido `vu1-record`; no recertificado | AB-011, `build-qa-dev2.log` |
| (2) Tiempo PAL | **PARCIAL**: VBlank de 20 ms comprobado (menu 50,0 V/s en 1 500 VSync, 2 ejecuciones); el cronometro cuenta 50 VSync/s. En la partida solo 11-25 V/s: el juego no llega a tiempo real. Musica/FMV a ritmo correcto: FMV UNVERIFIED en tiempo real; audio ver dev 3 | `qa/ab_a.log`, `qa/ab_b.log`, `qa/run_dev2/run.log` |
| (3) Imagen identica antes/despues, con y sin BLADE_NO_SKYWAIT | **ACTUALIZADO**: en modo determinista, con BLADE_NO_SKYWAIT los 79 PNG (cada 50 ticks hasta 3950) y los 78 hashes de frame coinciden con la corrida con skywait; la RAM diverge desde el tick 1600. Antes: no hay ejecuciones reproducibles ni siquiera una con otra del mismo binario (default vs default sin entrada: mediana de dif. 22, max 231). Con el guion, el binario dev 2 se desfasa y sigue otro flujo (qa_dev2_x_vs_nosky_timeline.png). La afirmacion "identica bit a bit" solo se puede comprobar con grabacion/reproduccion de la salida del GS o del VU1, que no he repetido | `qa/ab_*`, `qa/run_dev2_x`, `qa/run_dev2_nosky` |
| (4) VSync/s y tiempos de fotograma medidos por QA | Menu: 50,0 V/s con override vs 33,5 sin el (carga ~10): mejora real. Partida: 11-25 V/s. Distribucion por VSync (media, minimo, p1) con el build del dev 2: no medida (UNVERIFIED); el contador del dev da media 45,1 ms / p1 137 ms | PERFORMANCE_BASELINE.md |
Otros: mi comparacion de 5 hitos contra la referencia sale DIFERENTE en todos (dif. media 26-102) por el desfase del guion, no por un fallo de render demostrado.
Riesgo de regresion: el override de 0x1DF500 avanza el reloj del EE e IOP dentro del override; hay que revisar que no omita un cambio de estado que solo
la interrupcion de VBlank hacia (sin pruebas de ello). La oracion del dev sobre el oraculo VU1 (`BLADE_VU1_REPLAY`) esta pendiente de repetir por QA.

## Certificacion dev 3 (audio SPU2): PARCIAL, NO QA PASSED
| Criterio | Resultado |
|---|---|
| (1) Serie completa de parches sobre e563698 limpio | **OK**: los 13 de su carpeta aplican; con los 2 de dev 2 (15, orden alfabetico) aplican limpios y el arbol equivale al mio salvo restos de depuracion del dev 1 (AB-013). Su `third_party` difiere solo en esos restos |
| (2) Pruebas | `spu2_dsp_tests` y `spu2_mix_tests` PASAN con las ordenes manuales del propio fichero. Con CMake (`PS2X_IOP_BUILD_TESTS=ON`): el objetivo `ps2_iop_spu2_dsp_tests` NO compila y `spu2_mix_tests` no esta registrado (AB-012). Los vectores son calculados por el mismo autor |
| (3) Analisis del WAV (`qa/audio/dev23.wav`, build dev 2 + dev 3, guion del README, 3 600 VSync) | 48 kHz estereo, 72,05 s (esperado 72,00 s a 50 Hz: **coincide**); 0 muestras saturadas; 0 saltos >20 000 (sin clics grandes); pico 18 470; RMS -24 dBFS; continua -11 (despreciable); silencio total los primeros 8 s (arranque, plausible) y musica continua desde 8 s; 0 NaN (entero de 16 bits). Espectro: dominante 35-140 Hz y 45-96 % de la energia bajo 200 Hz en 10-40 s y 55-70 s, centroide 2-5 kHz; con picos de efectos (6 355 de RMS a ~37 s). Tono plausible: NO PUEDO afirmarlo sin referencia |
| (4) Comparacion con PCSX2 | **UNVERIFIED**: PCSX2 esta en el equipo pero no he grabado la misma escena (sin captura de audio automatica en este entorno) |
| (5) Caveats | H1 (MVOL del core 1 = 0 en el juego; uso 1,0 por defecto) es una HIPOTESIS no un hecho: una pista de que falta otra ruta de volumen, o que el valor se pone en otra parte (p. ej. un `sceSdSetParam` con otro codigo). Gauss vs Catmull-Rom y reverb de PSX: causan timbre distinto, riesgo bajo. AVOL/BVOL como entero con signo de 16 bits: plausible para BVOL/AVOL de voz, no comprobado. Hay que verificar con PCSX2 el nivel relativo musica/efectos |
| (6) Rutas absolutas y nombres de modelos en lo entregado | OK: ninguna en parche, runner, scripts, docs ni README de dev 3 ni de dev 2 (grep) |
Tiempo real: el mezclador va con el reloj del IOP; con la partida a 12 V/s el audio en vivo no se pudo comprobar (AB-015).

## Pendiente de QA
Recertificar dev 2 con `vu1-record`; modo reproducible (entrada por evento, sin esperas de reloj de pared); comparar con PCSX2 imagen
(panel blanco, tabla, negros) y audio; medir la distribucion por VSync del build de dev 2; certificar dev 1 cuando entregue.

## Recertificacion de audio de dev 3 (ronda 2, modo determinista)
Serie de 16 parches (con z-deterministic y el z-spu2-mixer nuevo de dev 3) aplicada limpia sobre e563698 en un clon nuevo; arbol propio reconstruido desde cero con ella.
- QA PASSED: pruebas CMake desde cero (`-DPS2X_IOP_BUILD_TESTS=ON`, build nuevo): `ps2_iop_spu2_dsp_tests` y `ps2_iop_spu2_mix_tests` 2/2 por ctest; vectores FFmpeg dif. max filtro 0 = 0, filtros 1-4 = 36 (tolerancia 40).
- QA PASSED: reproducibilidad del audio: dos volcados `BLADE_AUDIO_DUMP` en modo determinista hasta VSync 5300 son identicos byte a byte (qa/audio2/W1.wav, W2.wav) y los hashes de RAM tambien.
- QA PASSED: duracion 106,07 s para 5300 VSync (esperado 106,00 a 50 Hz; +0,07 s); 2 canales 48 kHz.
- QA PASSED: sin saturacion (0 muestras a fondo de escala; pico 18788 de 32767, RMS -23,8 dBFS); salto maximo entre muestras 11428 (sin saltos >20000); musica desde el segundo 8.
- Transitorios (energia en 10 ms > 3x la previa + umbral): 28 en total, 7 entre 60 y 104 s. No coincide con los 33 de dev 3 porque el criterio es distinto; son inicios de energia, no clics duros. La identificacion de SFX concretos por oido sigue siendo UNVERIFIED.
- UNVERIFIED: comparacion con PCSX2 (bloqueada); volumen del core 1 / AutoDMA/MVOL (no se alcanza antes del tick 6000); AVOL/reverb/Catmull-Rom como hipotesis.

## Recertificacion dev 2 (ronda 2): vu1-record / vu1-z-compiled
QA PASSED con evidencia: serie de 19 parches (los 16 propios + vu1-profile, vu1-record, vu1-z-compiled de dev 2) aplica limpia sobre e563698 en un clon nuevo; reconstruccion completa de build/qa (3978 unidades) enlazada sin shims; replay 20000/20000 y 6000/6000 identicas con el interprete y con el codigo compilado; prueba negativa detecta exactamente las llamadas 2964 y 2965. Rendimiento en docs/PERFORMANCE_BASELINE.md. No verificado: que el codigo compilado sea correcto fuera de lo cubierto por las grabaciones (634559 pares caen al interprete en vu1b).
