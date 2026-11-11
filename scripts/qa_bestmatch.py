#!/usr/bin/env python3
"""Compara dos volcados sin depender del VSync exacto (la linea de tiempo del guion varia con la velocidad del host):
para cada fotograma de A busca el mas parecido de B (mismo conjunto de miniaturas 160x128) y da la diferencia media.
Uso: qa_bestmatch.py carpetaA carpetaB [umbral_dif_media=3]"""
import sys, glob, re
from PIL import Image, ImageChops, ImageStat
def load(d):
    out = {}
    for x in glob.glob(d + "/frame_*.png"):
        k = int(re.search(r"(\d+)\.png$", x).group(1))
        out[k] = Image.open(x).convert("RGB").resize((160, 128), Image.BILINEAR)
    return out
A, B = load(sys.argv[1]), load(sys.argv[2]); thr = float(sys.argv[3]) if len(sys.argv) > 3 else 3.0
good = 0; rows = []
for ka in sorted(A):
    best = min(((sum(ImageStat.Stat(ImageChops.difference(A[ka], B[kb])).mean) / 3, kb) for kb in B))
    ok = best[0] <= thr; good += ok
    rows.append((ka, best[1], round(best[0], 2), ok))
print(f"{good}/{len(A)} fotogramas de A tienen pareja en B con dif. media <= {thr}")
print("sin pareja:", [(r[0], r[1], r[2]) for r in rows if not r[3]])
