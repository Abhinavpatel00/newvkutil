#!/usr/bin/env bash
set -euo pipefail

OUTDIR="compiledshaders"
ERRLOG="$OUTDIR/shader_errors.txt"
SLANGC="/opt/shader-slang-bin/bin/slangc"

mkdir -p "$OUTDIR"
: > "$ERRLOG"

if [ ! -x "$SLANGC" ]; then
  echo "slangc not found at $SLANGC" >&2
  exit 1
fi

compile_stage() {
  local shader="$1"
  local entry="$2"
  local suffix="$3"

  local base
  base=$(basename "$shader" .slang)
  local out="$OUTDIR/${base}.${suffix}.spv"

  if [[ "$shader" -nt "$out" || ! -f "$out" ]]; then
    echo "Compiling Slang ($suffix): $shader [$entry]"

    "$SLANGC" "$shader" \
      -target spirv \
      -profile sm_6_6 \
      -entry "$entry" \
      -O3 \
      -fvk-use-entrypoint-name \
      -enable-experimental-rich-diagnostics \
      -o "$out" \
      2>>"$ERRLOG"
  fi
}

for shader in shaders/*.slang; do
  [[ -f "$shader" ]] || continue

  grep -q '\[shader("vertex")\]'   "$shader" && compile_stage "$shader" "vsMain"      "vert"
  grep -q '\[shader("fragment")\]' "$shader" && compile_stage "$shader" "psMain"      "frag"
  grep -q '\[shader("compute")\]'  "$shader" && compile_stage "$shader" "computeMain" "comp"
done

if [ -s "$ERRLOG" ]; then
  echo
  echo "⚠ Shader warnings / errors:"
  echo "---------------------------"
  cat "$ERRLOG"
fi
