#!/usr/bin/env bash
# Runs every benchmark on this machine and writes the results to results/<name>/.
#
#   scripts/run_all.sh <name> [itch file]
#
# CPU choices (defaults suit the laptop, where CPUs 0-3 are cores 0-3 and 4-7 their SMT siblings):
#   BOOK_CPU   the core the book benchmark runs on                     (2)
#   PAIRS      CPU pairs for the queue benchmark, "a,b,label ..."      ("2,3,separate-cores 2,6,smt-siblings")
#   PUB_CPU RX_CPU CORE_CPU SMT_CPU   the pipeline set-ups, see run_pipeline.sh
set -euo pipefail
cd "$(dirname "$0")/.."

name=${1:?usage: run_all.sh <name> [itch file]}
file=${2:-data/itch/12302019.NASDAQ_ITCH50.first500MB}
out=results/$name
mkdir -p "$out" results/raw

{
  echo "cpu: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //')"
  echo "logical cpus: $(nproc)"
  echo "compiler: $(${CXX:-g++} --version | head -1)"
  echo "kernel: $(uname -r)"
  echo "transparent huge pages: $(cat /sys/kernel/mm/transparent_hugepage/enabled)"
  echo "net.core.rmem_max: $(cat /proc/sys/net/core/rmem_max)"
  echo "data: $file"
} > "$out/environment.txt"

./build/bench_book "$file" "$out" --rounds 5 --cpu "${BOOK_CPU:-2}" | tee results/raw/bench_book_$name.log
rm -f "$out/queue.csv"
pairs=()
for p in ${PAIRS:-2,3,separate-cores 2,6,smt-siblings}; do pairs+=(--pair "$p"); done
./build/bench_queue "$out" "${pairs[@]}" | tee results/raw/bench_queue_$name.log
OUT=$out scripts/run_pipeline.sh "$file"
