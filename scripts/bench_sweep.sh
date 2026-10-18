#!/usr/bin/env bash
# Runs inference_bench over every model in bench/models for the given
# providers and call periods, and writes one CSV row per run. Usage:
#   scripts/bench_sweep.sh <build_dir> <out.csv> <provider>...
set -euo pipefail

BUILD="$1"; OUT="$2"; shift 2
PROVIDERS=("$@")
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PERIODS=(0 1000 20000)

echo "width,provider,period_us,n,mean_us,p50_us,p99_us,max_us" > "$OUT"
for model in $(ls "$REPO"/bench/models/mlp_w*.onnx | sort -t w -k3 -n); do
    width=$(basename "$model" .onnx | sed 's/mlp_w//')
    for provider in "${PROVIDERS[@]}"; do
        for period in "${PERIODS[@]}"; do
            iters=2000; [[ "$period" -eq 20000 ]] && iters=300
            line=$("$BUILD/cpp_core/inference_bench" --model "$model" --provider "$provider" \
                     --period-us "$period" --iters "$iters" --warmup 50 | grep '^inference')
            n=$(echo "$line" | grep -o 'n=[0-9]*' | cut -d= -f2)
            vals=$(echo "$line" | grep -oE '(mean|p50|p99|max)= *[0-9.]+' | sed 's/.*= *//' | paste -sd, -)
            echo "$width,$provider,$period,$n,$vals" | tee -a "$OUT"
        done
    done
done
