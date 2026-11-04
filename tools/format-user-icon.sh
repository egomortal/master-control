#!/usr/bin/env bash
# Package a transparent 512x512 launcher PNG from an already prepared cutout.
set -euo pipefail
source_png=$1
output_png=$2
ffmpeg -y -loglevel error -i "$source_png" \
    -vf 'scale=472:472:flags=lanczos,pad=512:512:20:20:color=black@0' \
    -frames 1 "${output_png%.png}-rgba.png"
ffmpeg -y -loglevel error -i "${output_png%.png}-rgba.png" \
    -filter_complex '[0:v]split[a][b];[a]palettegen=reserve_transparent=1:transparency_color=000000[p];[b][p]paletteuse=dither=bayer:bayer_scale=3' \
    -frames 1 "$output_png"
rm "${output_png%.png}-rgba.png"
