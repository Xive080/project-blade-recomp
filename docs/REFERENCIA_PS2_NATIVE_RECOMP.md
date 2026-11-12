# REFERENCIA: ps2-native-recomp frente a nuestro plan de rendimiento (AB-001)

Fecha: 2026-11-12. Estudio de **solo lectura** de https://github.com/exoteric33/ps2-native-recomp, commit `0cc2658` (2026-11-11),
clonado con `--depth 1` en `~/airblade-work/refs/ps2-native-recomp` (fuera de este repo). No se ha compilado ni ejecutado nada
suyo. Las rutas `ref:` son de ese clon; las nuestras son de este repo. Lo marcado **[H]** es hipotesis; lo demas esta leido en el codigo.
Contraste con `docs/PERFORMANCE_PLAN.md` (§0, G0/G1-a/D2/G2, P1-P7, T1) y `docs/PERFORMANCE_BASELINE.md` (M0).

## Resumen (10 lineas)
1. Su GS en GPU es **exactamente el modelo que §0.5 recomienda**: VRAM de 4 MB como buffer con el swizzle nativo, raster en compute por teselas 8x8 (un hilo por pixel que recorre en orden la lista de su tesela). Confirma el diseno; no es codigo que podamos enchufar.
2. Su raster **no es entero ni bit a bit**: cobertura e interpolacion en `f32` en el shader (`gs.wgsl:1289`), sin dithering (DTHE/DIMX no se usan en ningun rasterizador). Nuestro G2 tiene que igualar a `gs_cpu_backend.cpp`, asi que el shader lo escribimos nosotros.
3. Lo mas valioso para nosotros es **como evitan la serie por "textura solapa destino"**: lote por FRAME/ZBUF, corte solo si los texeles leidos (rectangulo UV real) caen bajo lo ya dibujado en el lote, y **copia previa (snapshot) del rango compartido** para el resto. Es la regla que G1-a y G2 necesitan, y sirve tambien en la CPU.
4. Coste por fotograma: no publican cifras del GS en GPU salvo "18 ms el fotograma entero del pintor" en una escena de God of War en un portatil Intel (`ref: hwdraw.cpp:885`) y que PCSX2 es mas rapido en todas las escenas. Una dispatch de raster por lote; si las ~8 000 primitivas serie de AirBlade fueran realimentacion real, serian ~8 000 dispatches: G0 decide.
5. VU1: compilan **programa entero por PC de entrada + hash FNV del codigo estaticamente alcanzable** (`ref: vu_ops.hpp:107,546`), con horario de esperas calculado al generar y salida al interprete en lo no soportado. Coincide con nuestro P1/P2/P6; la clave por alcanzable estatico es mejor que la nuestra (por pares ejecutados).
6. Su aritmetica del VU1 **no es la de la PS2**: coma flotante del host con redondeo al mas cercano y recorte de exponente 255/denormales; la aritmetica exacta (`fp::`) solo la usa el VU0 (`ref: vurecomp_gen.cpp:837`). Sin bandera U, latencia uniforme de 4. No sirve para nuestra regresion por hashes ni para el oraculo de D1.
7. Instrucciones por par [H, estimado del generador]: ~15-25 en x86 con su truco DAZ; en arm64 el truco esta apagado (`ref: vu_ops.hpp:1105`) y quedan ~22-30. Eso es x5-8 sobre nuestro interprete, no el x12 de P7.
8. Hilos: emulacion / VU (VIF1+VU1+GIF) / ventana (GS), con colas; el VU1 **no consume tiempo del EE** y el EE solo espera al hilo del VU cuando toca memoria o registros del VU1 (`ref: memory.cpp:104`, `dmac.cpp:64`, `cop.cpp:121`). Es el modelo que nuestro T1 puede usar sin barrera por kick, porque nuestro VU1 tampoco alimenta el reloj del EE (plan §2.4).
9. Dependencia de wgpu-native: ~11 000 de ~80 000 lineas (todo el backend de GPU); el VU, el EE y el IOP no la usan. Para una app solo-Mac, **Metal propio con su arquitectura** sale mejor que meter wgpu; paraLLEl-GS queda como comprobacion corta, no como via principal.
10. Cambios propuestos: G0 clasifica con sus categorias de corte; G1-a adopta sus reglas (rectangulo UV, "dibujado debajo", lectura del propio pixel, byte alto, snapshot); D2 baja a 0,5-1 sesion; G2 v1 -3 a -5 sesiones **[H]**; P6 con clave por alcanzable estatico; T1 con el modelo de drenaje por acceso. Ahorro total estimado **5-11 sesiones [H]**.

---

## 1. GS

### 1.1 Que estado reproducen (gs.wgsl, raster de compute) y que no
| Estado | Ellos | Cita | Nosotros (referencia: `gs_cpu_backend.cpp`) |
|---|---|---|---|
| Primitivas | triangulo, sprite, linea (busca el paso entero mas cercano), punto | `ref: gs.wgsl:1273-1462` | igual (punto, linea, tira, triangulo, tira, abanico, sprite) |
| Cobertura | **funciones de arista en `f32`** (`cross2(...) * inv`), regla de inclusion por arista en `rec[27]`, casos especiales para rectangulos a escala >1 | `ref: gs.wgsl:1289-1330` | entera (`DrawTriangle`) **[H: comprobar en G2 que nuestra cobertura es entera]** |
| Interpolacion | baricentrica en `f32`; `EXPLICIT_FMA` para fijar el orden de fusion entre compiladores de shaders | `ref: gs.wgsl:17-35` | CPU; G2 debe igualarla |
| PSM color/Z | CT32/24/16/16S, T8, T4, T8H, T4HL, T4HH, Z32/24/16/16S | `ref: gs.wgsl:123-190` | mismos |
| Swizzle | tablas por formato en un buffer (`tables`) + base de pagina; los formatos de una configuracion se precalculan en la CPU (`fmt_words`) | `ref: gs.wgsl:118-214` | `ps2_gs_memory.cpp` |
| CLUT | **2 048 ranuras versionadas**: cada carga copia la ranura anterior y escribe la nueva (CSM1 y CSM2, CSA, 16/256 entradas); el lote apunta a su ranura y no se corta por cargar paleta | `ref: gpu.cpp:50`, `gs.wgsl:380-416` | una CLUT viva (`LoadClut`) |
| TFX, TCC | modulate/decal/highlight/highlight2 | `ref: gs.wgsl:1078-1114` | igual |
| TEXA/AEM, CLAMP (4 modos), bilineal, MIP (TEX1, MIPTBP) | si (MIP en una variante del shader, `HAS_MIP`) | `ref: gs.wgsl:21,583-604,841-893` | `sampleT` |
| FOG/FOGCOL | si | `ref: gs.wgsl:1116-1124` | si |
| TEST: ATE (8 pruebas, 4 AFAIL), DATE/DATM, ZTE/ZTST | si | `ref: gs.wgsl:995-1170` | si |
| ALPHA A/B/C/D/FIX, PABE, COLCLAMP | si | `ref: gs.wgsl:1172-1226` | si |
| FBA | si | `ref: gs.wgsl:1229` | si |
| FBMSK, ZMSK, Z24 que no toca el byte alto | si (`write_frame`, mascara 24 bits) | `ref: gs.wgsl:932-962` | si |
| SCISSOR, XYOFFSET, SCANMSK | si | `ref: gs.wgsl:976-986` | si |
| **Dithering (DTHE/DIMX)** | **no**: solo se guardan los registros (`ref: gs.cpp:871-872`); ni el shader ni `raster.cpp` los usan | grep sin resultados | si (plan §0.5) |
| Transferencias host→local | records en la arena + dispatch `upload` (decodifica por pixel en el shader) | `ref: gs.wgsl:288-330`, `gpu.cpp:1441` | `UploadImage` |
| Local→local | dos dispatches: `copy_read` a `scratch` y `copy_write` (respeta la direccion) | `ref: gs.wgsl:332-377`, `gpu.cpp:3389` | CPU |
| Local→host | copia a buffer mapeable y espera (sincrona o asincrona), con registro `PS2_READBACK_LOG` | `ref: gpu.cpp:1899-1922`, README | `ConsumeLocalToHostBytes` |
| Presentacion | compute `display` (circuito de salida) + blit | `ref: gs.wgsl:418` | CPU (v1 de G2 igual) |
| Escala interna 2x/3x | SCALE x SCALE muestras por palabra en la VRAM | `ref: gs.wgsl:14` | no aplica (no lo queremos para la regresion) |

### 1.2 Reparto en teselas, orden y tamanos
- La **CPU** construye las listas por lote (`tiles::Builder::build`): por cada primitiva, tramos por fila de teselas (recorte por aristas en `double`), recuento por tesela y listas de indices de **16 bits** (un lote tiene como mucho 65 536 formas) (`ref: tiles.cpp:47-50,88-225`). Puede ir en un hilo ayudante, **apagado por defecto** porque "cuesta cientos de MB y no se ha visto que sea mas rapido" (`ref: tiles.cpp:466-474`).
- Tesela de **8x8** por defecto (probaron 4, 8 y 16; 8 fue la mas barata, `ref: gpu.cpp:581`); un workgroup por tesela tocada, **un hilo por pixel** (`@workgroup_size(TILE, TILE)`, `ref: gs.wgsl:1502-1546`). Cada hilo recorre la lista de su tesela **en orden**, descarta por caja y pinta; el orden lo garantiza que cada pixel es de un solo hilo. La VRAM es `array<atomic<u32>>` para los formatos de menos de 32 bits (16/8/4 bits comparten palabra con pixeles de otros hilos: `atomicXor`, `ref: gs.wgsl:5,234-248`).
- Registro por forma: 28 palabras (`kPrimWords`) + configuracion de 40 palabras compartida dentro del lote (`ref: gpu.cpp:50-60`).
- **Dispatches por fotograma**: 1 `raster` por lote + 1 `snapshot` si el lote lee lo que escribe + `upload`/`load_clut` agrupados entre lotes + 2 por copia local + `decode` de texturas + `display`, todos en **un solo compute pass por envio** (`ref: gpu.cpp:1739-1820`; WebGPU sincroniza entre dispatches). El lote se cierra al cambiar FRAME/ZBUF (`cache.key` = FBP|FBW|FPSM|ZBP|ZPSM, `ref: gpu.cpp:3111,3214`), por conflicto de textura (1.3), por subida o paleta que toca lo que el lote lee o escribe (`ref: gpu.cpp:1430-1490`), por copia local y a las 65 536 formas (`ref: gpu.cpp:3351`). No publican cuantos lotes salen por fotograma (su `docs/speed.md` no esta en el repo); el comentario de `tiles.hpp:28` habla de "about 50,000 times a frame" formas.

### 1.3 Aliasing de la memoria del GS (textura que solapa destino)
Reglas en `draw()` (`ref: gpu.cpp:3226-3283`), aplicadas cuando una forma texturizada llega a un lote abierto:
1. **Rango de texeles real**: `texel_rect` saca el rectangulo UV que la forma puede leer (con los 4 modos de CLAMP y un texel de margen) en vez de la textura entera (`ref: gpu.cpp:2500-2534`).
2. **Byte alto intacto**: una textura PSMT8H/4HL/4HH no choca con un Z24 ni con un CT24 (el dibujo no escribe los bits 24-31) (`ref: gpu.cpp:3231-3240`).
3. **"Dibujado debajo"**: si la textura comparte buffer y formato con el FRAME, solo se corta si el rectangulo leido toca las cajas de las primitivas ya metidas en el lote (si hay mas de 64, la caja del lote) (`ref: gpu.cpp:2541-2550,3264`). Si el formato es otro, prueba de rango de paginas.
4. **Conjuntos de palabras exactos** para sprites pequenos (≤1 024 pixeles) que leen el FRAME en otra disposicion (MGS3: 7 168 sprites de 16 pixeles por fotograma que expanden indices en el sitio) (`ref: gpu.cpp:338-360`, `exact_clear`/`exact_follow`).
5. **Lectura del propio pixel**: un sprite 1:1 que lee exactamente la palabra que escribe no necesita nada (`reads_own_word`, `ref: gpu.cpp:746-766`).
6. Lo que no se corta pero comparte paginas con el destino lee de una **copia previa**: antes del raster, una dispatch `snapshot` copia a `scratch` el rango comun (union de lecturas ∩ escrituras del lote) y el shader lee de ahi los texeles de ese rango (`ref: gpu.cpp:1220-1255,1376`; `gs.wgsl:618-623,1485-1491`). Asi una forma de una tesela no ve las escrituras de formas posteriores de otra tesela.
7. Lo demas corta el lote: motivos contados por `PS2_BATCH_LOG` (`buffers`, `texel`, `drawn-under`, `overlap`, `depth`, `upload`, `palette`, `copy`, `size`; `ref: gpu.cpp:182-190`). Hay un autotest de las reglas de palabras (`PS2_EXACT_SELFTEST`, `ref: gpu.cpp:2552-2680`).

Comparacion con nuestra CPU: hoy `gs_cpu_backend.cpp` (≈1764-1849, fichero en obras por otro agente) manda a serie toda primitiva cuya **textura entera** (rango de paginas) solapa el rango de FRAME/ZBUF de filas 0..y1 del tramo. Es la prueba conservadora que ellos sustituyen por 1-6. **Las ~8 000 primitivas serie por VSync de M0-b son exactamente el caso que estas reglas resuelven si son de los tipos 2-5** **[H: lo dice G0]**.

### 1.4 Coste
No hay cifras del pintor en GPU por fotograma salvo: "the painter's whole frame is 18 ms" (God of War, barco, Intel integrado; `ref: hwdraw.cpp:885`) y "slower than PCSX2 in every scene I measured, mostly because of the graphics chip's programs" (README, "What it does not do"); el motivo que dan es el direccionamiento swizzle y el programa general por pixel. La ruta experimental de triangulos por hardware (`PS2_GPU_HW=1`) es **mas lenta** (12,6 %, 5 % y 17 % menos V/s en tres escenas) y se queda sin memoria en GoW2 (`ref: hwdraw.cpp:24-33`); ademas la mezcla de doble fuente **no es repetible** en Intel (`ref: hwdraw.cpp:272-276`). Herramientas de medida utiles: `PS2_GPU_BENCH` (repite un fotograma capturado por tipo de trabajo), banco de "tiny dispatch" (`ref: gpu.cpp:2281-2294`).

### 1.5 Sirve para una app Metal nativa? (D2)
| Opcion | A favor | En contra | Veredicto |
|---|---|---|---|
| **Metal propio con su arquitectura** (VRAM-buffer swizzle, teselas, listas en CPU, snapshot, CLUT por ranuras) | M1 con memoria unificada (`storageModeShared`, plan §0.5); sin dependencias; shader propio = igual a nuestra CPU (entera, dithering); WGSL→MSL es mecanico como guia | hay que escribir el shader exacto nosotros (es lo caro en cualquier opcion) | **recomendada** |
| wgpu-native + su WGSL | corre en Metal; diseno probado en 53 juegos | binario de Rust externo; su shader es `f32` y sin dithering (no iguala nuestra referencia); acoplado a su frontend (`gs.cpp` `State`/`Vertex`) y a su arena; ~11 000 lineas de backend que no casan con `GSRasterBackend` | descartada como dependencia; util como consulta |
| paraLLEl-GS por MoltenVK (plan D2 (a)) | pensado para igualar al GS por software, con seguimiento de riesgos | MoltenVK y las extensiones que pide **[H: sin verificar]**; licencia **[H: verificar]**; adaptar a nuestro frontend | comprobacion de 0,5 sesion (lista de extensiones frente a MoltenVK), no prototipo |
**Por que**: el diseno de ellos valida el nuestro y aporta las reglas de aliasing, pero su exactitud no es la que necesitamos y su capa grafica no aporta nada en una app solo-Mac.

## 2. VU1

### 2.1 Como traducen
| Aspecto | ps2-native-recomp | Cita | Nuestro plan (P1-P7) |
|---|---|---|---|
| Unidad | programa entero desde un PC de entrada; etiqueta `goto` por par | `ref: vurecomp_gen.cpp:536-910` | igual (§2.2) |
| Clave | **FNV-1a de (pc, lo, hi) de todo lo alcanzable estaticamente** desde la entrada (`reachable` sigue saltos y BAL, para en E-bit/JR) | `ref: vu_ops.hpp:107-130,546-560` | PC + hash de los pares **ejecutados** (§2.9; M0-c: 72 por contenido frente a 30 imagenes) |
| Cache en ejecucion | por entrada y por generacion del microcodigo, hasta 8 candidatos que se comprueban contra el codigo | `ref: vu_micro.cpp:1255-1283` | guarda por llamada y generacion (§2.2) |
| Registros | VF en la estructura `r.vf` en memoria (cada par guarda su resultado) + copias normalizadas locales (`vshadow`); VI locales | `ref: vurecomp_gen.cpp:827,880-886` | VF/VI/ACC/Q/P locales (§2.5) |
| Esperas | `stall_plan`: estado de tuberia (128 carriles + 16 VI + Q) propagado al generar; si los caminos discrepan, indice de estado `st` (hasta 24) | `ref: vu_ops.hpp:286-330` | firmas de pendientes por bloque (§2.4) |
| Latencias | **4 ciclos uniformes** para FMAC y cargas de VI, Q segun la instruccion; P/EFU sin tuberia propia en el plan | `ref: vu_ops.hpp:276-290` | las del interprete (oraculo): DIV 7, RSQRT 13, P, XGKICK |
| Ciclos | contador `fl.t` (+1+espera) solo para ver banderas a 4 ciclos y Q; **no es ciclo exacto ni alimenta el reloj del EE** | `ref: vurecomp_gen.cpp:869-872`, `cop.cpp:118` | ciclos exactos comparados en el banco (§2.4) |
| MAC/estado | liveness (lectores a ≤3 pares) y, si el programa lee MAC pero nunca el estado, `FlagPipeV`: guarda el resultado y calcula la MAC en el lector; pegajosas como 3 carriles (minima magnitud, OR de signos, maxima magnitud): "5 instrucciones vectoriales y 2 stores frente a ~50" | `ref: vurecomp_gen.cpp:560-590,814`, `vu_ops.hpp:850-900` | §2.6 y P5 (MAC solo de las FMAC que ve cada `FMAND`) |
| Bandera U | **no se activa nunca** (`flags()` solo Z, S y O = 0x7FFFFFFF) | `ref: vu_ops.hpp:711-731` | O/U por la ruta exacta (§2.7) |
| Clip | `clip_value` por instruccion | `ref: vu_ops.hpp:763-773` | igual |
| Exponente 255 / NaN | recorte a ±FLT_MAX en cada lectura (`vfix`/`vclamp`), resultado acotado con min/max; nunca comparan un NaN | `ref: vu_ops.hpp:612-650,1120-1160` | `normalizeOperand` + marca por registro (§2.7) |
| Denormales | a cero con signo; en x86 con DAZ y limpieza perezosa por analisis de flujo (`lazy`, solo si el programa "vale" ≥40 FMAC); **en arm64 apagado: `vfix` completo en cada operando y resultado** | `ref: vurecomp_gen.cpp:640-700`, `vu_ops.hpp:1086-1105` | igual que el interprete |
| Redondeo | **el del host (al mas cercano)**, MADD = suma + producto con dos redondeos (sin FMA); la aritmetica exacta de la PS2 (`fp::add` con bit de guarda y truncado) solo para el VU0 (`exact = p.unit == 0`) | `ref: vurecomp_gen.cpp:837`, `vu_ops.hpp:588-591`, `vu.hpp:205-220` | D1: sin FMA, `FE_TOWARDZERO`, `-frounding-math` |
| Saltos indirectos | `JR`/`JALR` → `dispatch:` → interprete, que vuelve a buscar programa para el destino; los destinos vistos se anaden como entradas | `ref: vurecomp_gen.cpp:910,985-990` | `switch` sobre destinos vistos (§2.2) |
| No soportado | `vu::interpret(1, pc)` desde ese par | `ref: vurecomp_gen.cpp:855` | materializacion y salida (§2.3) |
| Verificacion | en el juego, 1 de cada 64 llamadas: compilado frente a interprete desde el mismo estado, registros y memoria (`PS2_VU_CHECK`) | `ref: vu_micro.cpp:1180-1230` | banco diferencial sobre grabaciones (`BLADE_VU1_DIFF`) |

### 2.2 Instrucciones del host por par (estimacion del generador) [H]
Un par tipico de AirBlade (M0-d: `MADDbc` + `LQ`):
- Superior, x86 con DAZ: `vsplat` 1 + `mul` 1 + `vclamp_result` 2 + `add` 1 + `vclamp_result` 2 + guardar en `r.vf` 1 (3 con `vsel` si `dest` parcial) = **8-10**; MAC viva con `FlagPipeV` +7.
- Inferior `LQ`: direccion con VI local 2-3 + carga y guardado de 16 bytes 2 + `vshadow` (clamp) 2 = **6-7**; `++fl.t` 1.
- Total x86: **~15-25 por par**. En arm64 (`vfix_generic`, 5-6 instrucciones NEON por limpieza, en operandos y resultado) y con VF en memoria: **~22-30** **[H]**.
- Con nuestras cuentas (plan §0.2): x5,5 pide ~31 por par, x8 ~21, x12 ~14. **Su diseno en el M1 daria x5-8, no x12**: confirma que P7 necesita VF en registros y control por bloque.

### 2.3 Interprete → compilado en segundo plano
El programa desconocido corre en el interprete; `live::unknown(key, entry, code)` lo encola (`ref: vu_micro.cpp:1283`); un hilo genera C++ con el mismo generador, llama a un **compilador de C++ real** que la app descarga, y carga la biblioteca (los mas llamados primero, 24 por biblioteca, `ref: vu_live.cpp:70`); se instala en la siguiente llamada (`install_live`) y queda en disco para la siguiente partida. La version del navegador tiene un JIT a WebAssembly (`ref: vu_jit.cpp`). En macOS cargar una dylib recien compilada pide desactivar la validacion de bibliotecas en una app firmada y llevar un compilador: para nosotros encaja mejor la **cache AOT fuera de linea** (plan §0.6 opcion 2), con su misma idea (persistir lo que falla).

### 2.4 Que adoptar y que descartar (VU1)
| Adoptar | Por que | Donde |
|---|---|---|
| Clave por **alcanzable estatico** + entrada (no por pares ejecutados) | junta variantes que solo difieren en la ruta (M0-c: 43 de 72 "solo en una escena" por este efecto); compila caminos no vistos; es la opcion 1 de §0.6 hecha y probada en 53 juegos | P1 (analizador) y P6 |
| `JR` → salida a una tabla de entradas, destinos anadidos como programas | resuelve los saltos indirectos sin CFG global | P2 |
| Liveness de MAC + **pegajosas como 3 carriles acumulados** (min magnitud, OR signo, max magnitud) | es la forma concreta y barata del "control por bloque" de P7 para S/Z/O; U y O del borde siguen por la ruta exacta (rama fria) | P5, P7 |
| Comprobacion muestreada en el juego (1/64 llamadas compilado frente a interprete) | detecta en partidas reales lo que el corpus no cubre; barato, detras de una variable | P6 (`BLADE_VU1_MISS` + `BLADE_VU1_CHECK`) |
| Persistir lo que se interpreta para generarlo despues | ya es la opcion 2 de §0.6 | P6 |
| **Descartar** | **Por que** | |
| Redondeo del host, sin U, latencia uniforme 4 | rompe el oraculo de D1, las referencias `qa/ref_det` y los ciclos del banco | — |
| VF en memoria con guardado por par | es lo que limita su instrucciones por par | P2 mantiene locales |
| Limpieza perezosa con DAZ | solo x86 (SSE4.1/MXCSR); en arm64 `FPCR.FZ` vacia entradas **y** salidas, y ellos documentan que el vaciado de salidas discrepa de `vfix` justo bajo el minimo normal (`ref: vu_ops.hpp:1093-1095`) | P7: la prueba de `FPCR.FZ` pasa a **[H probable no]**; FIZ (FEAT_AFP) no existe en el M1 |
| Compilador de C++ en la maquina del jugador / JIT | firma de codigo en macOS, peso, verificacion | §2.9 sigue igual |

## 3. Hilos y sincronia
- **Tres hilos y dos colas** (anillos de 32 MB): emulacion → hilo del VU (palabras VIF1 crudas, `kVif1`, `ref: vif.cpp:633`; el hilo del VU desempaqueta, ejecuta el VU1 y reenvia el GIF, `ref: system.cpp:929-970`) → hilo de ventana (GS + GPU + ventana). El EE lleva una **copia "en seco" del VIF1** para responder estados y busca SIGNAL/FINISH en el GIF de PATH2/3 en su propio hilo (`ref: vif.cpp:526`, `gif.cpp:238`); FINISH llega 1 000 ciclos del EE despues (`ref: gs.cpp:53`).
- **El VU1 nunca esta ocupado** para el EE (VPU_STAT = 0, `ref: cop.cpp:118`) y **no gasta tiempo del EE**. El EE solo espera al hilo del VU (`drain_vu_thread`) cuando lee o escribe memoria del VU1, hace DMA desde/hacia ella o lee CMSAR1 (`ref: memory.cpp:104`, `dmac.cpp:64`, `cop.cpp:121`). Como el resultado del VU1 solo vuelve al EE por esos caminos, **el EE es determinista sin barrera por kick**.
- **2 imagenes por delante**: `post_present` bloquea si hay mas de 2 Present sin mostrar (`ref: gs.cpp:1377-1382`); el ritmo lo marca el audio (~80 ms en cola, README). La entrada se lee una vez por fotograma.

Implicaciones para nosotros:
1. **T1**: nuestro VU1 tampoco alimenta el reloj del EE (plan §2.4, `ps2_runtime.cpp`). Se puede copiar el modelo: barrera **solo** en los accesos del EE al estado del VU1/VIF1 (y en local→host), no por kick ni por VBlank. Antes hay que inventariar esos accesos en AirBlade y si hay SIGNAL/FINISH en PATH1 (XGKICK), que la copia en seco del EE no ve **[H: G0/M0-e lo pueden contar]**. Si se cumple, T1 baja de riesgo y de 4-8 a **3-6 sesiones [H]**.
2. **60/120**: la profundidad de 2 imagenes absorbe la variacion entre EE, VU1 y GS (fotograma = max de los tres), a costa de hasta 2 imagenes de latencia de entrada (33 ms a 60, 17 ms a 120). Para I1 no cambia nada: la interpolacion es del lado del EE (dibujar con el estado interpolado) y la cola solo transporta ordenes del GS. Propuesta: profundidad 1 por defecto y 2 como opcion, medida en H4.
3. **Nucleos del M1**: su ayudante de listas de teselas esta apagado por memoria y por no ganar; con nuestro GS en GPU el hilo del GS queda ligero, lo que libera nucleos P para T1 (ya en §0.2).

## 4. Riesgos y limites: lo que ellos simplifican y nosotros no
| Simplificacion suya | Cita | Por que no nos vale | Efecto en nuestro plan |
|---|---|---|---|
| VU1 con redondeo del host, sin U, latencia uniforme, ciclos no exactos | 2.1 | regresion por hashes de RAM y banco con ciclos exactos (D1, P1) | mantener P1-P6 tal cual |
| Raster `f32`, sin dithering, resultados que dependen del compilador de shaders (`EXPLICIT_FMA`) | `ref: gs.wgsl:17-35` | G2 v1 quiere VRAM identica a la CPU en las rutas enteras | shader propio con aritmetica entera; tolerancia solo donde la CPU sea flotante |
| "Una instruccion = un ciclo", transferencias/VU/GS sin tiempo | README, "What it does not do" | nuestro EE ya tiene su modelo; no cambia | — |
| Mezcla de doble fuente no repetible en Intel | `ref: hwdraw.cpp:272-276` | rompe hashes | no usar rasterizado fijo para la regresion |
| Determinismo: reproduccion con huella cada 15 fotogramas del lado del EE | README "Saves and checks" | igual que nuestro modo determinista: el GPU no toca la RAM del EE salvo local→host | confirma §0.5 punto 3 |

**Dependencia de wgpu-native**: el backend de GPU (`gpu.cpp` 4 220 lineas, `hwdraw.cpp` 4 491, `hwcache.cpp` 912, shaders WGSL ~2 100) son ~11 000-12 000 de ~80 000 lineas; el EE, el VU, el IOP, el audio y `raster.cpp` (584, el de respaldo en C++) no dependen de el. En Mac el WGSL pasa a MSL por la traduccion de wgpu; la ruta experimental nunca se ha probado en Metal (`ref: hwdraw.cpp:34`).

## 5. Recomendacion: cambios al plan
| # | Cambio | Tipo | Coste | Ahorro estimado | Hipotesis |
|---|---|---|---|---|---|
| 1 | **G0** clasifica las ~8 000 primitivas serie en las categorias de 1.3: rango UV real frente a textura entera, "dibujado debajo" (cajas del tramo), lectura del propio pixel 1:1, byte alto (T8H/4HL/4HH frente a Z24/CT24), sprites pequenos con palabras disjuntas, realimentacion real; y cuenta lotes/dispatches que saldrian con sus reglas | cambiado | igual (1 sesion) | decide G1-a y el numero de pasadas de G2 | — |
| 2 | **G1-a** = sus reglas en la CPU + **snapshot** del rango comun antes del tramo paralelo (las bandas leen los texeles de ese rango de la copia) | cambiado (mas alcance) | 1-2 → 2-3 sesiones | si las 8 000 son de los tipos 2-5, la parte serie (~12 ms/V) casi desaparece: techo de CPU de ~30 a **35-45 V/s** | **[H fuerte, lo dice G0]** |
| 3 | **D2** = comprobacion de 0,5 sesion de paraLLEl-GS (extensiones frente a MoltenVK, licencia) + decision "Metal propio con la arquitectura de ps2-native-recomp" | cambiado | 1-2 → 0,5-1 | 0,5-1 sesion | — |
| 4 | **G2-pre** (nuevo, Sonnet): microbanco Metal en el M1: coste de una dispatch vacia, de `atomic_fetch_xor` frente a store en un `MTLBuffer` compartido, y de 1 000 lotes pequenos por fotograma | nuevo | 0,5 | evita disenar G2 sobre supuestos | — |
| 5 | **G2 v1** con su esqueleto: listas de teselas 8x8 en la CPU (indices de 16 bits, lotes ≤65 536), lote por FRAME/ZBUF, snapshot, CLUT por ranuras versionadas, subidas/copias como dispatches con records, un solo compute encoder por envio; shader exacto propio | cambiado | 8-15 Opus + 4-8 Sonnet → 6-11 + 3-6 | **3-5 sesiones** | [H] |
| 6 | **P1/P6**: clave por alcanzable estatico + entrada; destinos de `JR` como entradas; comprobacion muestreada en el juego | cambiado | +0,5 | 1-3 sesiones (menos variantes y menos cola tras P6; mejora el leave-one-out del 86-88 %) | [H: que el alcanzable estatico de las imagenes de AirBlade no arrastre codigo muerto que no compile] |
| 7 | **P5/P7**: pegajosas por 3 carriles; quitar la prueba de `FPCR.FZ` (o dejarla como comprobacion rapida que se espera negativa) | cambiado | 0 | 0-1 | [H] |
| 8 | **T1** con el modelo de drenaje por acceso (sin barrera por kick); antes, inventario de accesos del EE al VU1/VIF1 y de SIGNAL/FINISH en PATH1 | cambiado | 4-8 → 3-6 (+0,5 de inventario) | 0,5-1,5 | [H] |
| 9 | Profundidad de cola de imagenes configurable (1 por defecto, 2 opcional) medida en H4 | nuevo, pequeno | dentro de T1/G2 | — | — |
| — | Descartado: su aritmetica del VU1, VF en memoria, DAZ perezoso, compilador/JIT en la maquina del jugador, ruta de triangulos por hardware, wgpu como dependencia | descartado | — | — | — |
**Ahorro total estimado**: 5-11 sesiones sobre las ~40-90 hacia 60 (casi todo en la via del GS, que va en paralelo; el camino critico baja 1-4) **[H]**. El mayor efecto posible no es el ahorro sino el punto 2: si G1-a con sus reglas lleva el GS por CPU a 35-45 V/s, H3 deja de estar limitado por el GS antes de G2.

## 6. Licencias y atribucion
- ps2-native-recomp: **MIT**, `Copyright (c) 2026 Samir Nuri` y `Copyright (c) 2026 exoteric33` (`ref: LICENSE`); base ps2-web-recomp de Samir Nuri (MIT). Las ideas y el diseno (reglas de corte, snapshot, CLUT por ranuras, clave por alcanzable) se pueden usar sin aviso, pero los citamos en los parches y en el ADR por honestidad.
- Si se copia o traduce **codigo** (por ejemplo tablas de swizzle, `locate`/`addr`, `texel_rect`, el constructor de listas de `tiles.cpp`, `reachable`/`program_key`, `stall_plan`): el fichero nuestro lleva una cabecera "Adaptado de ps2-native-recomp (MIT), Copyright (c) 2026 Samir Nuri, Copyright (c) 2026 exoteric33" y el texto completo de la licencia MIT va en un `THIRD_PARTY_NOTICES` del runner/runtime. Va como parche en `patches/` como siempre; un parche por origen para que la atribucion no se pierda.
- `runtime/src/ipu/NOTICE`: las tablas VLC del IPU vienen de Play! (BSD de 2 clausulas, Jean-Philip Desjardins). Solo importa si se tomara su decodificador MPEG; no es el caso.
- wgpu-native (MIT o Apache-2.0, segun el proyecto wgpu; **no viene en su repo**, se descarga) y SDL2 (zlib): no los usamos.
- No se ha copiado ningun codigo a este repo para este informe.

## 7. Incognitas que mas cambian la decision
1. **Que son las ~8 000 primitivas serie** (G0): si son de los tipos 2-5 de 1.3, G1-a da mucho en la CPU y G2 tiene pocas pasadas; si son realimentacion real pixel a pixel, ni su modelo ni el nuestro baja de miles de dispatches por fotograma y G2 necesita otra idea (agrupar por region, raster por tesela con barreras de threadgroup) **[H]**.
2. **Coste en el M1 de una dispatch y de los atomicos** en un buffer compartido (G2-pre): fija cuantos lotes por fotograma aguanta el diseno y si los formatos de 16/8/4 bits necesitan otra solucion.
3. **Cuanto cuesta en el shader la cobertura entera exacta** frente a su `f32`: si dobla el coste por pixel, el objetivo de ≤6 ms de G2 v2 se aleja **[H]**.
4. **Accesos del EE al estado del VU1** y SIGNAL/FINISH en PATH1 en AirBlade: deciden si T1 puede ir sin barrera por kick.
5. **Variantes por alcanzable estatico** en las imagenes de 16 KB de AirBlade: si colapsan las 72 a ~30 y compilan, la cola de P6 baja y el x5,5-8,5 se sostiene en escenas nuevas.
