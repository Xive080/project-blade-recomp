# AIRBLADE_STATUS (QA independiente, 2026-11-11)
Estados: NOT STARTED, INVESTIGATING, IMPLEMENTING, BUILT, TESTING, QA PASSED, BLOCKED, FAILED, VERIFIED.
Base: commit 795b79a (GitHub) + mmi-permute (sin commitear). Evidencia: `qa/`, `scratchpad/hitos/qa_*.png`.

| Subsistema | Estado | Evidencia / comentario |
|---|---|---|
| Traduccion y enlace (3 316 ficheros) | VERIFIED | build propio en `build/qa` desde cero, 4 012 pasos, sin errores |
| Arranque, tarjeta de memoria | VERIFIED (visto) | frames 50-100, log `[MC]` |
| Videos de inicio / FMV de fondo | TESTING | se ven SCEE, Criterion; audio UNVERIFIED |
| Menus, seleccion de partida, carga | TESTING | funcionan; panel blanco AB-005 |
| Nivel 1: mundo y camara | TESTING | se ve y se mueve; negros AB-006/007 |
| Nivel 1: jugador | TESTING / FAILED parcial | figura visible tras PEXEW, sin tabla (AB-004) |
| Entrada (guion y teclado) | TESTING | TESTED: modo determinista (PS2X_DETERMINISTIC=1, BLADE_DET_*): dos corridas a VSync 3950 identicas en RAM y 79/79 PNG; sin el modo el guion no es reproducible (AB-009/010) |
| Trucos, puntuacion | NOT VERIFIED | solo afirmacion del dev |
| Rendimiento (linea base) | FAILED | 12 V/s en partida (objetivo 50) |
| Tiempo PAL | dev 2: BUILT/TESTING | ver QA_REPORT |
| Audio | dev 3: BUILT/TESTING | ver QA_REPORT |
| Pruebas automaticas del SPU2 | TESTING | pasan con orden manual; CMake falla (AB-012) |
Nada esta QA PASSED todavia.
