#!/usr/bin/env python3
"""Uso: make_skip.py game.toml errfuncs.txt out.toml. Anade a `skip` las funciones con error y cambia la salida a work/output2/."""
import sys
toml, errs, out = sys.argv[1:4]
names = [l.strip() for l in open(errs) if l.strip()]
s = open(toml).read()
assert 'skip = []' in s
s = s.replace('skip = []', 'skip = [\n' + ',\n'.join(f'  "{n}"' for n in names) + '\n]', 1)
s = s.replace('work/output/', 'work/output2/') if 'work/output2' not in s and 'output/' in s and '/ov/' not in s else s
open(out, 'w').write(s)
