#!/usr/bin/env python3
"""Compara fotogramas volcados con las referencias: diferencia media por canal (0-255) y % de pixeles distintos.
Uso: qa_compare.py <carpeta_volcado> <carpeta_referencia> <informe.md> [hito ...]
Un hito es un VSync (frame_TTTTTT.png). Si no existe exacto, se toma el mas cercano dentro de +-TOL VSync."""
import sys, os, re, glob
from PIL import Image, ImageChops, ImageStat

TOL = 8
def frames(d):
    out = {}
    for f in glob.glob(os.path.join(d, "frame_*.png")):
        m = re.search(r"frame_(\d+)\.png$", f)
        if m: out[int(m.group(1))] = f
    return out

def nearest(fr, t):
    if not fr: return None
    k = min(fr, key=lambda x: abs(x - t))
    return (k, fr[k]) if abs(k - t) <= TOL else None

def metric(a, b):
    ia, ib = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    if ia.size != ib.size: return None
    d = ImageChops.difference(ia, ib)
    mean = sum(ImageStat.Stat(d).mean) / 3.0
    mx = d.convert("L").point(lambda v: 255 if v > 0 else 0)
    pct = 100.0 * (sum(mx.getdata()) / 255) / (ia.size[0] * ia.size[1])
    return mean, pct

def best_any(fc, ref_path):
    """La linea de tiempo del guion varia con la velocidad del host: busca el fotograma del volcado mas parecido a la referencia."""
    best = None
    for k, f in fc.items():
        m = metric(f, ref_path)
        if m and (best is None or m[0] < best[1]): best = (k, m[0])
    return best

def main():
    cur, ref, rep = sys.argv[1:4]
    hitos = [int(x) for x in sys.argv[4:]] or [650, 900, 1100, 3000, 3900]
    fc, fr = frames(cur), frames(ref)
    lines = ["| Hito (VSync) | actual | referencia | dif. media (0-255) | % pixeles distintos | veredicto | mejor pareja en todo el volcado (VSync, dif. media) |", "|---|---|---|---|---|---|---|"]
    bad = 0
    for h in hitos:
        a, b = nearest(fc, h), nearest(fr, h)
        if not a or not b:
            lines.append(f"| {h} | {a[0] if a else 'FALTA'} | {b[0] if b else 'FALTA'} | - | - | FALTA | - |"); bad += 1; continue
        m = metric(a[1], b[1])
        if m is None:
            lines.append(f"| {h} | {a[0]} | {b[0]} | - | - | TAMANO DISTINTO | - |"); bad += 1; continue
        v = "IDENTICO" if m[1] == 0 else ("CERCA" if m[0] < 2.0 and m[1] < 5.0 else "DIFERENTE")
        if v == "DIFERENTE": bad += 1
        ba = best_any(fc, b[1]); lines.append(f"| {h} | {a[0]} | {b[0]} | {m[0]:.3f} | {m[1]:.3f} | {v} | {ba[0] if ba else '-'}, {ba[1]:.2f} |")
    open(rep, "w").write("\n".join(lines) + "\n")
    print("\n".join(lines))
    sys.exit(1 if bad else 0)
main()
