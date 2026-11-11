#!/usr/bin/env python3
"""Genera third_party/PS2Recomp/ps2xIOP/tests/spu2_adpcm_ref_vectors.h: bloques ADPCM pseudoaleatorios y su salida
decodificada por el decodificador adpcm_psx de FFmpeg (oraculo independiente; requiere ffmpeg en el PATH)."""
import random, struct, subprocess, sys, tempfile, os
out = sys.argv[1]
random.seed(12345)
blocks = []
for f in range(5):
    for sh in (8, 10, 11, 12):
        b = bytearray(random.randrange(256) for _ in range(16)); b[0] = (f << 4) | sh; b[1] = 0; blocks.append(bytes(b))
# Antes de cada bloque de prueba va un bloque de ceros con filtro 0: deja s1 = s2 = 0, asi cada bloque se compara
# por separado y su error de redondeo no se acumula de un bloque al siguiente.
zero = bytes(16)
data = b''.join(zero + b for b in blocks)
hdr = b'VAGp' + struct.pack('>IIII', 0x20, 0, len(data), 44100) + bytes(12) + b'test'.ljust(16, b'\0')
with tempfile.TemporaryDirectory() as t:
    open(os.path.join(t, 't.vag'), 'wb').write(hdr + data)
    subprocess.run(['ffmpeg', '-y', '-v', 'error', '-i', os.path.join(t, 't.vag'), '-f', 's16le', '-acodec', 'pcm_s16le', os.path.join(t, 't.raw')], check=True)
    pcm = struct.unpack('<%dh' % (len(blocks) * 56), open(os.path.join(t, 't.raw'), 'rb').read())[0:]
    pcm = [v for k in range(len(blocks)) for v in pcm[k * 56 + 28:k * 56 + 56]]
def arr(vals, w, fmt):
    rows = []
    for i in range(0, len(vals), w):
        rows.append('    ' + ', '.join(fmt % v for v in vals[i:i + w]) + ',')
    return '\n'.join(rows)
open(out, 'w').write("""#pragma once
// Vectores de referencia INDEPENDIENTES del decodificador ADPCM del SPU2: 20 bloques (filtros 0..4 x desplazamientos
// 8,10,11,12; datos pseudoaleatorios, semilla 12345) y su salida segun el decodificador "adpcm_psx" de FFmpeg
// (implementacion independiente, usada solo como oraculo; no se copio codigo). FFmpeg NO es bit-exacto con el
// hardware (guarda mas precision), por eso el test compara con tolerancia. Generado con scripts/gen_adpcm_vectors.py.
#include <cstdint>

static const uint8_t kRefAdpcmBlocks[20 * 16] = {
%s
};
static const int16_t kRefAdpcmPcm[20 * 28] = {
%s
};
""" % (arr(list(b''.join(blocks)), 16, '0x%02x'), arr(list(pcm), 14, '%d')))
