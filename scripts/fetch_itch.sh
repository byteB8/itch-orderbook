#!/usr/bin/env bash
# Downloads the start of one day of NASDAQ TotalView-ITCH 5.0 from NASDAQ's public sample files and
# decompresses it into data/itch/. A full day is several GB, so only the first MEGABYTES of the gzip
# file are fetched (an HTTP range request). The decompressed file therefore ends in the middle of a
# message; the tools stop at the last complete one.
#
#   scripts/fetch_itch.sh [DAY] [MEGABYTES]     (default 12302019 500)
#
# Source: https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/
set -euo pipefail
cd "$(dirname "$0")/.."

day=${1:-12302019}
megabytes=${2:-500}
url="https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/$day.NASDAQ_ITCH50.gz"
out="data/itch/$day.NASDAQ_ITCH50.first${megabytes}MB"

mkdir -p data/itch
curl -fsSL -r "0-$((megabytes * 1024 * 1024 - 1))" "$url" -o "$out.gz.part"
# gzip stops with "unexpected end of file" at the cut; everything before it is written out
gzip -dc "$out.gz.part" > "$out" 2>/dev/null || true
rm -f "$out.gz.part"
echo "$out: $(stat -c %s "$out") bytes"
