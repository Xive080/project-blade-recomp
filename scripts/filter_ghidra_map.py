#!/usr/bin/env python3
"""Prepara el mapa CSV de Ghidra (ExportPS2Functions, con ghidra-emotionengine-reloaded) para PS2Recomp.

1. Deja solo las funciones: quita las etiquetas internas (entry_XXXXXXXX, caseD_*, default, switchD_*, LAB_*) que el
   script exporta como filas propias. El recompilador ya reentra a mitad de funcion con su `switch (ctx->pc)`.
2. Recupera inicios que no estan dentro de ninguna funcion: codigo al que solo se llega por punteros (tablas de funciones
   de RenderWare, p. ej. 0x1C2880). Candidatos: etiquetas de Ghidra (codigo ya desensamblado) fuera de toda funcion; los
   punteros sueltos de los datos no se usan porque meten zonas de datos como codigo. Se aceptan solo si parecen un inicio (detras de `jr $ra` + delay slot, detras de relleno, o con
   `addiu $sp,$sp,-N`), para no cortar nunca en un delay slot. Misma idea que tools/find_missing_starts.py de
   hctp-recomp (GPL-3.0).
3. Codigo en huecos pequenos (<= 0x400) entre funciones: su primer word no nulo es un inicio. En huecos grandes, solo
   una hoja corta pegada a la funcion anterior o un hueco (<= 0x4000) que acaba en `jr $ra` + delay slot + relleno.

Uso: filter_ghidra_map.py ghidra.csv game.elf salida.csv
"""
import csv, re, struct, sys

src, elf, dst = sys.argv[1:4]
d = open(elf, 'rb').read()
phoff, = struct.unpack_from('<I', d, 0x1C)
phentsize, phnum = struct.unpack_from('<HH', d, 0x2A)
segs = []
for i in range(phnum):
    p_type, p_off, p_vaddr, _, p_filesz, _, _, _ = struct.unpack_from('<8I', d, phoff + i * phentsize)
    if p_type == 1 and p_filesz:
        segs.append((p_vaddr, p_off, p_filesz))

def word(va):
    for v, o, n in segs:
        if v <= va < v + n:
            return struct.unpack_from('<I', d, o + va - v)[0]
    return None

rows = list(csv.DictReader(open(src)))
label = re.compile(r'^(entry_[0-9a-f]|caseD_|default|switchD_|LAB_)')
funcs = {int(r['Start'], 16): (r['Name'], int(r['End'], 16)) for r in rows if not label.match(r['Name'])}
lo = min(funcs)
hi = max(e for _, e in funcs.values())
ranges = sorted((s, e) for s, (_, e) in funcs.items())

def inside(t):
    import bisect
    i = bisect.bisect_right(ranges, (t, 0xFFFFFFFF)) - 1
    return i >= 0 and ranges[i][0] <= t < ranges[i][1]

def skip_pad(t):
    # Ghidra a veces pone la etiqueta al principio del relleno de nops (0x219830 -> la funcion empieza en 0x219840)
    while word(t) == 0:
        t += 4
    return t

def ends_func(v):
    # `jr $ra` o `j` de cola en v-4 (v es su delay slot) o en v
    for a in (v - 4, v):
        w = word(a)
        if w == 0x03E00008 or (w is not None and w >> 26 == 2):
            return True
    return False

def plausible(t):
    first = word(t)
    if first is None or first == 0:
        return False
    if (first >> 16) == 0x27BD and first & 0x8000:  # addiu $sp,$sp,-N
        return True
    # Relleno = nops detras del final de una funcion (`jr $ra`/`j` + delay slot). Un nop suelto no basta: en 0x21982C
    # es un nop de riesgo antes del `jr $ra` de 0x219830, que es el final de la funcion 0x2197D0.
    v = t - 4
    while word(v) == 0:
        v -= 4
    return ends_func(v)

cands = {skip_pad(int(r['Start'], 16)) for r in rows if label.match(r['Name'])}
added = sorted(t for t in cands if t not in funcs and not inside(t) and plausible(t))

def short_leaf(t):
    # `jr $ra` en menos de 0x100 bytes, sin otro salto antes, y relleno de nops detras del delay slot
    for a in range(t, t + 0x100, 4):
        w = word(a)
        if w is None or w == 0x42000018:  # eret: codigo de kernel
            return None
        if w == 0x03E00008:
            return a + 8 if word(a + 8) == 0 else None
        if w >> 26 in (2, 3) or (w >> 26 == 0 and (w & 0x3F) in (8, 9)):
            return None
    return None

def code_gap(t, nxt):
    # hueco de hasta 0x4000 bytes que acaba en `jr $ra` + delay slot (+ relleno) y sin `eret`
    if nxt - t > 0x4000:
        return False
    v = nxt - 4
    while v > t and word(v) == 0:
        v -= 4
    if word(v - 4) != 0x03E00008 and word(v) != 0x03E00008:  # delay slot no nulo, o nop tomado por relleno
        return False
    return all(word(a) != 0x42000018 for a in range(t, nxt, 4))

# 3. Codigo en huecos pequenos entre funciones que Ghidra no asigno a ninguna (p. ej. 0x1DB030: detras de un `j` de
#    cola + delay slot y solo alcanzable por puntero). El primer word no nulo del hueco es un inicio.
known = sorted(set(funcs) | set(added))
ends = {s: funcs[s][1] for s in funcs}
gaps = []
leaf_end = {}
for i, s in enumerate(known[:-1]):
    e = ends.get(s)
    if e is None:
        continue
    t = e
    while t < known[i + 1] and word(t) == 0:
        t += 4
    if t < known[i + 1] and known[i + 1] - t <= 0x400 and not inside(t):
        gaps.append(t)
    elif t < known[i + 1] and not inside(t) and plausible(t) and short_leaf(t):
        # Hueco grande (datos detras): solo una hoja corta pegada al final de la funcion anterior, p. ej. 0x260FE0
        # (ultima del texto, llamada por puntero desde 0x24D040). Termina en su `jr $ra` + delay slot.
        gaps.append(t)
        leaf_end[t] = short_leaf(t)
    elif t < known[i + 1] and not inside(t) and plausible(t) and code_gap(t, known[i + 1]):
        # Hueco grande que es todo codigo: termina en `jr $ra` + delay slot + relleno justo antes de la funcion
        # siguiente, p. ej. 0x232320 (metodo de una tabla, llamado por puntero desde 0x1918B8, 0x590 bytes) o 0x1E0510
        # (un `switch` de 0x4C0 bytes al que se llega por un puntero en 0x2E4DD4).
        gaps.append(t)
added = sorted(set(added) | set(gaps))


# 4. Dentro de los rangos recuperados: `jr $ra` + delay slot + relleno de nops => lo siguiente es otra funcion
#    (entry_00219830 de Ghidra cubre el final de una funcion, el relleno y la funcion 0x219840, llamada por puntero).
starts = sorted(set(funcs) | set(added))
split = []
for i, s0 in enumerate(starts):
    if s0 in funcs or s0 in leaf_end:
        continue
    e = starts[i + 1] if i + 1 < len(starts) else hi
    t = s0
    while t + 12 <= e:
        if word(t) == 0x03E00008 and word(t + 8) == 0:
            u = t + 8
            while u < e and word(u) == 0:
                u += 4
            if u < e and u % 8 == 0 and not inside(u):
                split.append(u)
            t = u
        else:
            t += 4
added = sorted(set(added) | set(split))

starts = sorted(set(funcs) | set(added))
nxt = {s: starts[i + 1] if i + 1 < len(starts) else hi for i, s in enumerate(starts)}
with open(dst, 'w') as out:
    out.write('Name,Start,End,Size\n')
    for s in starts:
        if s in funcs:
            name, end = funcs[s]
        else:
            name, end = f'FUN_{s:08x}', leaf_end.get(s, nxt[s])
        out.write(f'{name},0x{s:08X},0x{end:08X},{end - s}\n')
print(f'{len(rows)} filas -> {len(funcs)} funciones + {len(added)} inicios recuperados ({len(gaps)} de huecos, {len(split)} tras relleno) ({", ".join(f"{a:#x}" for a in added[:6])}...)')
