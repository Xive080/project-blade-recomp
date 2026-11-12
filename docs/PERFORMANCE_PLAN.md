# PERFORMANCE_PLAN: de 11-15 V/s a 60 como minimo y 120 de objetivo (revisado tras M0)

Fecha: 2026-11-12. Base: `main` en c6d6c69 (linea base limpia de AB-001). Es un plan: no cambia codigo.
**Revision 2026-11-12 (tras M0, commit d2044e4)**: la meta pasa de 50 V/s a **60 como minimo y 120 como objetivo** (peticion del
usuario: "nativamente deberias de ir minimo 60 o 120 fps"). La revision esta en la seccion 0 y manda sobre el resto; las
secciones 1-6 son el plan original (a1eedd5), que se conserva, con notas fechadas donde M0 o la meta nueva lo dejan obsoleto.
Fuentes: `docs/PERFORMANCE_BASELINE.md` (seccion 2026-11-12), README (Pendiente 3, VU1 compilado, `BLADE_VU1_DIFF`, Plan B),
`ps2xRuntime/src/lib/vu/*` (interprete y ruta `VU1_GEN`), `work/vu1gen/` (`nivel.rec` y los `.inc` generados),
`work/output_orig` (bucle principal del juego) y `EeScheduler.cpp`. Lo que he comprobado yo esta en el anexo A; lo que es
hipotesis va marcado **[H]**.

## Resumen de la revision 2026-11-12 (10 lineas)
1. M0 cambia el techo: el GS por CPU **no pasa de ~30 V/s** (aislado 23-30, en vivo 38, escena pesada 21): ~28 ms/V son serie y no escalan (4 bandas 35,5 ms, 8 bandas 34,8). Para 60 hace falta GS ≤16,7 ms y para 120 ≤8,3: **con CPU no se llega**.
2. Por eso el **GS en GPU (Metal) entra como hito temprano (G2)**, en una via paralela al VU1 (otro worktree y otro build), verificado contra el rasterizador de CPU, que sigue siendo la referencia.
3. El VU1 compilado sigue primero: es el 92 % del hilo EE. Presupuesto en el mismo hilo: 60 V/s pide VU1 ≤9-11 ms (**x8-9,5**); con el VU1 en su hilo (T1), ≤15 ms (**x5,5**). 120 solo con T1: VU1 ≤7 ms (**x12**).
4. x12 son ~4 ns por ciclo del VU1 (~14 instrucciones de la CPU por par). Con el histograma (87 % FMAC util, MADD/MADDA/MULA con difusion, 53 % load/store) cabe en NEON solo si las banderas y el control de exponentes se hacen por bloque y no por FMAC (paso nuevo P7) **[H]**.
5. Corpus M0: 72 microprogramas en 5 escenas, los 8 primeros 96,5-97,3 % de ciclos, pero 10-15 % esta en programas de entrada 0000 que cambian con la escena: el generador compila por contenido (todas las imagenes vistas) con el interprete de respaldo; JIT/cache solo si los niveles 2+ lo piden.
6. D1 (oraculo FP: `-ffp-contract=off`, `-frounding-math`, `double` explicito) esta **en curso** en otro agente; despues, M0-a (referencias).
7. Tiempo del juego: logica a 50 pasos/s atada al VBlank. En el codigo, 53 funciones leen `dt`, pero **9 funciones fuera del bucle construyen el literal 0,02 y 13 el 50,0**: "VBlank 60 + dt 1/60" no sale gratis. Un experimento acotado (L0, 1-2 sesiones) decide entre eso e interpolar.
8. Para 120 lo realista es **interpolar el dibujo** (logica a 50, dibujo a 120): camara, `RwFrame` de entidades y huesos, con cortes de camara, teletransportes y UI como riesgos; y la parte de dibujo del EE tiene que caber en ~7 ms (medida M0-e).
9. Nuevo orden: D1 → M0-a/M0-e → P1-P6 (VU1) **en paralelo con** G0 → G1-a → D2 → G2 (GPU) → T1 → L0/L1 (60 en pantalla) → P7 + E1 + I1 (120).
10. Estimacion: 60 V/s en pantalla en ~40-90 sesiones de trabajo (camino critico ~25-55 con la via del GS en paralelo); 120 en ~55-110 (critico ~40-70), con hipotesis fuertes. Las incognitas que mas mueven el plan estan en 0.8.

---

## 0. Revision 2026-11-12: meta 60/120 tras M0

### 0.1 Que dice M0 y que cambia (contrastado con `PERFORMANCE_BASELINE.md`, seccion "M0 medir")
| Dato de M0 | Cifra | Consecuencia para el plan |
|---|---|---|
| GS aislado (`PS2X_GS_BENCH`, 4 bandas) | 32,6-44,5 ms/V = **23-30 V/s** (central 28) | el GS por CPU es el techo a partir de ~25-35 V/s |
| GS en vivo (pared del rasterizador en `SubmitMany`) | mediana 26,4 ms = 38 V/s; escena pesada 46,7 ms = 21 V/s | las dos medidas no cuadran ±15 %; mejor estimacion **~30 V/s** |
| Escalado con bandas | 1 banda 58,4 ms, 4 bandas 35,5, 8 bandas 34,8 → s ≈ 28 ms serie, p ≈ 30 ms paralelo | mas hilos no ganan nada; con infinitas bandas el techo seria ~36 V/s |
| Hilos | 1 rasterizador + 3 hilos de bandas = 4 de trabajo, en 8 nucleos logicos (4P+4E) | un hilo mas (T1) compite por los 4 P |
| Parte serie | ~8 000 primitivas por VSync en serie, todas por "textura solapa destino" (~3 Mpx, ~12 ms de pared, ~13 % de las muestras del rasterizador) | el aliasing de memoria del GS es el problema de fondo, en CPU y en GPU |
| Corpus VU1 (5 escenas del nivel 1) | 72 microprogramas en la union; 8 primeros = 96,5-97,3 % de los ciclos de cada escena; leave-one-out 86-100 % | la cola (10-15 %, entrada 0000 por objeto) cambia con la escena: compilar por contenido + interprete de respaldo |
| Histograma de pares | 133,0 M pares / 153,5 M ciclos (1,16 ciclos/par); superior 87,3 % FMAC util (MADDbc 14,6 %, MADDAbc 14,4 %, MULAbc 12,0 %); inferior load/store 52,7 % (LQ 29,4 %, SQI 10,5 %, SQ 7,0 %), IALU+banderas 19,4 %, branch 11,7 %, FDIV/SQRT 3,9 %, EFU ~0 | el VU1 es una transformacion de vertices: se mapea casi 1:1 a NEON (`fmul`/`fmla` por elemento, `ldr q`/`str q`); lo caro es lo que rodea a cada FMAC (banderas, normalizacion, horario) |

Obsoleto por M0 (se conserva abajo como historia): el "techo del GS 34-43 V/s" de §1.2 y del resumen original (es ~23-38, ~30); el
"G1 = GS por CPU para 50 y quiza 60" de §3 (para 60/120 el CPU no llega: queda G1-a como arreglo barato y referencia); "120 es un
proyecto aparte" y "el GS en GPU no va antes de que el VU1 deje de ser el cuello" de §4.3 y §6 (sustituido por G2 en paralelo).
Sigue valido: todo el diseno del recompilador (§2), P1-P6, la cadena de regresion (§5) y el analisis del bucle del juego (§4.1).

### 0.2 Presupuesto para 60 y 120
Modelo: fotograma = max(hilo EE, hilo VU1 si existe, GS). R = resto del EE (codigo del juego, desempaquetado del VIF, IOP, GIF) =
5-7 ms hoy; VU1 interpretado = 83 ms/V (1,79 M ciclos x 46 ns); margen de 1 ms para la variacion.
| Meta | Presupuesto | VU1 en el hilo EE (≤ presupuesto - R - 1) | Ganancia en el VU1 | VU1 en su hilo (T1; ≤ presupuesto - 1,5) | Ganancia | GS (hilo o GPU) |
|---|---|---|---|---|---|---|
| 50 | 20 ms | 12-14 ms | x5,9-6,9 | ≤18 ms | x4,6 | ≤18 ms: CPU no llega (35,5 hoy) |
| **60** | 16,7 ms | 8,7-10,7 ms | **x7,8-9,5** | ≤15 ms | **x5,5** | ≤15 ms: **GPU** (CPU solo con G1-a muy bueno **[H improbable]**) |
| **120** | 8,3 ms | 0,3-2,3 ms: **no** | x36+ | ≤7 ms, y R (la parte por fotograma) ≤7 ms | **x12** | ≤6 ms: **GPU** |

En ns por ciclo del VU1: hoy 46; x5,5 → 8,4 ns (~27 instrucciones de la CPU a 3,2 GHz por ciclo, ~31 por par); x8 → 5,8 ns
(~21 por par); x12 → 3,9 ns (**~14 por par**). Lectura:
- **60 sin T1** pide x8-9,5, que es el limite alto de lo que el plan original esperaba de P6 (x5,5-8,5). **60 con T1** pide x5,5,
  que P5/P6 deberian dar. Conclusion: **T1 entra en el camino de 60**, no es opcional.
- **120** pide T1 y x12. Con D1 en vigor el `MADD` es `fmul` + `fadd` (dos redondeos, sin `fmla`): por par quedan ~2 instrucciones
  de FMAC, 0-1 de mascara `dest` (`bsl`), 1-3 de la inferior (`ldr q`/`str q` + direccion, `add`, salto) y lo que cueste el control.
  Si el control de exponentes y las banderas van **por FMAC** (rango [64,190]/[26,253], signo/cero, pegajosas) suben ~6-10 por
  par y no se llega; **por bloque** (acumular una mascara de "fuera de rango" y comprobar una vez) si. Eso es P7 **[H]**.
- **R a 120**: con interpolacion la logica va a 50 pasos/s y solo la parte de dibujo del EE se paga en cada fotograma. Hoy no
  sabemos cuanto de los 5-7 ms es dibujo: lo mide M0-e. Con T1 el desempaquetado del VIF1 sale del hilo EE (va con el VU1).
- **Nucleos**: el M1 tiene 4 P. Hoy: EE + rasterizador + 3 bandas. Con el GS en GPU desaparecen las 3 bandas y el rasterizador
  queda como hilo ligero de envio; eso **libera nucleos P para T1**. Es otra razon para adelantar G2.

### 0.3 Nuevo orden de trabajo (sustituye al de §3; los pasos de §3 que no se mencionan siguen igual)
Dos vias que no tocan los mismos ficheros: **via VU1** (`src/lib/vu/*`, parches del VU1, `build/blade`) y **via GS**
(`src/lib/gs/*`, parche nuevo del backend GPU, **worktree propio con su build**, p. ej. `build/gpu`). Nunca dos agentes sobre el
mismo build ni el mismo parche (regla del ADR); las medidas de V/s se hacen de una en una con la maquina en reposo.

| # | Paso | Via | Quien | Depende de | Para que |
|---|---|---|---|---|---|
| 1 | **D1** oraculo FP | VU1 | Opus (**en curso**, otro agente) | — | P1 |
| 2 | **M0-a** referencias `qa/ref_det` con el oraculo de D1 | comun | Sonnet | D1 | toda la regresion |
| 3 | **M0-e** (nuevo) reparto de R (logica frente a dibujo en el hilo EE) e histograma del estado del GS | comun | Sonnet | — | 120, G2 |
| 4 | P1 → P2 → P3 → P4 → P5 → P6 (§3, sin cambios de fondo; P4/P6 cubren el corpus de 5 escenas) | VU1 | Opus / Sonnet | D1, M0-a | 30-55 V/s de capacidad del EE |
| 5 | **G0** (nuevo) clasificar las ~8 000 primitivas serie por VSync | GS | Sonnet | — | G1-a, G2 |
| 6 | **G1-a** (nuevo, sustituye a G1) comprobacion de solape exacta en la CPU | GS | Sonnet (+Opus si hay diferencias) | G0 | +5-10 V/s de techo de CPU y la regla de barreras de G2 |
| 7 | **D2** (nuevo) decidir la via de GPU: Metal propio frente a reaprovechar un backend | GS | Opus | G0 | G2 |
| 8 | **G2** (nuevo) backend del GS en GPU, v1 exacta y v2 rapida | GS | Opus (diseno, depuracion) + Sonnet (formatos y funciones, con banco) | D2 | 60 y 120 |
| 9 | **T1** VU1 en su hilo (§3) | VU1 | Opus | P5 | 60 |
| 10 | **L0** (nuevo) experimento del tiempo del juego | comun | Sonnet | — (cuanto antes) | elegir L1 o I1 |
| 11 | **L1** 60 con `dt` 1/60, o **I1** interpolacion del dibujo | comun | Opus | L0 | 60 en pantalla |
| 12 | **P7** (nuevo) VU1 en NEON con control por bloque | VU1 | Opus | P6 | 120 |
| 13 | **E1** (nuevo) dibujo del EE ≤7 ms | comun | Opus + Sonnet | M0-e | 120 |
| 14 | **I1** interpolacion a 120 (si L1 fue la via de 60, aqui se hace igualmente) | comun | Opus | L0 | 120 |

### 0.4 Hitos y V/s esperados (nivel 1, tramo 2600-3900, media; rangos)
"Capacidad" = 1000 / max(EE, VU1, GS) en ms, es decir los V/s si el ritmo no estuviera atado al VBlank de 50 Hz. Con la logica a
50 pasos/s, mas de 50 en pantalla solo se ve despues de L1/I1.
| Hito | Que lo cierra | VU1 (x sobre el interprete) | GS | Capacidad esperada | En pantalla |
|---|---|---|---|---|---|
| H0 hoy | `VU1_GEN` | x1,4 | CPU ~30 V/s | 14,6 (medido) | 14,6 |
| H1 | P2 (r1810) | x1,8-2,3 | CPU | 17-21 | 17-21 |
| H2 | P4 (8 programas, 96-97 %) | x3,2-4,0 | CPU (G1-a hecho o no) | 25-30 (lo limita el GS) | 25-30 |
| H3 | P5 + P6 + G1-a | x5,5-8,5 | CPU con G1-a: 30-40 **[H]** | 30-40 | 30-40 |
| H3' | H3 + G2 v1 | x5,5-8,5 | GPU (≤10 ms **[H]**) | 44-55 (lo limita el EE) | 44-50 |
| **H4 = 60** | T1 + G2 v2 + L1 o I1 | x5,5-8,5 en su hilo | GPU | **60-80** | **60** |
| **H5 = 120** | P7 + E1 + I1 + G2 v2 ≤6 ms | x12-15 | GPU | **100-140 [H fuerte]** | **120** (o 100-120 en escenas pesadas) |

### 0.5 GS en GPU (D2 y G2): diseno razonado
**Interfaz**: el runtime ya separa el frontend (`gs_frontend.cpp`: GIF, registros, armado de primitivas) del backend
(`GSRasterBackend` en `gs_backend.h`: `Submit(GSPrimitiveBatch)`, `LoadClut`, `BeginTransfer`/`UploadImage`, `Flush`,
`TextureFlush`, `Sync`, `Present`, `ClearFramebuffer`, `ConsumeLocalToHostBytes`, `ReadVram`/`WriteVram`, `SnapshotVram`).
`GSCpuBackend` (3 053 lineas) es una implementacion y `GSThreadedBackend` la pone detras de una cola. El backend de GPU es **otra
implementacion de `GSRasterBackend`** detras de la misma cola, elegida con una variable (`PS2X_GS_BACKEND=metal`), sin tocar el
frontend. El de CPU no se retira: es la referencia y el respaldo.

**Que se queda en la CPU**: el frontend entero (GIF, PATH1/2/3, registros, PRIM, armado de vertices), la cola del hilo del GS, las
transferencias host→local (se copian al buffer compartido), `ReadVram`/`SnapshotVram`/local→host (leen el buffer tras esperar a la
GPU) y, en la v1, la presentacion (`PresentFromLocalMemory` leyendo el buffer compartido: 640x512 son <1 ms). Ventaja del M1: la
memoria es unificada, asi que la VRAM del GS puede ser un `MTLBuffer` en `storageModeShared` que la CPU lee sin copia una vez
terminada la GPU.

**Modelo de GPU recomendado [H, lo decide D2]**: la VRAM de 4 MB **como buffer con el swizzle nativo del GS**, no como cache de
texturas del host (el estilo del renderizador HW de PCSX2). Rasterizado por **tiles en compute**: cada tile de pantalla (32x32)
es un threadgroup que recorre sus primitivas **en orden** y hace en el shader lo que hace `GSCpuBackend` (direccionamiento por PSM,
muestreo, CLUT, mezcla, Z, pruebas), con aritmetica entera donde la CPU es entera. Es la traduccion directa de las bandas de la CPU
a la GPU y es lo unico que permite igualar bit a bit a la referencia. El estilo "cache de render targets" es mas rapido de
arrancar pero no reproduce el aliasing y no se puede comparar con hashes.

**Estado del GS a reproducir** (la referencia es lo que hace `gs_cpu_backend.cpp`, no la PS2; G0/M0-e dicen que usa de verdad
AirBlade y eso se hace primero):
- **Primitivas**: punto, linea, tira de lineas, triangulo, tira, abanico, sprite; IIP (Gouraud/plano), FST (UV frente a STQ con
  perspectiva), XYOFFSET, SCISSOR; reglas de cobertura y de arriba-izquierda iguales a `DrawTriangle`.
- **Texturas**: TEX0 (TBP/TBW/PSM/TW/TH/TCC/TFX: modulate, decal, highlight, highlight2), formatos PSMCT32/24/16/16S y PSMT8/4/8H/4HL/4HH
  con su swizzle; CLUT (CBP/CPSM/CSM/CSA/CLD, cargada en `LoadClut` con la semantica de CLD), TEXA (TA0/TA1/AEM), CLAMP (repeat,
  clamp, region), TEX1 (filtro y MIP: solo lo que implemente `sampleT`), `TextureFlush`.
- **Pixel**: ALPHA (A/B/C/D/FIX), PABE, COLCLAMP, **FBA** (bit alto del alfa forzado), **dithering** (DTHE/DIMX, en PSM de 16 bits),
  FOG/FOGCOL, TEST (ATE/ATST/AREF/AFAIL, DATE/DATM, ZTE/ZTST), ZBUF (ZBP/PSM/ZMSK, Z de 32/24/16), FRAME (FBP/FBW/PSM/FBMSK).
- **Memoria**: transferencias host→local y local→local (BITBLTBUF/TRXPOS/TRXREG/TRXDIR), local→host, `ClearFramebuffer`.
- **Aliasing "textura solapa destino"**: el caso que hoy fuerza la serie. En el modelo por tiles, una primitiva que lee texeles
  escritos por primitivas anteriores **de otro tile** necesita una barrera (fin de pasada). Con 8 000 por VSync, una barrera por
  primitiva serian 8 000 pasadas: inviable. Regla: (1) G0 mide cuantas son solape real (rectangulo UV leido frente a rectangulo
  XY escrito, en paginas del GS) y cuantas son falsos positivos de la prueba conservadora actual; (2) las que leen solo de su propio
  tile, o de una zona que nadie escribe dentro del lote, van sin barrera; (3) las consecutivas que leen la misma zona fuente se
  agrupan en una sola pasada. **[H: si la mayoria son copias de un buffer a otro sitio (bloom, sombra, reduccion), quedan unas
  decenas de pasadas por VSync; si son realimentacion pixel a pixel, la GPU pierde mucho]**. G1-a aplica la misma regla en la CPU.
- **Presentacion**: PMODE, DISPFB1/2, DISPLAY1/2 y la mezcla de circuitos, como hoy (en la v1 lo sigue haciendo la CPU).

**Reaprovechar en vez de escribir (D2)** **[H, a comprobar en D2]**: (a) **paraLLEl-GS** (rasterizador del GS en compute de
Vulkan, pensado para igualar al GS por software y con seguimiento de riesgos de realimentacion; corre en PCSX2): via MoltenVK en
Mac; hay que confirmar licencia, que MoltenVK soporta lo que usa y el coste de alimentarlo desde nuestro frontend; (b) el renderizador
HW de PCSX2: GPL-3 y no exacto, descartado salvo como consulta; (c) backends de Play! (BSD): estilo HW, no exactos. D2 es un
prototipo de 1-2 sesiones (Opus): alimentar la opcion (a) con una captura de `PS2X_GS_BENCH` y comparar con la CPU, frente al
coste estimado del Metal propio. Si (a) no cuadra, Metal propio.

**Verificacion** (la GPU no puede romper el modo determinista ni la referencia):
1. **Banco del GS sin juego**: `PS2X_GS_BENCH` ya graba las ordenes del hilo del GS y la VRAM de partida. Se anade un modo de
   repeticion que ejecuta la misma captura en los dos backends y compara, **tras cada Present**, la VRAM entera y la imagen
   presentada. Es el equivalente de `BLADE_VU1_DIFF` para el GS: no hace falta EE ni VU1, ~segundos por captura, y se puede
   bisecar hasta la primera primitiva distinta (comparando tras cada `Submit` en el modo de depuracion).
2. **Criterio**: objetivo **bit a bit** en la VRAM (hash identico) para las rutas enteras; donde no se pueda (interpolacion en
   coma flotante del shader frente a la de la CPU), tolerancia declarada por funcion: % de pixeles distintos, diferencia maxima por
   canal (≤1-2) y PSNR ≥45 dB, mas un **hash tolerante** (imagen reducida y cuantizada, p. ej. 4 bits por canal tras 2x2) para que
   el modo determinista tenga algo comparable. Shaders compilados sin matematica rapida (`MTLMathModeSafe`).
3. **Modo determinista**: con el backend GPU, `qa_regression.sh` tiene que dar los **hashes de RAM identicos** (el GS no escribe la
   RAM del EE salvo local→host y CSR/SIGNAL/FINISH, que salen del frontend) y PNG dentro de la tolerancia frente a los de la CPU.
   En ese modo se espera a la GPU en cada Present.
4. **A ojo**: los ticks 100, 900, 3100 y 3160 en `qa_out/` (CPU frente a GPU) y, si difieren, PCSX2.

**Coste [H]**: D2 1-2 sesiones (Opus). G2 v1 (exacta, triangulos/sprites, 32 bits y paletas de 8/4, mezcla, Z, scissor, FBA,
dithering, transferencias, barreras por solape, sin optimizar): **8-15 sesiones** de Opus mas **4-8** de Sonnet (un formato o una
funcion por tarea, con el banco como criterio). G2 v2 (lotes por estado, menos pasadas, presentacion en GPU sin pasar por la CPU,
≤6 ms/V): 3-6 sesiones. Si D2 elige paraLLEl-GS: v1 en 5-10 sesiones **[H]**. Sin G2, 60 no se alcanza.

### 0.6 VU1 para 60 y 120 (cambios sobre §2-§3)
- **Orden**: P1-P6 igual. **P4 cubre el corpus de 5 escenas** (`work/vu1gen/corpus/e1-e5.rec`), no solo `nivel.rec`, y P6 compila
  **todo el contenido visto** (los 72 de la union y los que salgan del menu y de los niveles 2+) con el interprete de respaldo y
  `BLADE_VU1_MISS`. El objetivo de P6 pasa de "<0,5 % de ciclos interpretados" a medirlo **por escena** con leave-one-out: hoy, con 4
  escenas de entrenamiento, la que falta queda al 86-100 %, y por Amdahl (§1.4) un 10 % interpretado limita el VU1 a ~x5.
- **Si la cola sigue alta tras P6** (escenas de niveles 2+ con >5 % interpretado): opcion 1, generar desde las **imagenes de 16 KB
  subidas** (desensamblado estatico de todo lo alcanzable desde las entradas, no solo lo ejecutado; las imagenes se pueden sacar
  de las grabaciones o del disco) **[H: el CFG estatico de los programas por objeto puede tener saltos indirectos sin resolver]**;
  opcion 2, cache AOT persistente en disco: lo que falla en una partida se genera fuera de linea y se usa en la siguiente; opcion 3
  (la cara), JIT con `MAP_JIT`. Se decide con datos de niveles 2+ (Pendiente 7 del ADR).
- **T1 sube al camino de 60** (0.2). Con G2 hecho, los nucleos P los liberan las bandas del GS.
- **P7 (nuevo, para 120; Opus)**: NEON de verdad con el mix del histograma. `MADDbc`/`MADDAbc`/`MULAbc` → `fmul` por elemento
  (`v.s[i]`) + `fadd` (D1: sin fusionar); `dest` parcial → `bsl`; LQ/SQ/SQI → `ldr q`/`str q` sin normalizar mas que lo marcado;
  **control por bloque**: cada FMAC del bloque hace un OR de una mascara "exponente fuera de [64,190] o resultado fuera de
  [26,253]" (2-3 instrucciones) y al final del bloque, si la mascara no es cero, se restaura el estado de entrada del bloque y se
  repite por la ruta exacta (el estado son locales; los `SQ` del bloque van a un buffer y solo se publican al confirmar) **[H]**;
  MAC/estado solo donde la liveness de P5 lo pida; probar `FPCR.FZ` para el vaciado de denormales si da los mismos bits que
  `normalizeFmacResult` **[H]**. Criterio: corpus en 0 diferencias con `EXIT`/`FUZZ` (la mutacion de P3 fuerza la repeticion de
  bloques) y replay **x12** o mas frente al interprete.
- **VU1 en varios hilos**: sigue descartado (cadena secuencial, §1.3).

### 0.7 Tiempo del juego: 60 en pantalla
**Comprobado otra vez en `work/output_orig`** (recompilado, leido para esta revision):
- `0x132D10`: lee el contador del VBlank `[gp-0x7CE0]` en `0x132E30`, hace 1-4 pasos y despues dibuja una vez (`jal 0x12EB30`,
  `0x16B040`, `0x12EC80` en `0x1330D8`-`0x1330EC`). El VBlank `0x133CB0` solo suma 1.
- Cada paso: `dt = 0,02 x [0x70003FAC]` (`0x132E70`-`0x132E84`), guardado en `0x2FA558` y copiado a `0x70003FA8`; reloj escalado
  `[0x70003FB0] += dt`, **reloj sin escalar `[0x70003FB4] += 0,02` (literal, `0x132FFC`-`0x133028`)**; llama a `0x174CD0`, al
  objeto de `0x70003FCC` (`jalr`), a `0x132350`, `0x2393B0` y `0x247F10`.
- **Quien usa `dt`**: 53 funciones leen `[0x70003FA8]` (dt), 71 leen el reloj escalado `0x70003FB0` y 8 el sin escalar `0x70003FB4`.
- **Quien no lo usa**: **9 funciones fuera del bucle construyen el literal 0,02** (0x3CA3D70A): acumuladores `+= 0,02` en
  `0x174CD0` (llamada en cada paso) y `0x1A3AC0`, productos `x 0,02` en `0x143E00`, `0x1446F0`, `0x171920`, y otros usos en
  `0x178800`, `0x1A4C60`, `0x25CB90`, `0x25EA30`; y **13 funciones construyen 50,0** (0x42480000; `0x127740`, `0x1334B0`, `0x167400`,
  `0x22FC20`, ... **[H: no todas tienen por que ser tiempo]**). Ademas `[0x70003FAC]` (la escala) **la escribe el propio juego** en
  `0x167860`, `0x1685F0`, `0x168200` y `0x225040` (camara lenta o pausa **[H]**). Es un recuento de texto sobre el desensamblado;
  no he seguido los contadores enteros por paso (animaciones, IA, audio), que son la otra forma de "contar pasos".
- **Conclusion**: la logica **no** usa `dt` en todas partes. "VBlank a 60 con dt 1/60" pide al menos cambiar los 10 literales de
  0,02 (el del bucle y los 9), revisar los 13 de 50,0 y los contadores por paso; posible, pero no se sabe si basta **[H]**.

**L0. Experimento barato y acotado (Sonnet, 1-2 sesiones; Opus revisa el resultado)**: detras de `BLADE_EXP_60HZ=1` (sin cambiar
el modo por defecto ni las referencias):
1. VBlank del EE a 60 Hz (`kVBlankPeriod` de NTSC, que ya existio) y `dt` a 1/60 cambiando el literal de `0x132E74/78` y el de
   `0x132FFC/0x133008` (0x3C888889) con parches de instruccion de PS2Recomp (TOML), no editando el codigo generado.
2. **Variante A**: solo esos dos. **Variante B**: tambien los 9 literales de 0,02 a 1/60 y los 50,0 que sean tiempo a 60,0.
3. Medidas, con el mismo guion en tiempo de juego (los ticks del guion x1,2): (a) el cronometro del HUD: 10 s de juego en 600
   VSync; (b) la posicion del jugador (RAM de su `RwFrame`) a los mismos segundos de juego que una corrida a 50 Hz, en el tramo
   2600-3900 → diferencia ≤1-2 %; (c) la musica y los efectos frente a la imagen; (d) un video lado a lado con PCSX2 para ver
   animaciones, IA y particulas; (e) una sonda que cuente por paso las llamadas a las 9 funciones del literal (cuales corren en
   la partida).
4. **Regla de decision**: si la variante A o B pasa (a)-(d) sin nada a 1,2x ni a 0,83x → **L1 = 60 nativo con dt** (2-4 sesiones
   para dejarlo limpio, opcional por variable, con referencias propias del modo determinista). Si falla algo que no se explica con
   un literal mas → **I1 = interpolacion**. Coste del experimento: 1-2 sesiones; no toca la ruta por defecto.

**I1. Interpolacion del dibujo (Opus; obligatoria para 120)**. La logica sigue a 50 pasos/s; se dibuja a 60/120 con el estado
interpolado entre los dos ultimos pasos (`alfa` = fraccion del paso). Detalle de §4.2 (sigue valido), con lo que hay que interpolar:
- **Camara**: la matriz del `RwFrame` de la `RwCamera` (y la vista que se sube al VU1): posicion en lineal, orientacion con slerp de
  cuaterniones.
- **Entidades**: los `RwFrame` (y sus LTM) de jugador, tabla, peatones y vehiculos; los objetos estaticos no cambian.
- **Huesos**: la paleta de matrices del esqueleto (RpHAnim/RpSkin) **[H: hay que localizarla]**: mejor interpolar el tiempo del
  interpolador de animacion que cada matriz.
- **No se interpola**: HUD/UI (2D, se dibuja con el estado del ultimo paso), particulas al principio (se dibujan en el ultimo paso).
- **Riesgos**: teletransportes y reapariciones (salto grande de posicion → sin interpolar ese fotograma), **cortes de camara** (salto
  de posicion/orientacion por encima de un umbral, o el evento del juego si se encuentra → se ajusta sin mezclar), UI dibujada dos
  veces si el dibujo tiene efectos, y sobre todo **efectos de logica dentro del dibujo** (particulas que avanzan, animaciones que
  se actualizan en el render, el contador de la cola de sky2 `gp-0x7EE8`, numeros aleatorios).
- **I0 (prueba previa, Sonnet, 1 sesion)**: en modo determinista, llamar a la secuencia de dibujo dos veces con el mismo estado de
  logica y comparar la RAM (salvo los buffers de dibujo conocidos). Si cambia algo, la lista de lo que cambia es la lista de
  efectos de logica del dibujo. Decide cuanto cuesta I1 antes de empezarlo.
- **Coste [H]**: I0 1 sesion; I1 6-12 sesiones de Opus (ingenieria inversa de las listas de `RwFrame` y del esqueleto) + 2-4 de
  Sonnet (umbrales y comprobaciones). Verificacion: a 50 Hz con `alfa = 1` siempre, el modo determinista da los mismos hashes que
  sin I1; a 120, a ojo y contra PCSX2 a 50.

### 0.8 Estimacion honesta (rangos, en sesiones de trabajo de un agente)
| Paso | Sesiones | Quien | Ganancia | Hipotesis que lo rompe |
|---|---|---|---|---|
| D1 (en curso) + M0-a | 1 + 0,5 | Opus / Sonnet | 0 (-1-3 %) | — |
| M0-e | 1 | Sonnet | 0 | — |
| P1-P6 | 14-28 | Opus + Sonnet | VU1 x5,5-8,5 | coste por par de clang (15-40 instrucciones) |
| G0 + G1-a | 2-4 | Sonnet (+Opus) | GS CPU 30 → 30-40 V/s **[H]** | que las 8 000 sean solape real |
| D2 + G2 v1 + v2 | 15-30 (5-10 menos si sirve paraLLEl-GS) | Opus + Sonnet | GS ≤6-10 ms | barreras por solape; exactitud del shader |
| T1 | 4-8 | Opus | fotograma = max(EE, VU1) | carreras, orden del GIF, 4 nucleos P |
| L0 + L1 | 3-6 | Sonnet + Opus | 60 en pantalla | literales y contadores por paso |
| (o) I0 + I1 | 9-17 | Opus + Sonnet | 60/120 en pantalla | efectos de logica en el dibujo |
| P7 | 4-8 | Opus | VU1 x12-15 | control por bloque con `SQ` diferidos **[H]** |
| E1 | 3-10 | Opus + Sonnet | dibujo del EE ≤7 ms | lo que mida M0-e |
**Acumulado hacia 60**: D1 + M0 + P1-P6 + T1 + G0-G2 + L (L0+L1, o I0+I1) = **~40-90 sesiones de trabajo, camino critico ~25-55**
(la via del GS va en paralelo con la del VU1). La cifra baja supone que L1 sirve y que el VU1 da x7+. **Hacia 120**: + P7 + E1 + I1 +
G2 v2 ≤6 ms = **~55-110 sesiones, camino critico ~40-70**. Las cifras de V/s de 0.4 por encima de H3 son hipotesis: nada de G2, T1,
P7 ni I1 esta medido.
**Las 3 incognitas que mas cambian el plan**:
1. **Instrucciones de la CPU por par del VU1 compiladas** (hoy ~150 por ciclo): x6 frente a x12 decide si 60 sale sin T1 y si 120
   es posible. Se mide en P2 (primer programa) y P5.
2. **Que son las ~8 000 primitivas serie por VSync** (solape real o falso positivo; copias de buffer o realimentacion): decide el
   techo de G1-a en CPU, el numero de barreras de G2 y si la GPU baja de 6 ms. Se mide en G0 (barato, 1 sesion).
3. **Si la logica aguanta `dt` 1/60** (10 literales de 0,02, 13 de 50,0, la escala escrita por el juego, contadores por paso) y, si
   no, cuantos efectos de logica tiene el dibujo (I0): decide si 60 cuesta 3-6 sesiones o 9-17. Se mide en L0 e I0.
(Cuarta: cuanto de R es dibujo, M0-e; solo importa para 120.)

### 0.9 Pasos nuevos en detalle
- **M0-e (Sonnet)**: (1) con `sample` del hilo EE en el nivel 1 (VU1_GEN o interprete, da igual), separar el tiempo de los pasos
  de logica (bajo `0x132E70`-`0x1330B8`) del de dibujo (bajo `0x12EB30`/`0x16B040`/`0x12EC80`) y del desempaquetado del VIF1,
  restando el VU1; (2) un histograma del estado del GS por primitiva en una captura de `PS2X_GS_BENCH`: tipos de PRIM, PSM de
  FRAME/ZBUF/TEX0, TFX, ALPHA, TEST, FBA, DTHE, FOG, filtros de TEX1, CLAMP, y transferencias por direccion. Criterio: el reparto
  suma el 100 % del hilo EE ±5 %; el histograma cuenta todas las primitivas de la captura. Riesgo: bajo.
- **G0 (Sonnet)**: sobre la captura de `PS2X_GS_BENCH` (ticks ~3043-3140 y la de la entrada al nivel), para cada primitiva que va
  en serie por "textura solapa destino": tipo, tamano en pixeles, rectangulo UV leido y XY escrito en paginas/bloques del GS, PSM
  de los dos, y si el solape es real (algun bloque leido se escribe en la misma pasada) o solo de rango de TBP. Agrupar por estado
  (es probable que sean pocos efectos: sombra, reflejos, post-proceso **[H]**). Criterio: la suma da las ~8 000/VSync de M0-b.
- **G1-a (Sonnet; Opus si hay diferencias)**: cambiar la prueba de solape del rasterizador de CPU por la exacta de G0 (por bloques
  leidos frente a escritos). Verificacion: `PS2X_GS_AB=1` en 0 en una corrida completa, `qa_regression.sh` identico (la imagen no
  puede cambiar), y los ms/V del banco aislado antes y despues. Si una primitiva se mueve de serie a paralelo y cambia un pixel, la
  prueba esta mal: se para.
- **E1 (Opus + Sonnet)**: segun M0-e. Candidatos **[H]**: la espera de RenderWare `0x1DF500` (ya tiene override), el desempaquetado
  del VIF1 (sale del hilo con T1), funciones calientes del codigo recompilado del EE con overrides nativos verificados con el modo
  determinista.

### 0.10 Quien hace que, y briefs reutilizables para Sonnet
Regla (igual que §3): **Opus** para diseno, decisiones (D1, D2, L1/I1) y depuracion sin criterio cerrado (P1, P2, P5, P7, G2 v1,
T1, I1); **Sonnet** para lo acotado y verificable con un banco o una medida (M0-a, M0-e, G0, G1-a, P3, P4, P6, subtareas de G2,
L0, I0), con la orden de **parar y escalar** ante la primera diferencia que no entienda.

**Brief base (todas las tareas de Sonnet)**: "Recomp de AirBlade en `~/airblade-recomp`, rama main (es la RECOMP, no el decomp
`~/airblade-decomp`: no lo toques). Arranque: `manage_adr get Users-home-airblade-recomp`, `git log -5`, `git status`; lee
`docs/PERFORMANCE_BASELINE.md` (seccion M0) y `docs/PERFORMANCE_PLAN.md` §0 y el paso `<ID>`. Tarea unica: `<texto del paso>`. Reglas:
los cambios del runtime van como parche en `patches/` y pasan `scripts/check_patches.sh`; el runner en `runner/`; no edites el
codigo generado ni las cabeceras; no limpies `build/blade` (ni `build/gpu`) ni pares compilaciones ajenas; una corrida a la vez con
load average <3; no cambies el modo por defecto (todo detras de una variable). Criterio de aceptacion: `<criterio del paso>`. Si algo
no cuadra o aparece una diferencia que no entiendes, para y dilo con las 5 primeras diferencias: no la tapes. Entrega: tabla de cifras
con comando, commit y load average, y las rutas de los artefactos; commit solo de tus ficheros, sin coautor ni mencion de IA, sin
push. Responde en espanol."
Lineas de tarea para el brief base:
- **M0-a**: "regenerar `qa/ref_det` con el build de D1; criterio: dos corridas deterministas identicas y el commit de la referencia
  anotado en un fichero de texto".
- **M0-e**: el texto de 0.9; criterio de 0.9.
- **G0**: el texto de 0.9; "trabaja sobre las capturas de `PS2X_GS_BENCH`, sin cambiar el rasterizador".
- **G1-a**: el texto de 0.9; criterio `PS2X_GS_AB=1` en 0, `qa_regression.sh` identico y ms/V del banco aislado.
- **P3, P4, P6**: los briefs de §3 (siguen validos), con el corpus `work/vu1gen/corpus/e1-e5.rec` ademas de `nivel.rec`.
- **G2-sub (una por formato o funcion)**: "implementar en el backend de GPU `<PSM o funcion: PSMT4 con CLUT CSM1, ALPHA, DATE...>`
  copiando la semantica de `gs_cpu_backend.cpp`; criterio: banco del GS (repeticion de capturas en los dos backends) con VRAM
  identica en todas las capturas que usan esa funcion, o dentro de la tolerancia declarada en 0.5 si es una ruta de coma flotante;
  no toques el frontend ni el backend de CPU; worktree y build propios".
- **L0**: el texto de 0.7 (variantes A y B, medidas (a)-(e)); "no decides L1/I1: entregas la tabla y la decides Opus".
- **I0**: el texto de 0.7; "entrega la lista de direcciones de RAM que cambian entre las dos llamadas al dibujo, agrupadas por
  funcion que las escribe".

---

## Resumen original (a1eedd5; sustituido por el de arriba el 2026-11-12, se conserva como historia)
1. En el nivel 1 el fotograma dura 87,8 ms: ~83 ms son VU1 (~46 ns por ciclo del VU1) y ~5-7 ms el resto del EE. Para 50 V/s el VU1 tiene que bajar a ~13 ms: **x6,5**.
2. El coste esta en lo que cuesta cada ciclo, no en cuantos hay ni en cuantas llamadas: el gasto fijo por llamada es <1 % y solo el 0,5 % de las llamadas repite entrada (lo he medido en `nivel.rec`), asi que no hay cache posible.
3. **El GS no aguanta 50 tal cual [H fuerte]**: el hilo del rasterizador gasta 23-29 ms por fotograma (26,5 % a 11,4 V/s; 42,5 % a 14,6). Su techo ronda 34-43 V/s. Primero hay que medirlo (M0-b).
4. El recompilador que rinde compila el **programa entero por punto de entrada** a C++ AOT: VF/VI en variables locales, horario de ciclos y riesgos calculado al generar, y solo las banderas que alguien lee.
5. Es viable porque cada llamada empieza con las tuberias vacias (30 000 de 30 000 en `nivel.rec`) y porque, en el tramo compilado, 625 FMAC escriben banderas MAC/estado y solo 9 `FMAND` y ninguna instruccion de estado las leen.
6. Todo se verifica contra el interprete con `BLADE_VU1_DIFF`: diferencias 0 en registros, ciclos, memoria y GIF sobre un corpus de grabaciones. Antes de generar codigo se valida el modelo de ciclos.
7. Antes hay que decidir el oraculo: en arm64 el interprete funde `MADD`/`MSUB` (`fmadd`/`fmsub` en `execUpper`) y usa `long double` = `double`, asi que no da lo mismo en x86. Es la decision D1 (Opus).
8. Orden: M0 medidas → D1 → modelo de ciclos → r1810 → los 8 grandes → eliminar banderas → integracion → cola → GS → hilo del VU1. Estimacion acumulada: 40-55 V/s si el GS acompana.
9. 60/120: el juego avanza la logica en pasos fijos de 1/50 s (`dt = 0,02`, entre 1 y 4 pasos por fotograma segun los VBlank pasados) y dibuja una vez. Ir a 60 de verdad pide interpolar o cambiar el paso; las dos cosas son investigacion **[H]**.
10. 120 pide un VU1 ~15-20x, el VU1 en su hilo y el GS en GPU (Metal): es un proyecto aparte, no un paso mas de este plan.

---

## 1. Presupuesto

### 1.1 Donde se va el fotograma hoy (nivel 1, VSync 2600-3900)
| Pieza | Interprete puro (11,4 V/s) | VU1_GEN actual (14,6 V/s) | Fuente |
|---|---|---|---|
| Fotograma medio | 87,8 ms | 68,7 ms | `[blade:frames]` |
| VU1 (hilo EE) | ~83 ms (2,41 G ciclos / 1 347 VSync x 46 ns) | ~60 ms (87 % del EE) | `PS2X_VU1_PROFILE`, `sample` |
| Resto del EE (codigo del juego, desempaquetado del VIF, IOP, GIF) | ~5-7 ms | ~7-9 ms | por diferencia |
| Hilo del rasterizador del GS (en paralelo) | 26,5 % de un nucleo → **~23 ms/fotograma** | 42,5 % → **~29 ms/fotograma** | `sample` |
| 3 hilos de bandas (en paralelo) | 18-19 % cada uno → ~16 ms/fotograma cada uno | 25-28 % → ~18 ms | `sample` |

Lo que manda es el hilo mas cargado: fotograma = max(EE, rasterizador, bandas), porque el GS va en tuberia con el EE.

### 1.2 Que hace falta para 50 V/s (20 ms)
- **VU1**: 83 ms a ≤13-15 ms (20 menos 5-7 del resto del EE): **x5,5-6,4**. El README dice x6,5 con el resto a 7 ms, y cuadra.
- **Resto del EE**: no es el cuello mientras el VU1 vaya en el mismo hilo. Con 5-7 ms cabe en 20 ms; para 60 V/s (16,7 ms) ya obliga a x8,6 en el VU1 o a mover el VU1 a otro hilo (seccion 4).
- **GS**: el hilo del rasterizador necesita 23-29 ms por fotograma, mas que los 20 ms. **Si las cifras de `sample` son trabajo de verdad, el GS por CPU se queda en 34-43 V/s.** Lo confirman por otro lado las medidas de dev 2: en modo determinista `SubmitMany` ocupaba 102 s de pared en una corrida de 116 s (88 %) que iba a 34 V/s de media, y en tiempo real la partida no paso de 32 V/s. Hace falta x1,2-1,5 en el hilo del rasterizador para 50, x1,4-1,8 para 60 y x3-4 para 120. Que "el GS va holgado" (BASELINE) solo vale a 11-15 V/s.
  **[2026-11-12, M0-b]**: medido. GS aislado 23-30 V/s, en vivo 38, escena pesada 21; ~28 ms/V serie. El techo real es ~30 V/s,
  peor que los 34-43 de arriba, y con CPU no se llega a 60 (ver §0.1-0.2).
- **Margen del M1**: tiene 4 nucleos de rendimiento y 4 de eficiencia (comprobado: `hw.perflevel0/1.physicalcpu` = 4/4). EE, rasterizador, 3 bandas y el hilo principal ya son 6 hilos con trabajo. Un hilo mas para el VU1 caeria en un nucleo de eficiencia (~1/3 de velocidad) salvo que se ajuste la QoS o se quite una banda. Esto hay que medirlo, no suponerlo.

### 1.3 Coste por ciclo frente a numero de ciclos y de llamadas
- **Ciclos**: 1,79 M ciclos del VU1 por VSync son el trabajo real del juego. Un VU1 de PS2 da 5,9 M ciclos por VSync PAL (294,912 MHz / 50), asi que el juego lo usa al ~30 %. No se puede reducir sin cambiar lo que dibuja.
- **Llamadas (MSCAL/MSCNT)**: ~890 por VSync, de ~2 100 ciclos de media (comprobado: 63,37 M ciclos en 30 000 llamadas). Las decide el flujo del VIF del juego (lotes de vertices). Con un gasto fijo de 0,2-0,5 us por llamada (`Vu1RecScope`, `fesetround` dos veces, `commitReadyPipelines`, `flushPipelines`, eleccion del programa) salen **0,2-0,45 ms por VSync: <1 %**. Reducir llamadas no compensa.
- **Cache de resultados**: en `nivel.rec`, 162 de 30 000 llamadas (0,5 %) repiten estado de entrada y datos subidos (anexo A). Cada lote trae vertices nuevos (~3,3 KB subidos por llamada) y matrices que cambian cada fotograma. **Descartada.**
- **VU1 en paralelo con el EE (Plan B del README)**: el VU1 no tiene con que solaparse mientras el resto del EE sean 5-7 ms frente a 83 ms (como mucho -7 %). Cuando el VU1 baje a ~13 ms, el solape da max(13, 7) en vez de 20 ms, y eso es lo que permite 60 sin pedirle x8,6 al VU1. Va despues del recompilador.
- **VU1 en paralelo consigo mismo**: no. Cada reanudacion sigue con el estado de la anterior (VF/VI, punteros de TOP/ITOP), una cadena estrictamente secuencial **[H razonable; no lo he probado llamada a llamada]**.
- **Conclusion**: ~99 % del problema es el coste por ciclo del VU1 (unas 150 instrucciones de la CPU por ciclo del VU1, 46 ns a ~3,2 GHz). Para x6,5 hay que bajar a ~23 por ciclo.

### 1.4 Ley de Amdahl de la cobertura
Con un compilado k veces mas rapido sobre una fraccion f de los ciclos, el VU1 gana 1/((1-f) + f/k):
| f (ciclos compilados) | k = 4 | k = 6,5 | k = 10 |
|---|---|---|---|
| 0,90 (los 5 primeros) | x3,1 | x4,1 | x5,3 |
| 0,96 (los 8 primeros) | x3,6 | x5,1 | x7,4 |
| 0,995 (todos los de la muestra) | x4,0 | x6,3 | x9,6 |
El 4 % de cola en el interprete cuesta mucho. **Hay que compilar todos los microprogramas** que salen en el corpus, y la generacion tiene que ser automatica, no a mano por programa.

---

## 2. Diseno del recompilador de microprogramas del VU1

### 2.1 Por que no basta lo actual
`VU1_GEN` genera un `case` por par que llama a `stepPairT<true>`: el mismo paso del interprete con la decodificacion como constante. Cada par sigue pagando `calculatePairReadyCycle` (un bucle sobre hasta 16 ranuras de lectura), `markPairWrites`, `noteEagerWrite`, una entrada de 48 bytes en la cola de banderas por FMAC, `commitReadyPipelines` cada ciclo, la normalizacion de 12 operandos, la guarda de 8 bytes y el `switch` por PC. Ademas, el estado vive en `m_state`, en memoria. Por eso da x1,44 en el replay y x1,28 en el juego.

### 2.2 Nivel de compilacion: programa entero por (PC de entrada, contenido ejecutado)
- **Unidad**: una funcion C++ por microprograma, es decir por PC de entrada (0x0000 de `execute`; 0x1810, 0x1e98, 0x1e58, 0x2da8... de `resume`) y hash de los pares alcanzables desde ella. Dentro van los bloques basicos con `goto`/etiquetas. Asi clang ve el bucle de vertices entero (160-560 pares por llamada) y puede tener VF/VI en registros en todo el bucle.
- **Saltos**: `B`/`BAL`/`IBxx` van directos. `JR`/`JALR` usan un `switch` sobre los destinos vistos (direcciones de vuelta de `BAL`), y el `default` sale al interprete.
- **Por que no bloques sueltos ni trazas**: en el limite de un bloque o una traza hay que volcar a memoria los registros y el estado de las tuberias. El bucle de vertices es justo lo que conviene tener entero, y el CFG de estos programas de RenderWare es pequeno: 992 pares en todo el tramo 0x0-0x1f00.
- **Guarda**: una por llamada, no por par. Al cambiar `getVU1CodeGeneration()` (501 subidas de microcodigo en 30 000 llamadas, ~8 por fotograma) se calcula el hash del tramo y se elige el programa. Despues, cada `resume` mira una tabla (PC de entrada → funcion) para esa generacion. Si no hay programa, interprete.

### 2.3 Estado al entrar y al salir: llamadas que empiezan con las tuberias vacias
- En las 30 000 llamadas de `nivel.rec` el indicador `clean` vale 1: al acabar no queda nada pendiente, porque con el bit E `run()` llama a `flushPipelines`. Cada llamada empieza con las tuberias vacias. **El estado de las tuberias al entrar es conocido y constante**, y eso hace posible el horario estatico. La guarda lo comprueba (`pipelinesPending()==false` al entrar) y, si falla, deja la llamada al interprete.
- **Salidas a mitad** (presupuesto de 65 536 ciclos agotado, `JR` a un destino desconocido o un bloque que no se compilo): el generador conoce en cada borde el estado estatico de las tuberias (que entradas de banderas, Q, P y escrituras siguen pendientes y en que ciclo) y emite el codigo que lo **materializa** en `m_flagPipeline`/`m_fdiv`/`m_efu`/`m_vfReady`/`m_viReady`, para que el interprete continue en el mismo estado. Esto se prueba con una salida forzada (paso P2, `BLADE_VU1_DIFF_EXIT=N`).
- **Bloques que el corpus no ejecuta**: no se compilan. Salen al interprete con materializacion. Asi nunca se ejecuta codigo compilado que el banco no haya visto.

### 2.4 Modelo de latencias y ciclos: horario calculado al generar
- El generador ejecuta en abstracto, con el **decodificador del propio interprete** (`decodeInstructionPair`, `InstructionUsage`: latencias, lecturas y escrituras por carril, `waitQ`/`waitP`, `PipelineFdiv`/`Efu`/`Xgkick`), el mismo calculo que `calculatePairReadyCycle`/`markPairWrites`/`commitReadyPipelines`. No se escribe una tabla nueva de latencias: se reutiliza el interprete, que es el oraculo.
- Sale, por bloque y por **firma de pendientes al entrar** (que registros, banderas, Q y P siguen pendientes y cuantos ciclos les faltan; la latencia mas larga es 13, la de RSQRT, y la cola tiene como mucho 8 entradas, asi que es finita), lo siguiente:
  - el ciclo de emision de cada par y los ciclos del bloque (una constante que se suma a `m_cycle`);
  - **que valor ve cada lectura**: si una instruccion lee Q antes de que acabe el `DIV` (7 ciclos), ve el Q antiguo, y el generador sabe cual es. Lo mismo para P (EFU), para MAC/estado/clip (4 ciclos) y para la copia de VI que ven los saltos (`readBranchVi`);
  - la firma de salida, que es la de entrada de los sucesores. En los bucles se itera hasta un punto fijo: lo normal son 1-2 firmas por cabecera (la primera vuelta y las demas).
- Si un bloque tiene demasiadas firmas (>4), se genera una variante general con los tiempos de listo en un pequeno array local (como hoy, pero solo para los registros pendientes en ese borde) **[H: no espero que pase en estos programas]**.
- **Ciclos**: el VU1 corre sincrono y su cuenta de ciclos **no alimenta el reloj del EE** (`ps2_runtime.cpp`: los callbacks MSCAL/MSCNT no consumen ciclos del EE). Solo se observan en el estado (que el banco compara), en el presupuesto de 65 536 (no se alcanza: ~2 100 por llamada) y en el XGKICK. Se mantienen exactos porque salen gratis del horario estatico y porque el banco los compara.
- **XGKICK**: el interprete copia 16 bytes cada 2 ciclos. En compilado, el kick guarda su ciclo de inicio y se "pone al dia" (`progressXgkickTo(ciclo)`, el mismo bucle del interprete) solo en tres sitios: ante un `SQ`/`SQI`/`SQD`/`ISW` cuya direccion cae en el tramo que falta por copiar, ante el siguiente XGKICK (que en el interprete espera) y al final. El resultado es el mismo y el coste baja de uno por ciclo a uno por evento. Hay 1,45 paquetes por llamada.

### 2.5 Asignacion de registros
- En C++, `VF` como `float32x4_t` locales (NEON), `VI` como `int32_t`, y `ACC`, Q, P, I, R, MAC, estado y clip tambien locales. Se cargan de `m_state` al entrar y se guardan al salir y en cada salida a mitad. La asignacion fina se la dejamos a clang, que con 32 registros vectoriales en AArch64 llega de sobra para los 16-24 VF que usa un programa de RenderWare **[H: mirar los VF vivos por programa en P1]**.
- Hay que evitar el alias: la memoria de datos del VU1 se lee y se escribe con `memcpy` desde `vuData` y las locales no tienen la direccion tomada. Asi clang no tiene que suponer que un `SQ` pisa un VF.
- `vf0` es una constante (0,0,0,1) y `vi0` vale 0. Las escrituras con mascara `dest` se hacen con `vbslq` o un `select`.

### 2.6 Banderas MAC, estado y clip
- **Medida estatica del tramo compilado** (variante 0, 992 pares): 625 FMAC escriben MAC y estado; leen MAC 9 `FMAND`; leen estado (`FSAND`/`FSEQ`/`FSOR`/`FSSET`) **0**; leen clip 40 (`FCAND` 27, `FCOR` 9, `FCGET` 4) frente a 26 `CLIP`. Variante 1: 485 FMAC, 9 `FMAND`, 0 de estado y 28 de clip.
- **MAC**: con el horario estatico se sabe que FMAC ve cada `FMAND` (la ultima cuyo ciclo de listo sea ≤ el de la lectura) y que bits le importan (el inmediato de `FMAND`). Solo se calcula la MAC de esas FMAC y la de la ultima antes de cada salida, porque el estado final pasa a la llamada siguiente y el banco lo compara.
- **Estado**: las banderas actuales (Z, S, U, O) solo importan en las salidas. Las pegajosas son el OR de todas las FMAC desde el ultimo `FSSET`, asi que hay que acumularlas. Basta un OR vectorial por FMAC: signo y cero de los carriles de `dest` con 2-3 instrucciones NEON. U y O solo se activan en la ruta lenta (2.7).
- **Clip**: se mantiene tal cual (`CLIP` desplaza 6 bits y `FCAND`/`FCOR` lo leen). El coste es pequeno y la semantica la da el interprete.
- `FSSET`/`FCSET` y las demas que escriben banderas siguen la misma regla: se calculan solo si algo lo lee o si llegan a una salida.

### 2.7 NaN, infinito y exponente 255 (la leccion de AB-004)
Hay que reproducir **la semantica del interprete**, no la del IEEE del host:
- **Operandos**: `normalizeOperand` lleva el exponente 0 a cero con signo y el 255 a ±FLT_MAX (0x7F7FFFFF) en cada lectura de VF, ACC, Q e I por FMAC (12-16 normalizaciones por par en el interprete). En compilado se lleva por registro la marca "normalizado". Un resultado de FMAC ya sale normalizado (el exponente 255 y el desbordamiento van por la ruta exacta y se acotan), asi que la marca solo se pierde con `LQ`/`LQI`/`LQD`, `MOVE`/`MR32` desde un registro sin marca, `MFIR`, `MFP` y la entrada. Solo esos se normalizan, y solo una vez. La memoria guarda los bits crudos (un `SQ` de un valor cargado sin tocar conserva su 0x7FFFFFFF).
- **Resultados**: la ruta rapida (`fastFmacFlags`) se vectoriza. Si todos los exponentes de los operandos estan en [64,190] y los de los resultados en [26,253] (o el resultado es cero exacto), solo hay banderas S y Z y el valor no cambia. Si no, sale una rama fria a la funcion exacta del interprete (`normalizeFmacResult` + `calculateFmacProductSticky`) para esa instruccion. **Nunca se compara en el host un valor que pueda ser NaN**: AB-004 fue un `c.lt.s` sobre FLT_MAX^2 = 0x7FFFFFFF leido como NaN.
- **`MAX`/`MINI`/`CLIP`/`FTOI`**: en el interprete comparan operandos ya normalizados (`MAX`, `MINI`) o bits enteros (`CLIP`), y el compilado hace exactamente lo mismo. Que `MAX` normalice un 0x7FFFFFFF a FLT_MAX en vez de copiarlo crudo puede no ser lo que hace la PS2 **[H, pendiente del ADR: "MAX.S/MIN.S y VU con exponente 255"]**. Si se corrige, se corrige en el oraculo (D1) y se regeneran las referencias, nunca solo en el compilado.
- **Redondeo hacia cero**: `run()` pone `FE_TOWARDZERO` por llamada y NEON respeta el FPCR. Las unidades del VU (interprete y generado) tienen que compilarse con `-frounding-math` (o `#pragma STDC FENV_ACCESS ON`) para que clang no pliegue ni mueva operaciones float suponiendo redondeo al mas cercano.
- **Pruebas especificas**: un modo de mutacion del banco (`BLADE_VU1_DIFF_FUZZ`, paso P3) toma estados reales grabados y mete en VF, ACC, Q, I y la memoria valores con exponente 0, exponente 255, ±0, 0x7FFFFFFF y FLT_MAX^2. Como A es el oraculo, la entrada no tiene que ser realista. Es lo que habria cazado AB-004 dentro del VU.

### 2.8 Decision previa D1: que es el oraculo (FMA y `long double`)
- En el binario Release de arm64, `VU1Interpreter::execUpper` contiene 35 `fmadd`/`fmsub` (anexo A): clang contrae `acc[c] + vs[c] * bc` (por defecto `-ffp-contract=on` y no hay `-ffp-contract=off` en el CMake). **El interprete actual hace MADD/MSUB fusionados**, con un solo redondeo. El VU de PS2 no fusiona. Un build x86 sin FMA daria otros bits.
- `calculateFmacExactResult` usa `long double`, que en arm64 es `double` y en x86/Linux es de 80 bits. Las banderas O/U del borde pueden cambiar segun la plataforma.
- Hay dos opciones: (a) el compilado imita el oraculo actual (usa `vfmaq_f32` en `MADD`/`MSUB`); o (b) **primero se corrige el oraculo** (`-ffp-contract=off` en `src/lib/vu/*`, `double` explicito en lugar de `long double`), se regeneran `qa/ref_det` y las grabaciones, y despues se compila. **Recomiendo la (b)**: es mas fiel a la PS2, da los mismos bits en Mac, Windows y Linux, y cuesta un dia de regresion. La decide Opus, y si cambia la imagen se mira contra PCSX2.

### 2.9 AOT o JIT
- **AOT** desde las grabaciones, como `BLADE_VU1_GEN` hoy: el C++ generado se queda en `work/vu1gen/` (no se versiona) y se compila con el runner. Son 60 imagenes de 16 KB y 32 microprogramas distintos por contenido ejecutado en el nivel 1. Con un corpus de varias escenas y niveles se cubren casi todos.
- **Lo que cambie en tiempo de ejecucion** (una imagen nueva en otro nivel) va al interprete y queda en un registro de fallos (`BLADE_VU1_MISS`: PC, hash, ciclos) para grabarlo y generarlo despues. No hay JIT: en macOS pide `MAP_JIT` y un emisor propio (sin LLVM), es mucho trabajo y no se podria verificar igual de bien. Solo lo reconsideraria si los niveles 2+ cargan cientos de variantes **[H: no lo sabremos hasta grabarlos]**.
- **Contenido ejecutado frente a imagen**: se agrupa por hash de los pares alcanzables, no de los 16 KB. Imagenes que solo difieren en datos o en codigo que no se ejecuta comparten funcion (32 frente a 60).

### 2.10 Por que este diseno se puede verificar
- El banco ejecuta, desde el mismo estado real, A (interprete puro) y B (lo que elige `selectGenProgram`), y compara bit a bit registros por carril, MAC/estado/clip, Q/P, ciclos, PC, la memoria de datos entera y los paquetes GIF. Lo unico que este diseno cambia de sitio (horario, banderas, normalizacion, XGKICK) cae en esos campos.
- La primera comprobacion (P1) no necesita generar codigo: el horario estatico tiene que predecir **los ciclos exactos de cada llamada grabada** recorriendo los bloques que ejecuto A. Si el modelo de ciclos falla, se ve ahi.
- Las salidas a mitad se prueban forzandolas (P2), y los valores raros con la mutacion (P3).
- Lo que el banco no cubre: los programas que no estan en el corpus (van al interprete por la guarda) y la fidelidad frente a la PS2 real (es otra cosa: PCSX2).

---

## 3. Orden de trabajo

**[2026-11-12]** El orden de esta seccion queda sustituido por §0.3 (meta 60/120). Estado: M0-b, M0-c y M0-d (parcial) hechos
(d2044e4); M0-a pendiente de D1; **D1 en curso** (otro agente); G1 sustituido por G1-a + G2 (§0.5, §0.9). Los pasos P1-P6 y T1 y sus
briefs siguen validos.

Reglas para todos los pasos (las del ADR): los cambios del runtime van como parche en `patches/` y se comprueban con
`scripts/check_patches.sh`; el runner va en `runner/`; no se limpia `build/blade`; el codigo generado no se versiona; commits sin
coautor ni mencion de IA y sin push. **Quien**: **Opus** es diseno o depuracion sin criterio cerrado; **Sonnet** es acotado, con un
criterio que da el banco o una medida, y con la orden de parar y escalar ante la primera diferencia que no entienda.

Las medidas de V/s son siempre en modo normal, con la maquina en reposo (load average <3), `BLADE_HIDDEN=1 BLADE_STATS=1
BLADE_EXIT_TICK=3950` y el guion de la linea base, tramo 2600-3900, **2 corridas**, y se da la media y el minimo de 1 s. La
regresion es en modo determinista (`scripts/qa_regression.sh`, hashes de RAM y de fotograma cada 50 ticks hasta el 3950).

### M0. Medir antes de tocar (Sonnet, 4 tareas independientes; no van en paralelo sobre el mismo build)
- **M0-a. Referencias**: regenerar `qa/ref_det` con el build actual (esta en Pendiente 6 del ADR). Criterio: dos corridas
  deterministas identicas entre si, y la referencia guardada con el commit en un fichero de texto. Riesgo: bajo.
- **M0-b. Techo del GS**: medir el coste del GS por fotograma en el nivel 1 con la ruta `VU1_GEN` actual: `PS2X_GS_PROFILE=1`
  (tiempo de pared de `SubmitMany` por VSync), `sample` de 10 s separando el hilo del rasterizador (trabajo propio frente a espera
  de las bandas) y `PS2X_GS_RASTER_THREADS=2/3/4/5`. Sale el techo de V/s del GS = 1000 / (ms del hilo mas cargado por
  fotograma). Criterio: dos cifras que cuadren (perfil y `sample`) con ±15 %. **Cambia el plan**: por debajo de 50 V/s, el paso G1
  sube por delante de P6. Riesgo: confundir espera activa con trabajo, asi que hay que mirar el arbol de llamadas.
- **M0-c. Corpus de grabaciones**: con `PS2X_VU1_NOGEN=1`, grabar 30 000 llamadas en el menu (~800), la escena de entrada
  (~2000), y el nivel 1 hacia ~2700, ~3200 y ~3700, en `work/vu1gen/corpus/` (sin versionar). Criterio: para cada fichero,
  `BLADE_VU1_DIFF_B=interp` con 0 diferencias y 0 no reproducibles, y una tabla de microprogramas por fichero. Riesgo: disco
  (~150 MB por fichero).
- **M0-d. Histograma dinamico**: anadir al banco (A) un recuento por par ejecutado (veces, ciclos de espera antes del par y tipo de
  instruccion) y sacar, por microprograma, los pares ejecutados frente a los ciclos, las FMAC por ciclo, las lecturas de banderas
  ponderadas y las esperas por Q/P/riesgo. Ficheros: `ps2recomp-zzz-vu1-diffbench.patch` (o un parche `zzzz-` nuevo) y
  `runner/blade_devtools.cpp`. Criterio: la suma de ciclos por programa igual a la de la tabla actual. Riesgo: bajo. Sirve para
  que P4 ataque lo que pesa.

**Brief Sonnet reutilizable (M0-b/c/d):** "Recomp de AirBlade en `~/airblade-recomp` (no el decomp). Lee el ADR
(`manage_adr get Users-home-airblade-recomp`), `docs/PERFORMANCE_BASELINE.md` y `docs/PERFORMANCE_PLAN.md` §3 M0-<x>. Tarea
unica: <texto del paso>. No cambies el interprete ni la semantica; los cambios del runtime van en `patches/` y pasan
`scripts/check_patches.sh`. No limpies `build/blade`. Una corrida a la vez, maquina en reposo. Entrega: tabla de cifras con
comando, commit y load average; commit sin coautor ni mencion de IA, sin push. Si algo no cuadra, para y dilo."

### D1. Decidir el oraculo (Opus) — antes de P1 — HECHO (2026-11-12, opcion b)
**Resultado**: parche `patches/ps2recomp-zzzzz-vu1-oracle.patch` (`check_patches.sh` identico): `-ffp-contract=off -frounding-math` en los
`.cpp` de `src/lib/vu/` y en las unidades generadas, y `double` en lugar de `long double`. `objdump -d`: 0 `fmadd`/`fmsub`/`fnm*`
en todas las funciones del VU1 (antes 35 en `execUpper`). Banco `BLADE_VU1_DIFF` sobre `nivel.rec` y `corpus/e1-e5.rec`
(135 000 llamadas): **0 diferencias A!=B** con `B=interp` y con `B=VU1_GEN` actual (build con `PS2X_VU1GEN_DIR=work/vu1gen`, que
se volvio a dejar vacio). **Cambio de referencia respecto a lo grabado**: el nuevo A no reproduce las grabaciones antiguas en
29 999 de 30 000 llamadas de `nivel.rec` (28 238 en `vf`, 25 en `acc`, 1 736 sin campo concreto, es decir solo cambia la memoria
o el GIF) y en 15 000 de 15 000 de cada e1-e5 (campos `vf`, `acc`, `q`, `vi`, ver `qa/`-no versionado). Aislado: con
`-ffp-contract=on` y el resto igual, 0 no reproducidas, asi que `long double`->`double` y `-frounding-math` no cambian nada en arm64 y
toda la diferencia es la FMA. Las grabaciones del corpus hay que regrabarlas (M0-c otra vez) antes de P1. **HECHO (2026-11-12)**: regrabadas con el build de D1, 0 diferencias y 0 no reproducibles en las 6; 72 microprogramas, sin cambios (BASELINE, M0-c). `qa/ref_det` regenerada
con `-b build/blade/blade_recomp` (no existe `build/qa` en este clon): dos regresiones seguidas, hitos 650/900/1100/3000/3900
IGUAL y 78 de 78 hashes RAM/imagen IGUALES, 0 errores. Sin comparar contra PCSX2 ni contra imagenes anteriores (no se conservan).
Que cambia: `-ffp-contract=off` y `-frounding-math` para `src/lib/vu/*` y `double` explicito en lugar de `long double` en la ruta
exacta (un parche nuevo). Despues se regeneran `qa/ref_det` y las grabaciones del corpus (M0-c otra vez). Criterio: `execUpper`
sin `fmadd`/`fmsub` en el desensamblado; el banco con `B=interp` en 0; las imagenes de los ticks 100, 900, 3100 y 3160 en
`qa_out/` comparadas con las actuales (y con PCSX2 si cambian). Ganancia: 0 V/s (puede costar 1-3 %: sin FMA hay una
instruccion mas). Riesgo: medio, porque cambia los hashes de todo y hay que dejarlo documentado como cambio de referencia. Si
se descarta la (b), el generador tiene que emitir `vfmaq` justo donde clang contrae hoy, que es fragil.

### P1. Analizador: CFG y horario estatico, sin generar codigo (Opus)
Que cambia: el generador (hoy dentro de `BLADE_VU1_GEN`, en el parche de `vu1-z-compiled`/`diffbench`) gana un paso de analisis.
Por microprograma (entrada + hash): CFG desde la entrada, firmas de pendientes por borde hasta el punto fijo, ciclos por
bloque y por firma, y que FMAC ve cada lectura de banderas, Q y P. Informe en texto en `work/vu1gen/`.
Criterio: para **cada llamada del corpus**, recorrer los bloques que ejecuto A y sumar los ciclos estaticos da **exactamente**
los ciclos grabados (30 000/30 000 por fichero), y los valores de Q/P/MAC que el analisis dice que ve cada lectura coinciden con
los que lee A (se instrumenta A en el banco). Ganancia: 0 V/s. Riesgo: medio, porque el modelo de tuberias del interprete tiene
casos finos (escritura sombra `upperVfShadowReg`, `suppressedLowerVf`, copia de VI para saltos, bits D/T/E en el hueco del salto).
Por eso se valida antes de emitir codigo.

### P2. Generador v1 para r1810 (Opus)
Que cambia: se emite una funcion por microprograma (2.2-2.5) con VF/VI locales, horario estatico, banderas **todas** calculadas
(sin eliminar nada todavia, con `fastFmacFlags` vectorizado y la rama fria a la ruta exacta) y materializacion en las salidas.
Hay seleccion por llamada (2.2) con convivencia: si la entrada no esta en el generador nuevo, se usa el `VU1_GEN` de pares o el
interprete. Se anade `BLADE_VU1_DIFF_EXIT=N` (salida forzada al interprete cada N bloques) para probar la materializacion.
Ficheros: parche nuevo `ps2recomp-zzzz-vu1-programs.patch` (generador, plantilla de funcion y seleccion) y `runner/blade_devtools.cpp`.
Criterio: `BLADE_VU1_DIFF=<cada fichero del corpus>,pc=1810` con 0 diferencias, y tambien con `EXIT=1,2,7`. El tiempo del replay
de las llamadas de r1810 baja **x3 o mas** frente al interprete. En el juego (r1810 es el 44 % de los ciclos): de 14,6 a **17-21 V/s**.
Riesgo: alto por novedad. Es el paso que fija la plantilla de los demas.

### P3. Mutacion de valores raros en el banco (Sonnet)
Que cambia: `BLADE_VU1_DIFF_FUZZ=N[,seed]`. Antes de ejecutar A y B, toma el estado de entrada grabado y, con una semilla, mete
valores en VF/ACC/Q/I y en el 5 % de los qwords de la memoria de datos: exponente 0, exponente 255, ±0, 0x7FFFFFFF, FLT_MAX,
FLT_MAX^2 y numeros normales aleatorios. Fichero: el parche del banco. Criterio: con `B=interp`, 0 diferencias (se prueba el
propio banco); con `BLADE_VU1_DIFF_FAULT`, sigue detectando. Ya con P2, r1810 con mutacion en 0 diferencias sobre 30 000
llamadas y 3 semillas. Riesgo: bajo. Si la mutacion lleva un programa a un bucle sin fin, el presupuesto de 65 536 lo corta en
los dos lados por igual. **Brief Sonnet**: el de M0 con "tarea unica: P3".

### P4. Los 7 siguientes: r1e98, r1e58, 0000 A, r2da8, 0000 B, r1e98 variante y r0388 (Sonnet, uno por tarea)
Que cambia: nada en el diseno, solo se genera con la plantilla de P2 para otra entrada. Si aparece una instruccion que la plantilla
no cubre, se anade con el cuerpo del interprete como funcion en linea. Criterio por programa: `BLADE_VU1_DIFF=<corpus>,pc=<X>` en 0
(y con `EXIT=` y `FUZZ=`), y el replay de ese programa x3 o mas. Acumulado (96 % de los ciclos): **27-35 V/s**, o el techo del
GS si es menor. Riesgo: medio-bajo. **Regla de escalado**: la primera diferencia que no se arregle copiando el cuerpo del
interprete (sobre todo de ciclos, banderas o XGKICK) pasa a Opus con el volcado del banco.
**Brief Sonnet reutilizable (P4):** "Recomp de AirBlade (no el decomp). Lee el ADR, `docs/PERFORMANCE_PLAN.md` §2 y §3 P2/P4 y el
informe de P2. Tarea unica: generar y verificar el microprograma de entrada `<PC>` (imagen `<hash>`) con el generador de programas.
1) `BLADE_VU1_GEN...` sobre el corpus; 2) reconfigurar y compilar sin limpiar; 3) `BLADE_VU1_DIFF=<cada .rec>,pc=<PC>` y con
`BLADE_VU1_DIFF_EXIT=1,2,7` y `BLADE_VU1_DIFF_FUZZ=30000,1..3`: todo en 0; 4) tiempo del replay con `BLADE_VU1_REPLAY_REPEAT=5`
frente a `PS2X_VU1_NOGEN=1`. Si hay una diferencia de ciclos, banderas o XGKICK, para: no la tapes en el generador, informa con
las 5 primeras diferencias. Entrega: tabla (programa, llamadas, diferencias, tiempo antes y despues); commit solo del parche, sin
coautor ni IA, sin push."

### P5. Banderas, normalizacion y XGKICK perezoso (Opus)
Que cambia: liveness de MAC/estado/clip (2.6), marca de normalizado por registro (2.7), pegajosas con OR vectorial y XGKICK
puesto al dia por evento (2.4). Se activa por programa y se puede apagar (`PS2X_VU1_PROG_NOOPT=1`) para comparar.
Criterio: corpus entero en 0 diferencias (con `EXIT`/`FUZZ`); los 8 programas, **x5-8** frente al interprete en el replay. En el
juego: **38-50 V/s** si el GS lo permite. Riesgo: medio. La liveness de banderas en las salidas es donde mas facil es fallar, y el
banco lo ve porque compara el estado final.

### P6. Integracion y cola (Sonnet)
Que cambia: (a) el generador compila **todos** los programas del corpus (los 32, Amdahl de 1.4) y se retira la ruta de pares
`VU1_GEN` o queda solo para depurar; (b) `BLADE_VU1_MISS` registra los programas que van al interprete con sus ciclos; (c) la
eleccion es por generacion de microcodigo y sin guarda por par. Criterio: corpus en 0 diferencias; `qa_regression.sh` con el
compilado **identico** a la referencia del interprete (D1) en los 78 puntos; ciclos interpretados en el nivel 1 por debajo del
0,5 % (`BLADE_VU1_MISS`); V/s medidos (2 corridas). Ganancia: +5-15 % sobre P5 (la cola). Riesgo: bajo.

### G1. GS por CPU para 50 (Opus para el diagnostico, Sonnet para los micro-pasos) — antes de P6 si M0-b da menos de 50
**[2026-11-12, obsoleto en parte]**: M0-b descarto "mas bandas" (8 bandas = 4 bandas) y mostro que la parte serie (~28 ms/V) no
baja con NEON en las funciones de pixel. Queda solo la comprobacion de solape exacta como **G1-a** (§0.9); el NEON del rasterizador
de CPU se aparca, porque la via para 60/120 es G2 (GPU) y la CPU queda como referencia.
Que cambia: segun M0-b. Opciones, de barata a cara: mas bandas o distinta QoS de los hilos (4P+4E); bajar la parte en serie
(las 84 000 primitivas cuya textura lee el destino y las 79 000 con FRAME/ZBUF solapados que dev 2 media en serie);
`sampleT`/`DrawTriangle`/`WritePixelFast` con NEON. Criterio por micro-paso: `PS2X_GS_AB=1` con 0 diferencias en una corrida
completa, `qa_regression.sh` identico y el ms por fotograma del rasterizador medido como en M0-b. Objetivo: ≤16 ms por fotograma
(60 V/s). Riesgo: medio. El A/B del GS ya existe y ha funcionado (274 000 tramos con 0 diferencias en `z-gs-fastpath`).
**Brief Sonnet (micro-paso del GS)**: el de M0 con "tarea unica: <funcion> del rasterizador con NEON; verificacion `PS2X_GS_AB=1`
en 0 y `qa_regression.sh` identico; mide los ms del rasterizador por fotograma antes y despues como en M0-b".

### T1. VU1 en su hilo (Plan B del README) (Opus) — despues de P5, para 60
Que cambia: un productor-consumidor "VIF1+VU1" (diseno del README: copia de los datos fuente en el kick, tickets de GIF por kick
para conservar el orden PATH1/2 frente a PATH3, `vu0_vpu_stat` publicado de forma atomica). En modo determinista va con barrera
por kick o por VBlank, para que los hashes no cambien. Criterio: `qa_regression.sh` identico en modo determinista; en modo normal,
fotograma ≈ max(EE, VU1) + <1 ms. Ganancia: con VU1 a ~13 ms y EE a ~7 ms, de ~20 a ~14 ms por fotograma (capacidad de ~70 V/s si
el GS acompana). Riesgo: alto (carreras, orden del GIF, 4 nucleos P en el M1).

---

## 4. De 50 a 60 y 120

### 4.1 Como cuenta el tiempo el juego (comprobado en `work/output_orig`)
- El manejador de VBlank `0x133CB0` (registrado con `AddIntcHandler(2, ...)` desde `0x133A60`) solo hace `[gp-0x7CE0] += 1`.
- El bucle principal `0x132D10` lee ese contador en `0x132E30` y lo pone a 0. Si vale ≤0, hace 1 paso y deja el contador en -1
  (deuda para el fotograma siguiente). Si vale 1-4, hace ese numero de pasos. Si vale ≥5, lo limita a 4.
- Cada paso (`0x132E70`-`0x1330B8`) calcula `dt = 0,02 x [0x70003FAC]` (0x3CA3D70A = 1/50; el factor del scratchpad parece una
  escala de velocidad **[H]**), lo guarda en `0x2FA558` y llama a las actualizaciones (`jalr`). Despues, una sola vez, dibuja
  (`0x12EB30`, `0x16B040`, `0x12EC80`). Si `[0x2FA42C]` no es 0, se salta los pasos que quedan.
- Es decir: **logica a paso fijo de 1/50 s con salto de fotogramas (hasta 4 pasos por dibujo)**. Encaja con lo medido (el
  cronometro del HUD cuenta 50 VSync por segundo).
- En el port, el VBlank del EE se dispara cuando el reloj del EE llega a su ciclo **y** ademas la hora real lo permite
  (`EeScheduler::processDueDeadlines`). El trabajo del VU1 no avanza el reloj del EE, asi que con el VU1 lento llegan menos VBlank
  por segundo real, el juego ve 1 VBlank por fotograma y va a camara lenta (11 V/s = 0,23x). No salta fotogramas.

**Consecuencia lateral, una opcion barata [H]**: si en modo normal el VBlank fuera por hora real (aunque el EE no haya llegado a
su ciclo), el juego haria hasta 4 pasos por dibujo. Con ≥12,5 fotogramas por segundo dibujados, la logica iria a velocidad real
(con 11,4 fotogramas, al 91 %). Es lo que pasa en una PS2 cargada. Riesgo: codigo del EE que cuente ciclos frente a VBlank. No
afecta al modo determinista. Lo decide Opus y lo prueba Sonnet (cronometro del HUD frente a la hora real y frente al video de
PCSX2). Juega a velocidad real, pero no da mas fluidez.

### 4.2 Correr a 60
- **VBlank a 60 sin mas**: el juego iria 1,2 veces deprisa (ya paso con el VBlank NTSC; esta en BASELINE §4).
- **VBlank a 60 y `dt` a 1/60** (cambiar la constante en `0x132E74`/`0x132E78`): solo sirve si **toda** la logica usa `dt` y nada
  cuenta pasos (animaciones por paso, contadores, el cronometro si cuenta VSync o pasos, el audio enganchado a pasos, la fisica
  con constantes por paso) **[H, probablemente no]**. Prueba barata (Sonnet, sin cambiar codigo del juego en el repo: un
  override de prueba en `runner/` detras de una variable): cronometro del HUD (10 s reales en 600 VSync), recorrido del jugador
  con el mismo guion frente a PCSX2 y duracion de la musica. Si algo va a 1,2x, se descarta.
  **[2026-11-12]**: comprobado que no toda la logica usa `dt` (9 funciones mas con el literal 0,02, 13 con 50,0, reloj sin escalar
  con literal; §0.7). La prueba queda concretada como L0 (§0.7), con parches de instruccion en vez de un override.
- **Interpolacion del dibujo (la buena para 60/120) [H, investigacion de Opus]**: la logica sigue a 50 Hz y se dibuja a 60/120 con
  el estado interpolado entre el paso anterior y el actual. En RenderWare, las transformaciones de camara y objetos estan en las
  matrices de los `RwFrame` (y sus LTM), en RAM. Haria falta: (1) localizar la lista de `RwFrame` y camaras vivas; (2) guardar
  sus matrices tras cada paso; (3) para un dibujo intermedio, escribir matrices interpoladas, llamar solo a la parte de dibujo
  (`0x12EB30`/`0x16B040`/`0x12EC80`) y restaurar despues; (4) comprobar que el dibujo no tiene efectos de logica (particulas,
  animacion que avance dentro del render, el contador de la cola del driver sky2 `gp-0x7EE8`). Es trabajo de ingenieria inversa
  de semanas, con riesgo alto de artefactos (saltos de camara, esqueletos), y solo se puede verificar a ojo y contra PCSX2 a 50.
- **Coste por fotograma dibujado**: a 60 se dibuja 60 veces por segundo, asi que EE de dibujo + VU1 + GS caben en 16,7 ms. Con T1:
  VU1 ≤ ~12 ms (x7) y GS ≤ 16 ms (G1). Sin T1: VU1 x8,6.

### 4.3 120
- 8,3 ms por fotograma: VU1 ≤ ~6-7 ms en su hilo (**x12-15** sobre 83 ms; a medio camino de lo que puede dar un AOT en C++
  **[H]**), parte de dibujo del EE ≤ 8 ms (hoy el EE entero son 5-7 ms, justo), y GS x3-4.
- **GS en GPU (Metal)**: compensa cuando el GS por CPU no baja de ~12-16 ms por fotograma con G1, es decir para 120 y quiza para
  60. Es un backend nuevo que convive con el de CPU, que sigue siendo la referencia: `qa_regression.sh` compara la RAM bit a bit y
  las imagenes del backend de CPU, y la GPU necesita su propia validacion por imagen con tolerancias (PSNR), sin hashes. Costo:
  semanas. **No** va antes de que el VU1 deje de ser el cuello: hoy cambiaria 0 V/s.
- **[2026-11-12, sustituido]**: con el techo del GS por CPU en ~30 V/s y la meta en 60/120, el GS en GPU pasa a hito temprano
  (G2, §0.5) en una via paralela a la del VU1: hoy no cambia los V/s, pero es el paso mas largo hacia 60 y no comparte ficheros con
  el VU1. La validacion por imagen con tolerancias se concreta en §0.5 (banco de capturas, bit a bit donde se pueda).

---

## 5. Riesgos, medidas previas y como no romper nada
| Riesgo | Medida antes | Mitigacion |
|---|---|---|
| Optimizar el VU1 y que el GS sea el techo | M0-b | G1 antes de P6 si el techo es <50 |
| Modelo de ciclos o tuberias mal entendido | P1 (ciclos predichos = grabados en todo el corpus) | sin P1 en verde no hay P2 |
| Valores raros (exp 255, NaN, denormales) | P3 (mutacion) | reproducir el interprete; nunca comparar en el host un posible NaN |
| Oraculo dependiente de la plataforma (FMA, `long double`) | desensamblado de `execUpper` | D1 |
| Programas o variantes fuera del corpus | `BLADE_VU1_MISS` | guarda por llamada, interprete de respaldo, regrabar |
| Salidas a mitad mal materializadas | `BLADE_VU1_DIFF_EXIT` | bloques no vistos → interprete |
| Clang reordena respecto a `fesetround` | revisar flags | `-frounding-math` en las unidades del VU |
| Hilos en nucleos de eficiencia (M1 4P+4E) | `sample` por hilo y QoS | ajustar el numero de bandas y la QoS antes de anadir T1 |
| Medidas con la maquina cargada | load average en cada fila | repetir; no comparar corridas con carga distinta |
| Romper el modo determinista | `qa_regression.sh` tras cada paso que toque el runtime | el compilado tiene que dar los mismos hashes que el interprete; T1 con barrera |
| (2026-11-12) GS en GPU distinto de la CPU | banco de capturas `PS2X_GS_BENCH` en los dos backends | bit a bit en VRAM donde sea entero; tolerancia declarada y hash tolerante si no; CPU como respaldo |
| (2026-11-12) Barreras por "textura solapa destino" en GPU | G0 | prueba de solape exacta (G1-a); agrupar las que leen la misma fuente |
| (2026-11-12) Logica que no usa `dt` (literales 0,02/50,0, contadores por paso) | L0 | interpolacion (I1) si L0 falla |
| (2026-11-12) Dibujo con efectos de logica (interpolacion) | I0 | lista de efectos antes de I1; `alfa = 1` da los mismos hashes |
| (2026-11-12) Dos vias en paralelo pisandose | ADR: un agente por build | worktree y `build/gpu` propios para la via del GS |

Cadena de regresion por paso (de barata a cara): (1) `BLADE_VU1_DIFF` del programa tocado, ~6 s por fichero; (2) corpus
completo con `EXIT` y `FUZZ`, ~2 min; (3) `scripts/check_patches.sh`; (4) `scripts/qa_regression.sh` identico frente a
`qa/ref_det`, ~6 min; (5) V/s en modo normal, 2 corridas. Un paso de rendimiento con cualquier diferencia en (1)-(4) no entra.

---

## 6. Estimaciones (rangos; V/s del nivel 1, tramo 2600-3900)
**[2026-11-12]** Sustituidas por §0.4 (hitos) y §0.8 (sesiones e incognitas). Lo que M0 cambia aqui: G = 33-44 ms/V aislado (no
23-29), asi que la columna "con el techo del GS sin G1" baja a 23-30 V/s desde P4; "120 no es alcanzable con este plan" pasa a ser
el hito H5 con P7, E1, I1 y G2.
Modelo: fotograma = max(R + 83/s_VU1, G), con R = 5-7 ms (resto del EE) y G = 23-29 ms (rasterizador, **sin medir bien: M0-b**).
| Paso | Ganancia en el VU1 (acumulada, sobre el interprete) | V/s esperados si el GS no limita | Con el techo del GS sin G1 | Esfuerzo |
|---|---|---|---|---|
| Hoy (`VU1_GEN`) | x1,4 | 14,6 (medido) | 14,6 | — |
| M0, D1 | — | 14-15 (D1 puede quitar 1-3 %) | — | 1-2 sesiones (Sonnet) + 1 (Opus) |
| P1 | — | — | — | 2-4 sesiones (Opus) |
| P2 (r1810) | x1,8-2,3 | 17-21 | 17-21 | 3-6 sesiones (Opus) |
| P3 | — | — | — | 1 sesion (Sonnet) |
| P4 (8 programas, 96 %) | x3,2-4,0 | 27-35 | 27-35 | 4-8 sesiones (Sonnet), +1-3 de escalado a Opus |
| P5 (banderas y normalizacion) | x4,5-6,5 | 38-50 | **34-43** | 3-5 sesiones (Opus) |
| P6 (cola, 99,5 %) | x5,5-8,5 | 44-55 | 34-43 | 1-2 sesiones (Sonnet) |
| G1 (GS x1,3-1,8) | — | — | 45-55 (lo marca el VU1) | 3-8 sesiones (Opus + Sonnet) |
| T1 (VU1 en su hilo) | — | capacidad de 55-75 | lo que de G1 | 4-8 sesiones (Opus) |

**Acumulado para 50 estables**: M0 + D1 + P1-P6 + G1, unas 20-40 sesiones de trabajo. El resultado mas probable es 45-55 V/s
de media en el nivel 1, con el 1 % peor por debajo de 50 en escenas pesadas (hoy el peor 1 % va ~1,9x por encima de la media:
163,6 frente a 87,8 ms). "50 estables" de verdad probablemente pide tambien T1 para tener margen.
**Incertidumbre principal**: el coste real por par que consiga clang con NEON (de 15 a 40 instrucciones de la CPU por par; eso es
la diferencia entre x4 y x9) y el techo real del GS. Las dos cosas se miden pronto: P2 da la primera y M0-b la segunda.
**60** pide ademas T1 y G1 hasta ≤16 ms, mas una de las vias de 4.2 (interpolacion o `dt`), que es investigacion sin plazo.
**120** no es alcanzable con este plan solo: pide un GS en GPU y un VU1 x12-15.

---

## Anexo A. Comprobaciones hechas para este plan (solo lectura, sin ejecutar el juego)
- **Aritmetica de BASELINE**: 111,6 s / 1 347 VSync = 82,9 ms; 2,41 G / 1 347 = 1,79 M ciclos por VSync; 111,6 s / 2,41 G =
  46,3 ns por ciclo; 1,79 M x 50 = 89,5 M ciclos/s. Cuadra.
- **`work/vu1gen/nivel.rec`, leido con un analizador propio del formato `VU1REC01`** (registros `C` de imagen de 16 KB y `K` de
  llamada, con estado `VU1State` de 664 bytes): 30 000 llamadas (27 114 `resume`, 2 886 `execute`), 63 370 425 ciclos (2 112 por
  llamada), **0 llamadas no limpias** (todas acaban sin nada pendiente), 501 subidas de microcodigo, 51,8 trozos de 64 B subidos
  por llamada (~3,3 KB) y 1,45 paquetes GIF por llamada. Ciclos por PC de entrada: 0x1810 44,4 %, 0x1e98 28,7 %, 0x0000 12,1 %,
  0x1e58 8,7 %, 0x2da8 3,6 %, 0x0388 1,1 %. **Entradas repetidas** (mismo tipo, PC, estado sin ciclos y bytes subidos): 162
  (0,5 %); salidas repetidas (hash de memoria y de GIF): 210.
- **`work/vu1gen/vu1gen_unit_*.inc`** (tramo 0x0-0x1f00): variante 0 con 992 pares (625 FMAC con banderas, 268 NOP arriba, 73 sin
  banderas, 26 `CLIP`), 9 `FMAND`, 0 lectores de estado, 27 `FCAND`, 9 `FCOR` y 4 `FCGET`; variante 1 con 764 pares, 485 FMAC,
  9 `FMAND`, 0 de estado y 28 de clip. Es un recuento estatico (no ponderado por ejecucion; eso lo da M0-d).
- **`build/blade/blade_recomp`** (Release, `-O3`, `-march=armv8-a+fp+simd+crypto+crc`, sin `-ffp-contract=off`): en el
  desensamblado de `VU1Interpreter::execUpper` hay 35 `fmadd`/`fmsub` (MADD/MSUB fusionados).
- **`ps2_vu1_core.cpp`/`ps2_vu1_step.inc`/`ps2_vu1_fmac.inc`/`ps2_vu1_upper.cpp`**: `run()` pone `FE_TOWARDZERO`; los operandos se
  normalizan (exponente 0 a ±0 y 255 a ±FLT_MAX) en cada FMAC; MAC/estado llegan con 4 ciclos de retraso por una cola de 8
  entradas; Q tarda 7 (DIV/SQRT) o 13 (RSQRT); el XGKICK copia 16 bytes cada 2 ciclos; el presupuesto es de 65 536 ciclos por
  llamada (`ps2_runtime.cpp`).
- **`EeScheduler.cpp`**: un plazo vence con `deadlineCycle <= m_eeCycle` y, fuera del modo determinista, ademas con la hora real.
- **`work/output_orig`**: manejadores de VBlank `0x133CB0` (contador `gp-0x7CE0`), `0x16B010` (llama a `0x10AC18`, sin identificar) y `0x25D540` (lee `GS CSR` 0x12001000; driver de imagen). Bucle
  principal `0x132D10` con 1-4 pasos de `dt = 0,02 x [0x70003FAC]` y un solo dibujo (§4.1).
- **CPU**: Apple M1, 4 nucleos de rendimiento y 4 de eficiencia.
- **(2026-11-12, revision) `work/output_orig`, recuento de texto sobre el desensamblado comentado**: lectores de `[0x70003FA8]` (dt)
  53 funciones, de `0x70003FB0` 71, de `0x70003FB4` 8; `lui 0x3CA3` + `ori 0xD70A` (0,02) en 10 funciones (el bucle y 9 mas);
  `lui 0x4248` (50,0) en 13; escritores de `[0x70003FAC]` 4 funciones. Reloj sin escalar `[0x70003FB4] += 0,02` en el propio paso.
  No es un analisis de flujo: un mismo literal puede no ser tiempo, y los contadores enteros por paso no estan contados.
- **(2026-11-12, revision) Interfaz del GS**: `GSRasterBackend` (`include/runtime/gs/gs_backend.h`) con `GSCpuBackend` y
  `GSThreadedBackend` como implementaciones; `GSPrimitiveBatch` = 3 vertices + `GSDrawState` por primitiva; `GSContext` lleva FRAME,
  SCISSOR, TEX0, XYOFFSET, ZBUF, TEX1, MIPTBP1/2, CLAMP, ALPHA, TEST y FBA. No hay ningun backend de GPU en el runtime.
- **Sin comprobar** (y por eso con [H] arriba): que el tiempo del hilo del rasterizador sea trabajo y no espera, que las
  reanudaciones dependan entre si en todos los casos, cuantos VF vivos tiene cada programa, si la logica usa `dt` en todas partes,
  y que significa `[0x70003FAC]`.
- **Discrepancia sin explicar**: dev 2 media 22-23 V/s con el VU1 compilado y la linea base limpia da 14,6. Coincide con que despues
  de AB-004/AB-017 se dibujan el jugador, la tabla y la sombra (mas trabajo del VU1 y del GS) **[H]**. Para comparar con cifras
  viejas, hay que medir los ciclos del VU1 por VSync de las dos builds en el mismo tramo.
