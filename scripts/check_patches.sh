#!/bin/sh
# Comprueba que third_party/PS2Recomp es exactamente e563698 + patches/*.patch (en orden alfabetico): aplica la serie
# a un arbol limpio temporal y compara fichero a fichero con third_party. Sale con 1 si hay diferencias (codigo sin
# parchear, parches que no aplican o ficheros de mas). Uso: scripts/check_patches.sh
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)
TMP=$(mktemp -d)
trap 'git -C third_party/PS2Recomp worktree remove --force "$TMP/wt" >/dev/null 2>&1 || true; rm -rf "$TMP"' EXIT
git -C third_party/PS2Recomp worktree add -f "$TMP/wt" e563698 >/dev/null 2>&1
for p in $(ls "$ROOT"/patches/*.patch | sort); do
  git -C "$TMP/wt" apply "$p" || { echo "NO APLICA: $p"; exit 1; }
done
bad=0
for f in $(git -C "$TMP/wt" status --short | awk '{print $2}'); do
  cmp -s "$TMP/wt/$f" "third_party/PS2Recomp/$f" || { echo "DISTINTO: $f"; bad=1; }
done
for f in $(git -C third_party/PS2Recomp status --short | grep -v '^??.*build' | awk '{print $2}'); do
  [ -e "$TMP/wt/$f" ] || { echo "SOBRA en third_party (no esta en los parches): $f"; bad=1; }
done
[ $bad = 0 ] && echo "third_party = e563698 + patches/ (en orden): identico"
exit $bad
