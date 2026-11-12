# ADR historico (texto integro del ADR codebase-memory a 2026-11-12, antes de condensarlo)

## Contexto
Recompilación estática de AirBlade (PS2, PAL SCES-50246) a un port nativo para Mac/PC, en /Users/home/airblade-recomp (remoto github Xive080/project-blade-recomp, rama main). Traducción del ELF con PS2Recomp (método de hctp-recomp: Ghidra + inicios recuperados), runner propio en runner/ (blade_overrides.cpp, blade_devtools.cpp, blade_audio.cpp, blade_input.cpp, qa_deterministic.cpp) y serie de parches sobre PS2Recomp en patches/ (20 tras la integración). El decomp (~/airblade-decomp) es un proyecto aparte: NO se toca desde aquí. Objetivo del usuario: que el juego funcione en la recomp (ver memoria feedback_blade-solo-recomp).

## Invariantes
- Sin ELF, ISO ni assets en el repo (derechos de terceros).
- Sin rutas absolutas en el código (regla del usuario).
- No editar a mano el código generado ni las cabeceras del runtime: los arreglos van en runner/ o como parche en patches/.
- No limpiar el build (build/blade) ni parar compilaciones en curso.
- Commits sin línea de coautor ni mención de IA (regla «sin IA en repos públicos»); no hacer push sin pedirlo.
- Reproducibilidad: modo determinista (PS2X_DETERMINISTIC=1 + BLADE_DET_*), entrada por tick de VSync.

## Decisiones
- 2026-11-11: base = rama integracion (fast-forward de main); descartado fusionar a mano los clones -audio/-gfx/-perf/-qa.
- 2026-11-11: orden: integrar, AB-004, rendimiento (comparten blade_overrides.cpp).
- 2026-11-11: AB-004 a un agente sin worktree; nada en paralelo.
- 2026-11-12: AB-017 en el runtime (zz-guest-heap), no override del juego (taparia el sintoma; AB-005 seguiria).
- 2026-11-12: M0-b medido con un banco del GS aislado (PS2X_GS_BENCH, repite en el hilo del GS) en vez de saltar dibujos con BLADE_RT_SKIP (solo omite un atomico); repetir en el hilo de Present daba 47-56 ms/V frente a 35 en el hilo del GS.

## Estado
- Arranca, llega al nivel 1 y se juega; 11 000 ticks sin cuelgues ni errores del runtime (QA, modo determinista). Mundo, cámara, HUD y cronómetro correctos (50 VSync/s).
- Corregido hasta ahora: SQRT.S/CVT.W.S/macros VU0 (matriz de cámara nula), PEXEW y permutaciones MMI (color del material), dispatch recursivo tras expropiación, caché de texturas del menú, VBlank PAL a 50 Hz, mezclador SPU2 (WAV de 72 s).
- main = integracion + commit 0c95c65 (parche MMI desplazamientos/saturación, README), luego 19d7701 ... a1eedd5 y d2044e4 (M0). Sin subir. El agente de AB-004 tiene cambios sin commitear en runner/blade_overrides.cpp y runner/qa_deterministic.cpp.
- Matriz de QA: nivel 1 no completado; niveles 2+ sin probar.

## Hallazgos
- AB-004 (P1, confirmado contra PCSX2): en el nivel 1, ticks 3000-3160, en PCSX2 el jugador va sobre la tabla con sombra a 2-3 unidades de la cámara; en el port solo se dibujan peatones lejanos. La cámara (controlador 0x1EC9E68) es correcta; el atómico de la tabla llega al GS con A=0 (112,112,128,0), ALPHA=0x44. Igual con y sin PS2X_VU1_NOGEN=1, así que el VU1 compilado no es la causa. Causa raíz aún sin identificar.
- AB-001 rendimiento: partida 14-25 V/s (objetivo 50); EE ~100 % de un núcleo, ~75 % en el intérprete de VU1, hilo del GS ~90 %. Hacen falta ~2,5x: recompilador de microprogramas VU1 y abaratar el paso de primitivas al GS.
- AB-005: panel blanco opaco en el menú (PCSX2 no lo tiene). AB-008 corte gris ~tick 2450. AB-011 los parches de dev 2 no reproducen limpios (símbolo ps2xVu1Replay). AB-013 restos de depuración (BLADE_PTRACE/GSTRACE/TMPTRACE/ATRACE). AB-015/016 audio en vivo y MVOL sin verificar.
- Tras el tick 3500 de la prueba ciega la cámara queda pegada a una pared; no se distingue fallo del port de fallo del guion.
- Documentos de QA en docs/: AIRBLADE_ISSUES.md, GRAPHICS_VALIDATION.md, LEVEL_COMPLETION_MATRIX.md, QA_REPORT.md, PERFORMANCE_BASELINE.md, AUDIO_IMPLEMENTATION_STATUS.md.
- Error de sesión 2026-11-11: se confundió «recomp» con el decomp y se lanzó una pasada de matching (parada; quedan 4 .c sin commitear en ~/airblade-decomp/src/matched, no tocados).

## Proceso
Cómo se trabaja en esta recomp (método, para retomar tras compactar o en otra sesión):
1. Arranque: list_projects -> index_status -> leer este ADR -> `git log` y `git status` en ~/airblade-recomp. Si el ADR contradice el código, manda el código.
2. Bucle de un defecto: reproducir con el modo determinista (PS2X_DETERMINISTIC=1, BLADE_HIDDEN=1, BLADE_DET_INPUT por tick, BLADE_DET_DUMP/HASH/EXIT; ver runner/README_DETERMINISTIC.md; ~6 min hasta el tick 3265) -> bisecar por ticks (volcado fino cada 1) -> comparar con PCSX2 en el mismo tick -> localizar causa en RAM/traza del GS/instrucción mal traducida (patrón de los bugs ya resueltos: SQRT.S, CVT.W.S, PEXEW) -> arreglar en runner/blade_overrides.cpp o como parche en patches/ps2recomp-*.patch -> recompilar sin limpiar build/blade -> repetir la misma corrida y comparar PNG/hashes -> documentar en README (estilo Pendiente N) y en docs/AIRBLADE_ISSUES.md.
3. Evidencia: cada afirmación de «arreglado» lleva PNG antes/después con ruta y tick; sin referencia de PCSX2 se marca UNVERIFIED. Estados de QA: OPEN / FIXED-BUILT / CERTIFIED.
4. Agentes: una ronda = un agente por defecto, con brief explícito (objetivo único, lo ya descartado, ficheros a leer, reglas de este ADR). No lanzar agentes en paralelo sobre los mismos ficheros ni el mismo build. Los informes del agente no se dan por buenos hasta ver commits/PNG.
5. Integración: base = rama integracion; parches nuevos se añaden a patches/ y se comprueban con scripts/check_patches.sh; regresión con scripts/qa_regression.sh (referencias en qa/ref_det).
6. Subida: solo commits de la recomp, sin coautor ni mención de IA, sin push salvo petición. Mandar capturas de avances al usuario.
7. Cierre: actualizar este ADR sin borrar lo anterior; mover lo no estructural aquí, no a memoria/.

## Hallazgos AB-004 (cierre 2026-11-11)
- Causa: en 0x128910 la distancia de dibujo del jugador (0x179CF0, tabla 0x2E02F0, FLT_MAX) al cuadrado desborda a 0x7FFFFFFF (NaN en el host) y `c.lt.s` con comparación del host daba falso; no se llamaba a 0x13CD40.
- Arreglo: patches/ps2recomp-z-fpu-compare.patch (commit 19d7701, sin subir). FIXED-BUILT, UNVERIFIED vs PCSX2. PNG en qa_out/ab004/antes y /despues (ticks 3100, 3160).
- Regresión: hashes distintos del tick 1750 al 3950; regenerar ref (~/airblade-recomp-qa/qa/ref_det; usar -b build/blade/blade_recomp -r).
- AB-017 OPEN: mancha blanca en el suelo junto al jugador (¿sombra, 0x14CF90?). Sin comprobar MAX.S/MIN.S y VU con exponente 255.

## Hallazgos AB-017/AB-005 (cierre 2026-11-12)
- Causa: el runtime cortaba el monticulo en 0x01F00000 (alli sus pools HLE); AirBlade lo llena a ~0x01EFC000 y RwRasterCreate de la sombra (0x131670, tick 2, [0x2FA43C]) fallaba E_RW_NOMEM -> raster NULL -> sombra PRIM 0x0B blanca. AB-005 (panel del menu) igual.
- Arreglo: patches/ps2recomp-zz-guest-heap.patch (limite 0x01F80000, pools a 0x80000-0xB1000) + zz-gs-probe (depuracion). FIXED-BUILT, UNVERIFIED vs PCSX2. PNG qa_out/ab017/.
- check_patches.sh no ve ficheros cambiados que ningun parche toca; qa_regression.sh -r espera PNG en la raiz de la ref.

## Hallazgos AB-001 (2026-11-12)
- Nivel 1: 11,4 V/s interprete, 14,6 VU1_GEN; EE 100 %, ~90 % VU1 (~46 ns/ciclo, hace falta ~6,5x). Banco BLADE_VU1_DIFF. Detalle: docs/PERFORMANCE_BASELINE.md.
- Plan: docs/PERFORMANCE_PLAN.md (a1eedd5). Llamadas VU1 empiezan con tuberias vacias; cache inutil (0,5 %); 9 FMAND frente a 625 FMAC.
- Interprete funde MADD/MSUB en arm64 (sin -ffp-contract=off): oraculo dependiente de plataforma (D1).
- GS ~23-29 ms/fotograma: techo probable 34-43 V/s (medir M0-b). Juego: 1-4 pasos dt=0,02 por dibujo (0x132D10). M1 4P+4E.

## Hallazgos AB-001 M0 (2026-11-12, commit d2044e4)
- GS: 1 hilo rasterizador + BandPool de 4 (3 hilos de banda); Mac 8 nucleos (4P+4E). En vivo el rasterizador esta ocupado 26,4 ms/VSync (mediana; 18,2 CPU + 8,2 espera de bandas; 47 ms en la escena pesada ticks 2780-2870) y ocioso 52 % (el EE manda hoy). GS aislado (PS2X_GS_BENCH): 33-44 ms/V => techo 23-30 V/s; 1 banda 58 ms, 4 bandas 35,5, 8 bandas 34,8 => ~28 ms/V de parte serie que no escala (~8 000 prims/V en serie por "textura solapa destino"). Techo estimado 23-38 V/s, <50: G1 va antes de P6 y mas hilos no ayudan. Las dos medidas no cuadran +-15 %.
- Corpus nivel 1 (work/vu1gen/corpus/e1-e5.rec, ticks 2640-3908, 15000 llamadas c/u, 0 diferencias): 72 microprogramas distintos, 4 en las 5 escenas, 43 en una sola; los 8 primeros = 96-97 % de ciclos; las otras 4 escenas cubren 86-100 % de los ciclos de la que falta (lo no cubierto = programas de entrada 0000 por objeto).
- Histograma de pares (133 M pares, 153,5 M ciclos): 87 % FMAC util en la superior (MADD/MADDA/MULA con bc dominan), inferior 53 % load/store, 19 % IALU/banderas, 12 % saltos, 3,9 % DIV/SQRT, NOP inferior 9,5 % (MOVE.0). 1,16 ciclos/par.
- Instrumentacion: patches/ps2recomp-zzzz-gs-bench-vu1-hist.patch (PS2X_GS_BENCH, BLADE_VU1_HIST), scripts/vu1_record_corpus.sh, scripts/vu1_hist_report.py.

## Pendiente
1. AB-004 FIXED-BUILT (2026-11-11); sombra OK desde 2026-11-12.
2. AB-001 rendimiento (recompilar microprogramas VU1, coste de Submit/UploadImage); seguir el plan: M0-a (qa/ref_det, pendiente de la decision del oraculo FP), D1, G1 antes de P6.
3. AB-005 FIXED-BUILT 2026-11-12 (falta PCSX2); AB-013 limpiar depuración; AB-011/012 recertificar parches y pruebas del SPU2.
4. Completar el nivel 1 con un guion que juegue de verdad; probar niveles 2+.
5. Indexar docs/ en el grafo si se quiere buscar en los informes de QA.
6. Regenerar qa/ref_det; capturas PCSX2 ticks 100/900/3100/3160.
7. M0-d completo (esperas por par) y corpus de menu/niveles 2+.
