# PERFORMANCE_PLAN: de 11-15 V/s a 50 (PAL), y despues a 60 y 120

Fecha: 2026-11-12. Base: `main` en c6d6c69 (linea base limpia de AB-001). Es un plan: no cambia codigo.
Fuentes: `docs/PERFORMANCE_BASELINE.md` (seccion 2026-11-12), README (Pendiente 3, VU1 compilado, `BLADE_VU1_DIFF`, Plan B),
`ps2xRuntime/src/lib/vu/*` (interprete y ruta `VU1_GEN`), `work/vu1gen/` (`nivel.rec` y los `.inc` generados),
`work/output_orig` (bucle principal del juego) y `EeScheduler.cpp`. Lo que he comprobado yo esta en el anexo A; lo que es
hipotesis va marcado **[H]**.

## Resumen (10 lineas)
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

### D1. Decidir el oraculo (Opus) — antes de P1
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

Cadena de regresion por paso (de barata a cara): (1) `BLADE_VU1_DIFF` del programa tocado, ~6 s por fichero; (2) corpus
completo con `EXIT` y `FUZZ`, ~2 min; (3) `scripts/check_patches.sh`; (4) `scripts/qa_regression.sh` identico frente a
`qa/ref_det`, ~6 min; (5) V/s en modo normal, 2 corridas. Un paso de rendimiento con cualquier diferencia en (1)-(4) no entra.

---

## 6. Estimaciones (rangos; V/s del nivel 1, tramo 2600-3900)
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
- **Sin comprobar** (y por eso con [H] arriba): que el tiempo del hilo del rasterizador sea trabajo y no espera, que las
  reanudaciones dependan entre si en todos los casos, cuantos VF vivos tiene cada programa, si la logica usa `dt` en todas partes,
  y que significa `[0x70003FAC]`.
- **Discrepancia sin explicar**: dev 2 media 22-23 V/s con el VU1 compilado y la linea base limpia da 14,6. Coincide con que despues
  de AB-004/AB-017 se dibujan el jugador, la tabla y la sombra (mas trabajo del VU1 y del GS) **[H]**. Para comparar con cifras
  viejas, hay que medir los ciclos del VU1 por VSync de las dos builds en el mismo tramo.
