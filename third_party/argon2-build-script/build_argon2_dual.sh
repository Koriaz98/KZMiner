#!/bin/bash
# Compile libargon2 deux fois (variante portable et variante AVX2), et
# renomme tous les symboles de la variante AVX2 avec le prefixe "avx2_"
# pour permettre aux deux d'etre liees dans le meme binaire final.
# Usage: build_argon2_dual.sh <source_dir> <output_dir>
set -e

SRC_DIR="$1"
OUT_DIR="$2"
BUILD_DIR="${OUT_DIR}/build_tmp"

mkdir -p "$BUILD_DIR"
rm -rf "${BUILD_DIR:?}"/*
cp -r "$SRC_DIR"/* "$BUILD_DIR/"
cd "$BUILD_DIR"

# --- Variante portable (aucune extension SIMD requise) ---
make clean >/dev/null 2>&1 || true
make OPTTARGET=none -j"$(nproc)"
cp libargon2.a "${OUT_DIR}/libargon2_ref.a"

# --- Variante AVX2/FMA (x86-64-v3) ---
make clean >/dev/null 2>&1 || true
make OPTTARGET=x86-64-v3 -j"$(nproc)"

mkdir -p avx2_objs
cd avx2_objs
ar x ../libargon2.a

nm --defined-only -g ./*.o | awk '$2 ~ /^[A-Z]$/ {print $3}' | sort -u \
    | awk '{print $1, "avx2_"$1}' > ../rename_map.txt

for f in *.o; do
    objcopy --redefine-syms=../rename_map.txt "$f" "renamed_$f"
done

ar rcs "${OUT_DIR}/libargon2_avx2_renamed.a" renamed_*.o
cd ..

cp include/argon2.h "${OUT_DIR}/"

rm -rf "$BUILD_DIR"
echo "OK: libargon2_ref.a et libargon2_avx2_renamed.a generees dans ${OUT_DIR}"
