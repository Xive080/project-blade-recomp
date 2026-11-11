# Modo determinista (QA)

Ejecuta el juego sin reloj de pared: los plazos se cumplen por ciclos del EE y la entrada se aplica por tick de VSync del juego.
Dos ejecuciones iguales dan RAM y fotogramas identicos bit a bit. Sin `PS2X_DETERMINISTIC=1` el runtime no cambia.

Requiere el parche `patches/ps2recomp-z-deterministic.patch` (aplica despues de ee-idle-iop y pal-vblank) y `runner/qa_deterministic.cpp`.
El gancho solo se instala si hay alguna variable `BLADE_DET_*` definida.

| Variable | Efecto |
|---|---|
| `PS2X_DETERMINISTIC=1` | sin ritmo de reloj de pared; corre tan rapido como puede |
| `BLADE_DET_INPUT="tick[+duracion]:BOTON;..."` | misma sintaxis que `BLADE_INPUT`, pero tick de VSync exacto (duracion por defecto 6 ticks) |
| `BLADE_DET_DUMP=carpeta,cada_N[,desde]` | PNG del fotograma presentado en ese VBlank (`frame_NNNNNN.png`) |
| `BLADE_DET_HASH=cada_N[,desde]` | imprime `[det] tick N ram=<fnv1a RAM 32 MB> frame=<fnv1a RGB>` |
| `BLADE_DET_EXIT=N` | sale al llegar al tick N |

Ejemplo (guion que llega a la partida; el nivel 1 empieza hacia el tick 2500):

    PS2X_DETERMINISTIC=1 BLADE_HIDDEN=1 \
    BLADE_DET_INPUT="300:START;400:START;500:START;600:START;700:START;900:CROSS;1050:CROSS;1200:CROSS;2250:CROSS;2400:CROSS" \
    BLADE_DET_DUMP=salida,50 BLADE_DET_HASH=50 BLADE_DET_EXIT=3950 ./blade_recomp disc/SCES_502.46

Regresion: `scripts/qa_regression.sh` (referencias en `qa/ref_det`). Un tick de volcado fino (`cada_N`=1 desde un tick) permite biseccionar.
