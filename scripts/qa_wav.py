#!/usr/bin/env python3
"""Analisis independiente de un WAV volcado (BLADE_AUDIO_DUMP): duracion, RMS por segundo, pico, saturacion, saltos
(clics), silencios, canales y espectro (frecuencia dominante y centroide) por ventanas.
Uso: qa_wav.py archivo.wav [ticks_esperados hz]"""
import sys, wave, array, math, cmath
w = wave.open(sys.argv[1]); nch, sw, sr, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
raw = w.readframes(n)
assert sw == 2, f"profundidad {sw*8} bits no soportada por este analizador"
s = array.array("h"); s.frombytes(raw)
if sys.byteorder == "big": s.byteswap()
dur = n / sr
print(f"canales={nch} frecuencia={sr} Hz muestras/canal={n} duracion={dur:.2f} s")
if len(sys.argv) > 3:
    t, hz = float(sys.argv[2]), float(sys.argv[3]); print(f"duracion esperada por VSync: {t/hz:.2f} s (diferencia {dur-t/hz:+.2f} s)")
ch = [s[c::nch] for c in range(nch)]
for c, x in enumerate(ch):
    pk = max(max(x), -min(x)); rms = math.sqrt(sum(v * v for v in x) / len(x))
    clip = sum(1 for v in x if v >= 32767 or v <= -32768)
    jumps = sum(1 for i in range(1, len(x)) if abs(x[i] - x[i - 1]) > 20000)
    dc = sum(x) / len(x)
    print(f"canal {c}: pico={pk} rms={rms:.0f} ({20*math.log10(max(rms,1)/32768):.1f} dBFS) saturadas={clip} saltos>20000={jumps} continua={dc:.1f}")
x = ch[0]
sec = []
for i in range(0, len(x) - sr + 1, sr):
    seg = x[i:i + sr]; sec.append(math.sqrt(sum(v * v for v in seg) / sr))
print("RMS por segundo:", " ".join(f"{r:.0f}" for r in sec))
first = next((i for i, r in enumerate(sec) if r > 200), None)
print("primer segundo con RMS>200:", first)
sil = [i for i, r in enumerate(sec) if r < 5]
print("segundos casi silenciosos (RMS<5):", len(sil), "de", len(sec))
def fft(a):
    n = len(a)
    if n == 1: return a
    e, o = fft(a[0::2]), fft(a[1::2]); t = [cmath.exp(-2j * math.pi * k / n) * o[k] for k in range(n // 2)]
    return [e[k] + t[k] for k in range(n // 2)] + [e[k] - t[k] for k in range(n // 2)]
N = 4096
print("espectro por ventana (cada ~5 s): t, frec. dominante Hz, centroide Hz, energia 0-200 / 200-2k / 2k-8k / >8k (%)")
for t0 in range(0, int(dur) - 1, 5):
    seg = x[t0 * sr: t0 * sr + N]
    if len(seg) < N: break
    win = [v * (0.5 - 0.5 * math.cos(2 * math.pi * i / (N - 1))) for i, v in enumerate(seg)]
    sp = [abs(v) for v in fft(win)[:N // 2]]
    tot = sum(v * v for v in sp)
    if tot < 1e3: print(f"{t0:4d}s  silencio"); continue
    dom = max(range(1, N // 2), key=lambda k: sp[k]) * sr / N
    cen = sum(k * sr / N * sp[k] for k in range(N // 2)) / sum(sp)
    def band(a, b): return 100 * sum(sp[k] ** 2 for k in range(int(a * N / sr), int(b * N / sr))) / tot
    print(f"{t0:4d}s  {dom:7.0f}  {cen:7.0f}   {band(0,200):4.0f}/{band(200,2000):4.0f}/{band(2000,8000):4.0f}/{band(8000,sr/2):4.0f}")
