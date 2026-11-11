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

## Intento de completar el nivel 1 (modo determinista, 11000 ticks)

Ejecucion: `qa/blade_recomp.fp` (rama integracion, serie de 20 parches), `PS2X_DETERMINISTIC=1`, oculto, un solo proceso. Guion `BLADE_DET_INPUT`: START/X hasta el nivel y despues, desde el tick 3000, stick adelante (LY=0) sostenido y X cada 400 ticks. Volcado de captura y hash cada 100 ticks desde 2500 (`BLADE_DET_DUMP`, `BLADE_DET_HASH`). Guion completo en `qa/lvl1/script.txt` (no versionado), log en `qa/lvl1/run.log`, hojas de contacto `qa/lvl1/lvl1_sheet.png` (2500-11000) y `qa/lvl1/lvl1_end.png` (9800-10900).

Resultado: el nivel 1 NO se completo. No hay cuelgue ni caida; el motivo es de juego, no de ejecucion.

| Hecho | Estado | Evidencia |
|---|---|---|
| El proceso llega a 11000 ticks sin parar | OK | run.log, `[det] tick 11000` |
| Errores del runtime (guest-branch, missing-target, Unimplemented, Syscall TODO, Segmentation, abort) | OK: 0 apariciones | grep sobre run.log |
| Hash de frame distinto en cada muestra (no hay imagen congelada) | OK | 91 muestras 2500-11000, ninguna igual a la anterior |
| Cronometro del nivel baja de 2:29 (tick 2500) a 0:01 (tick 9900), rojo desde ~0:09 | OK | lvl1_sheet.png, lvl1_end.png |
| Jugador visible | PARCIAL: solo en tick 3000 (junto al camion); desde 3500 la camara queda pegada a una pared de ladrillo, sin personaje a la vista | lvl1_sheet.png |
| Puntuacion | UNVERIFIED que suba: se queda en 00000000 en todas las muestras (el guion no realizo trucos reales) | lvl1_sheet.png |
| Avance por el nivel | FALLA/UNVERIFIED: la camara se desplaza pero el guion ciego no lleva al jugador a ningun objetivo; no se puede distinguir fallo del port de fallo del guion | lvl1_sheet.png |
| Fin por tiempo: pantalla negra (tick ~10000, "CONTINUE"), "game over" con RESTART/QUIT, menu "quit game" | OK (flujo de fin de partida funciona y responde a X) | frames 10000-11000 |
| Nivel 2 y siguientes | UNVERIFIED: no alcanzados | -- |

Divergencia exacta: ninguna detectada por hash ni por log. Limite del intento: sin ver el jugador a partir del tick 3500 no se puede afirmar si el personaje no se mueve (entrada/animacion, relacionado con AB-004: sin tabla) o si esta oculto por la camara. Siguiente paso necesario: comparar en PCSX2 la misma entrada y ticks 3500-4500 para ver si el jugador aparece, y localizar un guion que supere el nivel (requiere mirar el juego, no ciego).

## Contraste con PCSX2 y comprobacion del mando (ronda siguiente)

PCSX2 2.9.168 (Rosetta), ISO propia de solo lectura, DualShock2 con W = stick izquierdo arriba, K = Cruz, Return = Start. Flujo: Start, "single player", "continue without saving", "story mode", carga hideaway, X.

| Hecho | Estado | Evidencia |
|---|---|---|
| En PCSX2 el jugador SI se dibuja (de espaldas, camiseta blanca, pantalon verde, mochila amarilla, sobre la tabla roja con sombra) | OK | primera captura del nivel, cronometro 2:21,88; capturas 2:13-1:25 |
| En PCSX2 el stick adelante (W mantenido) mueve al jugador por el nivel: de la plaza al cartel CLARKES (cronometro 1:35-1:25) | OK | hoja de capturas posterior en la carpeta de trabajo (no versionada) |
| En el puerto el jugador no aparece cerca de la camara en ningun tick 3000-11000 (figura de pantalon verde solo a media distancia, sin tabla) | FALLA (AB-004, CONFIRMADO) | qa/lvl1/lvl1_sheet.png, qa/lvl2/lvl2_sheet.png |
| El stick llega al juego en el puerto: volcado de RAM del tick 3150 (stick arriba desde 3095) tiene 4 bloques de lectura de mando con modo 0x73 (DualShock2 analogico), botones 0xFFFF, rx=0x80 ry=0x80 lx=0x80 **ly=0x00** | OK | `qa/lvl3/ram/ram_003150.bin` (no versionado), offsets 0x2fa708, 0x2fa8c8, 0x306c08, 0x306dc8; el juego pide el modo analogico (`scePadSetMainMode`) y el puerto lo concede |
| Misma imagen con stick (LY=0) y con cruceta (UP) en los ticks 3000-3200; difieren desde 3300 | UNVERIFIED que la entrada mueva al jugador: hasta ~3200 el nivel parece guionizado (introduccion), y desde 3208 el guion con stick pulsa ademas X, asi que no es comparacion limpia | hashes de frame de `qa/lvl1/run.log` y `qa/lvl2/run.log`; los hashes de RAM si difieren entre ambas entradas desde el tick 3100 (la entrada llega a RAM) |
| Primer punto de divergencia puerto/PCSX2 | El jugador no se dibuja (AB-004): impide ver si avanza. No se detecta fallo del mando | -- |

Siguiente paso propuesto (no hecho): volcar RAM del puerto con y sin stick en dos ticks y buscar la posicion del jugador (flotantes que cambian) para saber si el personaje avanza aunque no se dibuje; y revisar por que no se dibuja el modelo del jugador/tabla (AB-004).
