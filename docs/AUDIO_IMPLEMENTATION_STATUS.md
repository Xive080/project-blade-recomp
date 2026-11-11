# Audio / SPU2 - estado de la implementacion

Marcas: INVESTIGATING / IMPLEMENTING / BUILT / TESTING / BLOCKED. Nada esta VERIFIED: eso lo certifica QA.

## Auditoria de uso (TSSDRV + LIBSD reales, corriendo como codigo MIPS del IOP)
- Init de TSSDRV: `sceSdSetParam` con 0x980/0xa80 = 0x3fff y 0xd81/0xe81 = 0x7fff. El "set master" de 0x14a4 solo
  actualiza una tabla sombra.
- La funcion 0x10674 si escribe MVOL, pero solo se alcanza desde la ruta de streams (0xbe64).
- Registros usados: voces (VOLL/R, PITCH, ADSR1/2, SSA), KON/KOFF, TSA + puerto de datos y DMA a la RAM del SPU2, ESA/EEA y
  registros de reverb, VMIX/MMIX, ATTR, volumenes maestros (0x760 + core*0x28).
- No usados en las ejecuciones observadas: barridos de volumen, ruido, PMON, ADMA/AUTODMA.

## Puntos
| Punto | Estado | Evidencia |
|---|---|---|
| Decodificador ADPCM | TESTING | `spu2_dsp_tests`: vectores a mano + 20 bloques contra el decodificador adpcm_psx de FFmpeg (oraculo independiente, `scripts/gen_adpcm_vectors.py`): filtro 0 identico al bit; filtros 1-4 dentro de 36 LSB (FFmpeg no es bit-exacto con el hardware: no prueba el redondeo exacto) |
| ADSR (ataque/decaimiento/sostenido/liberacion) | TESTING | `spu2_dsp_tests` |
| Voces: tono, bucle, ENDX, KON/KOFF | TESTING | `spu2_mix_tests`: 0x1000/0x2000/0x0800 medidos 1715/3430/857,5 Hz (esperado 1714,3/3428,6/857,1) |
| Volumenes de voz y maestros | TESTING | test de volumen 0,5 -> pico 12288 |
| Reverb | TESTING | test: la cola decae; en el juego EVOL es ~0,05 (poco audible) |
| Sincronia con el reloj del IOP | TESTING | 3600 ticks (60 Hz) -> WAV de 60,05 s; con el parche PAL 50 Hz de dev 2, 1500 ticks -> 30,01 s |
| Salida WAV / raylib | TESTING | `BLADE_AUDIO_DUMP` |
| Barridos, ruido, PMON, ADMA | IMPLEMENTING (no hecho) | el juego no los usa |
| Comparacion con PCSX2 | BLOCKED | no hecha: PCSX2 solo graba audio desde la interfaz grafica (menu/atajo sin asignar) y la sesion de pantalla del Mac estaba bloqueada, sin forma de pulsar teclas; no hay captura de audio por linea de comandos. Sin verificar |

## Hallazgos
- H1 (HIPOTESIS, no hecho): en la traza el MVOL del core 1 (0x1F900788) solo recibe ceros (LIBSD lo silencia al
  iniciar); el del core 0 recibe 0x3fff. Con el registro tal cual saldria silencio; por defecto se usa un valor "asumido"
  (`PS2X_SPU_MASTER=strict` aplica el registro tal cual).
  - Hecho (codigo abierto de PCSX2, `pcsx2/SPU2/Mixer.cpp`, `ADSR.cpp`, `spu2sys.cpp`, leidos solo como idea): PCSX2 aplica
    `Cores[1].MasterVol` a la salida final y `Cores[0].MasterVol` a la del core 0, con reset a 0; si el juego no lo fijara,
    PCSX2 tambien sonaria mudo. Luego el juego lo fija en hardware real/PCSX2 por un camino que aqui no se ve.
  - Evidencia de la busqueda exhaustiva (trazas `PS2X_SPU_TRACE` de todas las escrituras de 8/16/32 bits a registros del
    SPU2, hasta ~55 s de juego con menus e historia; mas una ejecucion de 30 s con un vigilante temporal de PC en el IOP,
    ya retirado del codigo):
    - MVOL core 0 (0x1F900760, L y R): 0 al reiniciar LIBSD, luego 0x3fff / 0x3fff desde TSSDRV (`sceSdSetParam(0x980/0xa80)`).
    - MVOL core 1 (0x1F900788, L y R): solo escrituras de valor 0 (LIBSD lo silencia al iniciar). Nunca otro valor.
      Ningun valor con bit 15 (barrido) en ninguno de los dos MVOL, asi que no es un problema de formato del decodificador.
    - 0x1F9001B0/0x1F9005B0: solo ceros (no es un MVOL).
    - AVOL core 1: 0x7fff (TSSDRV, `sceSdSetParam(0xd81/0xe81)`). Los RPC de la EE al servidor de LIBSD (sid 0x701) y a
      TSSDRV (sid 0x475453) no producen ninguna escritura a MVOL; las unicas escrituras a MVOL son las de arriba.
    - Analisis estatico de todos los IRX del disco (busqueda de las constantes 0x980/0xa80/0x981/0xa81): solo TSSDRV las
      usa. La unica rutina que escribe el MVOL del core 1 es TSSDRV `0x10674(core, volL, volR)` (copia de una tabla sombra
      en 0x119ac a `sceSdSetParam(core|0x980)`), llamada solo desde `0xbe64(0x109a8, 0)`. `0x14a4(vol)` (API publica
      "volumen maestro") solo escribe esa tabla sombra, no el registro.
    - Dinamico (TSSDRV cargado en 0x54500): `0x10674` no se ejecuta nunca; `0xbe64` si, pero con los objetos de stream por
      voz (0x6efd8, 0x6f064), nunca con el objeto 0x109a8 (el unico que dispara la escritura). Ese objeto parece ser el
      stream maestro por AutoDMA (`sceSdBlockTrans`, tambien relacionado con AVOL/BVOL 0x1080/0xf80), que aqui no se activa.
  - Conclusion: no se ha encontrado ninguna escritura real al MVOL del core 1, ni con otro formato. El valor "asumido"
    queda como apano documentado (HIPOTESIS: el juego lo fija al activar el stream maestro AutoDMA, funcion no implementada
    en este emulador y no alcanzada en las escenas probadas). Si el juego sonara mudo en PCSX2 sin ese stream, la
    discrepancia seria un hallazgo de QA que habria que contrastar con una captura de PCSX2 (pendiente, a cargo de QA).
- AVOL/BVOL son enteros con signo de 16 bits (0x7fff = maximo), no el formato de volumen fijo de MVOL. Hecho (PCSX2):
  `ExtVol` se guarda como valor crudo, reset al maximo, y se aplica `(vol*dato)>>15`; MVOL en modo fijo es
  `SignExtend16(reg<<1)`, igual que `fixedVolume` aqui.
- El reverb lo usa el core 0; la salida final es el core 1 (la entrada externa del core 1 es la salida del core 0 * AVOL).
- Flujo mono repartido en ambos cores.

## Desviaciones conocidas (sin verificar contra hardware)
- Interpolacion cubica Catmull-Rom en vez de la tabla Gauss del SPU2.
- Algoritmo de reverb estilo PSX (IIR/comb/APF, paso a 24 kHz, FIR de 39 taps).
- ADSR segun la descripcion de nocash.

## AutoDMA / stream maestro (0x109a8): NO alcanzado, no implementado (UNVERIFIED)
- Prueba con el guion del README + una X cada ~300 ticks hasta el VSync 5400, parche PAL de 50 V/s (guion desfasado respecto a 60 V/s):
  `BLADE_EXIT_TICK=6000` (120 s de juego), fotogramas cada 500 ticks (`BLADE_DUMP_FRAMES`). Se llega a la partida del nivel 1:
  a partir de ~VSync 3000 se ve el HUD (barra "boost", marcador, cuenta atras 2:19 -> 1:52), pasillos y un mensaje de tutorial.
- Vigilante temporal de PC en TSSDRV (base 0x54500; ya retirado): `0x10674` 0 veces; `0xbe64` 14 veces, siempre con los objetos de
  stream por voz (0x6efd8, 0x6f064, 0x6f0f0, 0x6f17c), nunca con el objeto 0x109a8.
- Registros ADMAS 0x1B0/0x5B0: solo un cero. MVOL core 1 sigue solo en 0. Los DMA al SPU2 son transferencias de memoria (no
  continuas): 4096 bytes repetidos (streaming de musica por voces con buffer circular) y bancos.
- Tick maximo probado: 6000 (VSync a 50 Hz). Conclusion: la ruta AutoDMA no se alcanza en el nivel 1 hasta ahi, asi que no se ha
  implementado AutoDMA (sin comportamiento real que comprobar, no se falsea). Pendiente si algun modo/escena lo usa.
- SFX en la partida: el WAV de 104 s (`work/audio/run10_play.wav`, mismo guion) tiene 33 eventos transitorios en 60-104 s (partida,
  tramo jugado) frente a 9 en 20-50 s (menus), ademas de la musica; unos 70 KON de voz en la partida.

## Prueba con el juego (guion de README, sin altavoces)
`BLADE_HIDDEN=1 BLADE_EXIT_TICK=3600 BLADE_AUDIO_DUMP=salida.wav BLADE_INPUT="300:START;400:START;500:START;600:START;700:START;900:CROSS;1050:CROSS;1200:CROSS;2250:CROSS;2400:CROSS" build/blade_recomp disc/SCES_502.46`
- Resultado (60 Hz): 60,05 s, musica desde ~7,1 s, sin saltos > 20000 (clicks). Con 50 Hz: musica desde ~8,5 s (7,1 x 1,2), coherente.
- Para enlazar sin recompilar los 3316 ficheros generados: `scripts/link_audio.sh`.
- Serie de parches: `z-spu2-mixer` aplica sobre los 16 anteriores de `patches/` (incluidos vu1-fast, pal-vblank, vu1-commit-once, vu1-profile, vu1-record) en un worktree limpio de e563698; ejecucion con la serie completa: 1500 ticks -> 30,01 s, mismo WAV que antes.
- Tests (CMake desde cero, ambos pasan): `-DPS2X_IOP_BUILD_TESTS=ON`, ejecutables `ps2_iop_spu2_dsp_tests` y `ps2_iop_spu2_mix_tests`.

## Pendiente
Comparar con PCSX2, confirmar H1, Gauss y reverb exactos, barridos/ruido/PMON/ADMA si algun modo del juego los usa.
