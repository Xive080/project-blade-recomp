# LEVEL_COMPLETION_MATRIX
Casillas: OK (visto en captura propia), PARCIAL, FALLA, UNVERIFIED (no probado por QA).
| Fase | Arranque | Entrada | Imagen | Audio | Rendimiento | Notas / evidencia |
|---|---|---|---|---|---|---|
| Carga de tarjeta / pantalla "loading" | OK | n/a | OK | UNVERIFIED | OK | frames 50-100 |
| Videos de inicio (SCEE, Criterion, FMV de fondo) | OK | START salta | OK (SCEE/Criterion); ruido gris 450 UNVERIFIED | UNVERIFIED | OK en menu (35 V/s linea base, 50 dev 2) | qa_sheet_arranque |
| Titulo "Press START" | OK | OK | OK | UNVERIFIED | OK | frame 550-650 |
| Menu principal | OK | OK | PARCIAL (panel blanco, AB-005) | UNVERIFIED | OK | qa_baseline_650/900 |
| Menu "single player" / modo historia | OK | OK (X) | PARCIAL | UNVERIFIED | OK | qa_baseline_1100 |
| Seleccion de partida (GAME1, autoguardado) | OK (registros de tarjeta en el log) | OK | UNVERIFIED (vista) | -- | -- | log `[MC]` |
| Carga hideaway (L1.BIN en segundo plano) | OK | X en CONTINUE | OK | UNVERIFIED | PARCIAL (13 V/s) | frames 1250-2250 |
| Escena de entrada del nivel 1 | OK | X salta | PARCIAL (pared negra, AB-006) | UNVERIFIED | FALLA (3-7 V/s con carga alta) | qa_intro_edificio_negro_1950 |
| Nivel 1 mundo + camara | OK | UNVERIFIED (solo guion X; stick no probado) | OK en lo visto | UNVERIFIED | FALLA (12 V/s) | qa_baseline_3000/3900 |
| Nivel 1 jugador | PARCIAL | UNVERIFIED | FALLA/PARCIAL (sin tabla, AB-004) | UNVERIFIED | -- | qa_jugador_zoom_3000 |
| Nivel 1 HUD y cronometro | OK | -- | OK | -- | -- | cronometro a 50 VSync/s |
| Trucos / puntuacion (CUADRADO suma 200) | UNVERIFIED por QA (afirmacion del dev) | UNVERIFIED | UNVERIFIED | UNVERIFIED | -- | no probado |
| Fin del nivel 1 / puntuacion final / guardar | UNVERIFIED | UNVERIFIED | UNVERIFIED | UNVERIFIED | UNVERIFIED | no alcanzado |
| Niveles 2-N, entrenamiento, multijugador, extras | UNVERIFIED | UNVERIFIED | UNVERIFIED | UNVERIFIED | UNVERIFIED | no probado |
