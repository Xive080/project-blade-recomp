#!/usr/bin/env python3
"""Distribucion de tiempos de VSync a partir de volcados cada 1 VSync (mtime de frame_TTTTTT.png).
Uso: qa_frametimes.py carpeta [desde hasta]. Solo cuenta intervalos entre volcados consecutivos; si el volcador se salta
VSync se reparte el intervalo entre los VSync saltados (tiempo medio por VSync de ese tramo)."""
import os, re, glob, sys, statistics
d = sys.argv[1]
lo = int(sys.argv[2]) if len(sys.argv) > 2 else 0
hi = int(sys.argv[3]) if len(sys.argv) > 3 else 10**9
pts = sorted((int(re.search(r"(\d+)\.png$", f).group(1)), os.stat(f).st_mtime_ns) for f in glob.glob(d + "/frame_*.png"))
pts = [p for p in pts if lo <= p[0] <= hi]
per = []   # tiempo por VSync en ms (uno por VSync)
clean = 0
for (v0, t0), (v1, t1) in zip(pts, pts[1:]):
    dv = v1 - v0
    if dv <= 0: continue
    ms = (t1 - t0) / 1e6 / dv
    per += [ms] * dv
    clean += dv == 1
per.sort()
n = len(per)
def pct(p): return per[min(n - 1, int(p / 100 * n))]
dur = (pts[-1][1] - pts[0][1]) / 1e9 if len(pts) > 1 else 0
print(f"VSync medidos={n} (intervalos de 1 VSync={clean}) duracion={dur:.1f}s VSync/s medio={n/dur:.2f}")
print(f"tiempo por VSync ms: media={statistics.mean(per):.1f} mediana={pct(50):.1f} min={per[0]:.1f} p1%(mejor 1%)={pct(1):.1f} p99%(peor 1%)={pct(99):.1f} max={per[-1]:.1f}")
print(f"equivalente V/s: media={1000/statistics.mean(per):.2f} 1% peor (1%low)={1000/pct(99):.2f} max={1000/per[0]:.2f}")
