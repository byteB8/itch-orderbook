#!/usr/bin/env bash
# Runs the publisher and the feed handler on this host at several message rates, in three set-ups:
#   threads1      the receive thread updates the books itself (CPU 2)
#   threads2-core receive thread on CPU 2, book thread on CPU 3: separate physical cores
#   threads2-smt  receive thread on CPU 2, book thread on CPU 6: two hardware threads of one core
# The publisher runs on CPU 1. The CPU numbers suit the laptop (CPUs 0-3 are cores 0-3, 4-7 their SMT
# siblings); override with PUB_CPU, RX_CPU, CORE_CPU and SMT_CPU elsewhere. Appends one row per run to
# $OUT/pipeline.csv (default results/) and writes each run's latency histogram to results/raw/.
#
#   scripts/run_pipeline.sh [itch file] [messages per run]
set -euo pipefail
cd "$(dirname "$0")/.."

file=${1:-data/itch/12302019.NASDAQ_ITCH50.first500MB}
limit=${2:-5000000}
pub_cpu=${PUB_CPU:-1}
rx_cpu=${RX_CPU:-2}
core_cpu=${CORE_CPU:-3}
smt_cpu=${SMT_CPU:-6}
out=${OUT:-results}/pipeline.csv
mkdir -p results/raw
rm -f "$out"

for rate in 100000 250000 500000 1000000 2000000; do
  for setup in "threads1 1 $rx_cpu" "threads2-core 2 $core_cpu" "threads2-smt 2 $smt_cpu"; do
    read -r name threads book_cpu <<< "$setup"
    label="${name}_rate${rate}"
    ./build/feed_handler --threads "$threads" --rx-cpu "$rx_cpu" --book-cpu "$book_cpu" --label "$label" \
      --result "$out" --hist "results/raw/latency_$label.csv" > "results/raw/handler_$label.txt" &
    handler=$!
    sleep 1  # the handler joins the group before the first packet
    ./build/publisher "$file" --rate "$rate" --limit "$limit" --cpu "$pub_cpu" > "results/raw/publisher_$label.txt"
    wait "$handler"
    tail -n 1 "$out"
  done
done
