#!/usr/bin/env python3
"""Reparte las muestras de un 'sample' de macOS entre categorias (por hilo y por funcion, solo el nodo mas externo).
Uso: qa_sample_split.py sample.txt"""
import re, sys
lines = open(sys.argv[1], errors="replace").read().split("\n")
thr = re.compile(r"^    (\d+) (Thread_\d+)(?::\s*(.*)|\s+(.*))?$")
node = re.compile(r"^([ +!:|]*)(\d+) (.*)$")
cats = {
 "VU1 (interprete)": r"VU1Interpreter::run",
 "VIF1/DMA (total, incluye VU1)": r"PS2Memory::processVIF1Data|processPendingTransfers",
 "IOP (runCycles)": r"IopEmulator::Impl::runCycles|IopKernel::beginNextReady",
 "Espera sky2 0x1DF500": r"FUN_001df500",
 "Rasterizador GS": r"GSCpuBackend::(SubmitMany|DrawPrimitive|SampleTexture|WritePixel)",
 "Temporizadores EE": r"advanceEeTimers",
}
out = {}
cur = None
for ln in lines:
    if ln.startswith("Sort by top of stack") or ln.startswith("Binary Images"): break
    m = thr.match(ln)
    if m:
        cur = {"id": m.group(2), "n": int(m.group(1)), "name": (m.group(3) or m.group(4) or "").strip(), "cats": {k: 0 for k in cats}, "stack": []}
        out[cur["id"]] = cur; continue
    if cur is None: continue
    m = node.match(ln)
    if not m: continue
    depth = len(m.group(1)); cnt = int(m.group(2)); txt = m.group(3)
    cur["stack"] = [s for s in cur["stack"] if s[0] < depth]
    for k, rx in cats.items():
        if re.search(rx, txt) and not any(s[1] == k for s in cur["stack"]):
            cur["cats"][k] += cnt
    for k, rx in cats.items():
        if re.search(rx, txt): cur["stack"].append((depth, k))
    if "first" not in cur: cur["first"] = txt[:90]
for t in out.values():
    if t["n"] < 100: continue
    print(f"{t['id']} {t['name'][:30]:30s} muestras={t['n']} " + "; ".join(f"{k}={v} ({100*v/t['n']:.0f}%)" for k, v in t["cats"].items() if v))
