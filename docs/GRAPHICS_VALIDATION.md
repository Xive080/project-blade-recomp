# GRAPHICS_VALIDATION (QA independiente)

Capturas en `scratchpad/hitos/qa_*.png`; referencias de regresion en `qa/ref_baseline/`. Todo lo "correcto" abajo es lo que
se ve en la captura; **no hay referencia de consola ni de PCSX2 comparada (UNVERIFIED)**. PCSX2 existe en `~/emu/pcsx2` pero no se
ha usado todavia para comparar imagenes.

| Elemento | Captura | Observado | Estado |
|---|---|---|---|
| Pantalla de carga de la tarjeta | qa_sheet_arranque_50_600 (50, 100) | Letrero "loading", barras animadas, texto legible | Se ve bien |
| Logo SCEE, Criterion | idem (150-400, 500) | Texto nitido, logo de Criterion con color y sombra correctos | Se ve bien |
| Ruido gris (VSync 450) | qa_arranque_ruido_450 | Textura de ruido a pantalla completa un instante | UNVERIFIED si es la transicion original |
| Titulo "Press START" | qa_sheet_arranque (550, 600) | Ilustracion naranja con el corredor y el logo, texto de copyright legible | Se ve bien |
| Menu principal | qa_baseline_650 / 900 | Video de fondo (corredor desenfocado en azul-verde), opciones en naranja, panel blanco vertical a la derecha con lineas oscuras | Panel: AB-005 |
| Menu un jugador | qa_baseline_1100 | Titulo naranja "single player", texto parcialmente tapado por el resaltado de "STORY MODE" | Texto solapado UNVERIFIED |
| Carga "hideaway" | qa_sheet_menu_carga_1150_2250 | Silueta del patinador en tonos turquesa, consejo legible, "CONTINUE" abajo | Se ve bien |
| Escena de entrada | qa_intro_edificio_negro_1950 | Marco de puerta con textura madera iluminada, **pared del edificio de color negro plano**, cielo y muro bajo texturados | AB-006 |
| Pasillo / sofa | qa_sheet_menu_carga (2050, 2150), qa_sheet_intro (2300) | Paredes de ladrillo con carteles amarillos, sofa verde con textura, luz azulada de la ventana | Se ve bien |
| Cielo y horizonte (VSync 2450) | qa_intro_suelo_gris_2450 | Skyline con nubes bien texturadas; **mitad inferior gris oscuro plano**, sin HUD | AB-008 (transitorio) |
| Mundo del nivel 1 | qa_baseline_3000 / 3900, qa_sheet_partida_3000_3180 | Calles, edificios de ladrillo con ventanas, cielo con nubes, texto del cartel "THE LIVING BEATS", camion; sin cortes de geometria a simple vista | Se ve bien en lo observado |
| Voladizo del teatro | qa_baseline_3000, sheet (3060-3160) | **Cara inferior del voladizo negro plano** con borde iluminado | AB-007 |
| HUD | qa_baseline_3000 | "boost" con barra azul brillante, "score 00000000", reloj 2:19.44 con decimas en superindice, flecha naranja | Se ve bien |
| Jugador (zoom) | qa_jugador_zoom_3000 | Persona de perfil (chaqueta azul clara, pantalon oscuro, zapatillas lila, pelo marron liso). Proporciones plausibles, sin triangulos estirados, sombreado suave, textura casi lisa; **sin tabla debajo y sin sombra proyectada**. Esta figura (VSync 3000) es un peaton junto al camion | AB-004 |
| Jugador (captura del dev 1, hito 20) | hitos/20_... | Figura de camiseta gris, pantalon verde, pelo largo claro, pies planos sobre el suelo, brazos flexionados; **sin tabla**; posicion identica en los fotogramas siguientes de la figura lejana (pantalon verde caminando) | AB-004, animacion UNVERIFIED |
| Cara / textura de la cara / animacion de caminar | - | No se ve la cara en ningun fotograma (siempre de espalda o lejos). Animacion: no he visto ciclo de paso | UNVERIFIED |
| Transparencias / sombras | - | No hay sombras proyectadas del jugador ni del peaton | UNVERIFIED si el juego las dibuja |

## Hipotesis (confianza)
- AB-005 paneles blancos: el dev 1 dice que el juego pide abanicos `rgba=255,255,255,128` con `ALPHA=0x48`; con A=0x80 (= 1,0 en PS2)
  la formula Cs*As+Cd satura a blanco, asi que el GS lo hace bien **si** el juego lo pide. Pero un panel blanco opaco sobre un video es
  raro para un menu. Hipotesis: el color/alfa del abanico lo arma el EE con `PEXEW`/`PPACW` (el mismo camino que el jugador invisible) o falta
  una textura/mascara; confianza baja (30 %). Comprobable con PCSX2.
- AB-006/AB-007 negro plano (pared de la intro, cara inferior del voladizo): geometria que el VU1 dibuja con color o textura nulos
  (ruta de iluminacion o de material distinta, p. ej. sin textura en el lote). Confianza media-baja (40 %). Comprobable volcando el lote GS.
- AB-004 sin tabla: la tabla (atomico distinto del cuerpo) no se dibuja o va transparente. El dev 1 la vio con color (112,0,128,0) y
  corrigio PEXEW; en mis capturas sigue sin verse. Confianza media (50 %) de que es un defecto y no que el modo historia empieza sin tabla.
