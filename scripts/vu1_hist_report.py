#!/usr/bin/env python3
"""Resume la salida '[vu1hist]' de BLADE_VU1_HIST=1 BLADE_VU1_DIFF=<grabacion> (histograma dinamico de pares del VU1 por clase).
Uso: vu1_hist_report.py salida1.txt [salida2.txt ...]   (suma todos los ficheros dados)"""
import re
import sys
from collections import Counter

PIPE = ["ninguna/NOP", "FMAC", "LSU (load/store)", "FDIV/SQRT", "EFU", "IALU", "BRANCH", "XGKICK"]

UPPER_NAMES = {}
for i in range(4):
    UPPER_NAMES[i] = "ADDbc"
    UPPER_NAMES[4 + i] = "SUBbc"
    UPPER_NAMES[8 + i] = "MADDbc"
    UPPER_NAMES[12 + i] = "MSUBbc"
    UPPER_NAMES[16 + i] = "MAXbc"
    UPPER_NAMES[20 + i] = "MINIbc"
    UPPER_NAMES[24 + i] = "MULbc"
UPPER_NAMES.update({0x1C: "MULq", 0x1D: "MAXi", 0x1E: "MULi", 0x1F: "MINIi", 0x20: "ADDq", 0x21: "MADDq", 0x22: "ADDi", 0x23: "MADDi",
                    0x24: "SUBq", 0x25: "MSUBq", 0x26: "SUBi", 0x27: "MSUBi", 0x28: "ADD", 0x29: "MADD", 0x2A: "MUL", 0x2B: "MAX",
                    0x2C: "SUB", 0x2D: "MSUB", 0x2E: "OPMSUB", 0x2F: "MINI", 0x30: "IADD?", 0x3C: "special"})
# subcodigos (funct 0x3c-0x3f): clave 64 + (fd<<2 | funct&3), fd = (w>>6)&31
SPEC = {0: "ADDAbc", 1: "SUBAbc", 2: "MADDAbc", 3: "MSUBAbc", 4: "ITOF", 5: "FTOI", 6: "MULAbc"}
SPEC_Q = {7: ["MULAq", "ABS", "MULAi", "CLIP"], 8: ["ADDAq", "MADDAq", "ADDAi", "MADDAi"], 9: ["SUBAq", "MSUBAq", "SUBAi", "MSUBAi"],
          10: ["ADDA", "MADDA", "MULA", "UPS_0a3"], 11: ["SUBA", "MSUBA", "OPMULA", "NOP"]}
LOWER_OP = {0: "LQ", 1: "SQ", 4: "ILW", 5: "ISW", 8: "IADDIU", 9: "ISUBIU", 0x10: "FCEQ", 0x11: "FCSET", 0x12: "FCAND", 0x13: "FCOR",
            0x14: "FSEQ", 0x15: "FSSET", 0x16: "FSAND", 0x17: "FSOR", 0x18: "FMEQ", 0x1A: "FMAND", 0x1B: "FMOR", 0x1C: "FCGET",
            0x20: "B", 0x21: "BAL", 0x24: "JR", 0x25: "JALR", 0x28: "IBEQ", 0x29: "IBNE", 0x2C: "IBLTZ", 0x2D: "IBGTZ", 0x2E: "IBLEZ", 0x2F: "IBGEZ"}
LOWER_F = {0x30: "IADD", 0x31: "ISUB", 0x32: "IADDI", 0x34: "IAND", 0x35: "IOR"}
LOWER_SP = {0x30: "MOVE", 0x31: "MR32", 0x34: "LQI", 0x35: "SQI", 0x36: "LQD", 0x37: "SQD", 0x38: "DIV", 0x39: "SQRT", 0x3A: "RSQRT", 0x3B: "WAITQ",
            0x3C: "MTIR", 0x3D: "MFIR", 0x3E: "ILWR", 0x3F: "ISWR", 0x40: "RNEXT", 0x41: "RGET", 0x42: "RINIT", 0x43: "RXOR", 0x64: "MFP",
            0x68: "XTOP", 0x69: "XITOP", 0x6C: "XGKICK", 0x70: "ESADD", 0x71: "ERSADD", 0x72: "ELENG", 0x73: "ERLENG", 0x74: "EATANxy",
            0x75: "EATANxz", 0x76: "ESUM", 0x78: "ESQRT", 0x79: "ERSQRT", 0x7A: "ERCPR", 0x7B: "WAITP", 0x7C: "ESIN", 0x7D: "EATAN", 0x7E: "EEXP"}


def upper_name(k):
    if k < 64:
        return UPPER_NAMES.get(k, "UP_%02x" % k)
    fd, f2 = (k - 64) >> 2, (k - 64) & 3
    if fd in SPEC_Q:
        return SPEC_Q[fd][f2]
    return SPEC.get(fd, "UPS_%02x" % (k - 64))


def lower_name(k):
    if k < 128:
        return LOWER_OP.get(k, "LOP_%02x" % k)
    if k < 192:
        return LOWER_F.get(k - 128, "LF_%02x" % (k - 128))
    return LOWER_SP.get(k - 192, "LSP_%02x" % (k - 192))


def cls_upper(k):
    n = upper_name(k)
    if n == "NOP":
        return "NOP"
    return "FMAC"


def cls_lower(k):
    n = lower_name(k)
    if n in ("DIV", "SQRT", "RSQRT", "WAITQ"):
        return "FDIV/SQRT"
    if n.startswith("E") and n not in ("ESADD",) or n in ("ESADD", "ERSADD", "ELENG", "ERLENG", "WAITP"):
        return "EFU"
    if n in ("LQ", "SQ", "ILW", "ISW", "LQI", "SQI", "LQD", "SQD", "ILWR", "ISWR"):
        return "load/store"
    if n in ("B", "BAL", "JR", "JALR") or n.startswith("IB"):
        return "branch"
    if n in ("IADD", "ISUB", "IADDI", "IAND", "IOR", "IADDIU", "ISUBIU", "MTIR", "MFIR", "FCEQ", "FCSET", "FCAND", "FCOR", "FSEQ", "FSSET",
             "FSAND", "FSOR", "FMEQ", "FMAND", "FMOR", "FCGET"):
        return "IALU/flags"
    if n in ("MOVE", "MR32"):
        return "MOVE/MR32 (FMAC-like)"
    if n == "XGKICK":
        return "XGKICK"
    return "otros"


def main():
    pairs = upnop = lonop = loi = 0
    pipeU = Counter()
    pipeL = Counter()
    U = Counter()
    L = Counter()
    for fn in sys.argv[1:]:
        for ln in open(fn, errors="replace"):
            m = re.match(r"\[vu1hist\] pares=(\d+) upperNop=(\d+) lowerNop=(\d+) loi=(\d+)", ln)
            if m:
                a, b, c, d = map(int, m.groups())
                pairs += a
                upnop += b
                lonop += c
                loi += d
                continue
            m = re.match(r"\[vu1hist\] pipe (\d+) upper=(\d+) lower=(\d+)", ln)
            if m:
                pipeU[int(m.group(1))] += int(m.group(2))
                pipeL[int(m.group(1))] += int(m.group(3))
                continue
            m = re.match(r"\[vu1hist\] ([UL]) (\d+) (\d+)", ln)
            if m:
                (U if m.group(1) == "U" else L)[int(m.group(2))] += int(m.group(3))
    if not pairs:
        print("sin datos")
        return
    print("pares ejecutados: %d  (superior NOP %.1f %%, inferior NOP %.1f %%, pares con LOI %.1f %%)" % (pairs, 100 * upnop / pairs, 100 * lonop / pairs, 100 * loi / pairs))
    cu = Counter()
    for k, v in U.items():
        cu[cls_upper(k)] += v
    print("\nMitad superior (FMAC) por clase:")
    for k, v in cu.most_common():
        print("  %-24s %12d  %5.1f %%" % (k, v, 100 * v / pairs))
    print("\nMitad inferior por clase:")
    cl = Counter()
    for k, v in L.items():
        cl[cls_lower(k)] += v
    cl["LOI (sin inferior)"] += loi
    for k, v in cl.most_common():
        print("  %-24s %12d  %5.1f %%" % (k, v, 100 * v / pairs))
    print("\nClase de tuberia segun el decodificador (superior / inferior):")
    for i in range(8):
        if pipeU[i] or pipeL[i]:
            print("  %-18s %12d %12d" % (PIPE[i], pipeU[i], pipeL[i]))
    un = Counter()
    for k, v in U.items():
        un[upper_name(k)] += v
    print("\nTop 15 opcodes superiores (todas las variantes de componente/bc juntas):")
    for k, v in un.most_common(15):
        print("  %-10s %12d  %5.1f %%" % (k, v, 100 * v / pairs))
    ln = Counter()
    for k, v in L.items():
        ln[lower_name(k)] += v
    print("\nTop 20 opcodes inferiores:")
    for k, v in ln.most_common(20):
        print("  %-10s %12d  %5.1f %%" % (k, v, 100 * v / pairs))


if __name__ == "__main__":
    main()
