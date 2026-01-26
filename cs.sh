#!/bin/bash
# compile_shaders.sh

mkdir -p compiledshaders

for shader in shaders/*.vert shaders/*.comp shaders/*.frag; do
  if [ ! -f "$shader" ]; then
    continue
  fi
  
  filename=$(basename "$shader")
  output="compiledshaders/$filename.spv"
  
  # Check if source is newer than output, or output doesn't exist
  if [ "$shader" -nt "$output" ] || [ ! -f "$output" ]; then
    echo "Compiling $shader..."
    glslc --target-env=vulkan1.3 "$shader" -o "$output"
  fi
done
