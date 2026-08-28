#!/usr/bin/env bash
# Cross-checks the in-process libyuv PSNR/SSIM numbers against stock ffmpeg
# filters, using the frame-aligned Y4M pairs the quality tests write when
# DRISCORD_MEDIA_DUMP_DIR is set:
#
#   DRISCORD_MEDIA_DUMP_DIR=/tmp/driscord-dumps \
#       ctest --test-dir .builds/core-tests -R screen_quality
#   scripts/media_metrics_crosscheck.sh /tmp/driscord-dumps
#
# The *.ref.y4m/*.recv.y4m pairs contain exactly the frames the
# VideoQualityAccumulator compared, in comparison order, so ffmpeg's
# positional filters see aligned streams — the alignment ffmpeg cannot do by
# itself is already baked in by the frame markers. Per-frame logs land next
# to the pairs as *.psnr.log / *.ssim.log.
#
# Implementation notes for reading the numbers side by side:
#   - ffmpeg "average" PSNR and libyuv I420Psnr both divide the summed
#     squared error of all three planes by the total sample count; the test
#     additionally caps PSNR at 48 dB (kPsnrCapDb), so compare
#     min(ffmpeg, 48) against the gate value.
#   - SSIM implementations differ by design (window layout and plane
#     weighting), so expect a small systematic offset, not equality; the
#     per-plane Y value is the closest apples-to-apples comparison.
#
# WAV dumps (*.ref.wav / *.rendered.wav) get an EBU R128 loudness summary as
# a sanity check; MOS-grade audio scoring is ViSQOL's job (phase 7).
set -euo pipefail

DUMP_DIR="${1:-${DRISCORD_MEDIA_DUMP_DIR:-}}"
if [ -z "$DUMP_DIR" ]; then
    echo "usage: $0 <dump-dir>  (or set DRISCORD_MEDIA_DUMP_DIR)" >&2
    exit 2
fi
if [ ! -d "$DUMP_DIR" ]; then
    echo "ERROR: $DUMP_DIR is not a directory" >&2
    exit 2
fi
command -v ffmpeg >/dev/null 2>&1 || {
    echo "ERROR: ffmpeg not found in PATH" >&2
    exit 2
}

found=0
shopt -s nullglob

for ref in "$DUMP_DIR"/*.ref.y4m; do
    recv="${ref%.ref.y4m}.recv.y4m"
    label="$(basename "${ref%.ref.y4m}")"
    if [ ! -f "$recv" ]; then
        echo "WARN: $label has no .recv.y4m pair, skipping" >&2
        continue
    fi
    found=1
    echo "== video: $label"
    # The received stream is the distorted "main" input, the reference second.
    ffmpeg -hide_banner -nostats \
        -i "$recv" -i "$ref" \
        -lavfi "psnr=stats_file='$DUMP_DIR/$label.psnr.log'" \
        -f null - 2>&1 | grep 'Parsed_psnr' | sed 's/^.*] /  ffmpeg /'
    ffmpeg -hide_banner -nostats \
        -i "$recv" -i "$ref" \
        -lavfi "ssim=stats_file='$DUMP_DIR/$label.ssim.log'" \
        -f null - 2>&1 | grep 'Parsed_ssim' | sed 's/^.*] /  ffmpeg /'
done

for wav in "$DUMP_DIR"/*.wav; do
    found=1
    echo "== audio: $(basename "$wav")"
    ffmpeg -hide_banner -nostats -i "$wav" \
        -af ebur128=framelog=quiet -f null - 2>&1 \
        | sed -n '/Integrated loudness/,/LRA high/p' | sed 's/^ */  /'
done

if [ "$found" = 0 ]; then
    echo "ERROR: no *.ref.y4m/*.recv.y4m pairs or *.wav files in $DUMP_DIR" >&2
    echo "       run a quality test with DRISCORD_MEDIA_DUMP_DIR=$DUMP_DIR first" >&2
    exit 1
fi
