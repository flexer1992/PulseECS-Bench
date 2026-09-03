#!/usr/bin/env python3
"""
Benchmark runner across entity scales for comparative ECS analysis.
Runs all 6 ECS implementations across multiple entity counts,
verifies equivalence via sink values, and saves structured results.
"""

import argparse
import csv
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parent.parent
BUILD_DIR = ROOT_DIR / "build" / "bench-release"
RESULTS_DIR = ROOT_DIR / "results"

BENCHMARKS = [
    ("ecs_bench_mine", "PulseECS"),
    ("ecs_bench_entt", "EnTT"),
    ("ecs_bench_flecs", "flecs"),
    ("ecs_bench_entityx", "EntityX"),
    ("ecs_bench_gaia", "gaia-ecs"),
    ("ecs_bench_pico", "pico_ecs"),
]

OPERATIONS = [
    ("add_components", "add components (Pos+Vel+50%Tag)", "add components"),
    ("each_packed", "each<Pos,Vel>", "each<Pos,Vel> avg"),
    ("each_sparse", "each<Pos,Vel,Tag>", "each<Pos,Vel,Tag>"),
    ("query_filter", "query<4> without<1>", "query<ReqA,B,C,D>"),
    ("destroy_entity", "destroyEntity", "destroyEntity"),
    ("get_component", "get<Pos>", "get<Pos>"),
    ("systems_packed", "7 systems mixed (full)", "7 systems mixed"),
    ("systems_frag", "frag 7sys mixed (alive)", "frag 7sys mixed"),
]

DEFAULT_SCALES = [
    (20_000, 5),
    (100_000, 5),
    (200_000, 5),
    (500_000, 5),
    (1_000_000, 3),
    (2_000_000, 3),
    (10_000_000, 1),
]


def parse_benchmark_output(output_text: str):
    results = {}
    sink = None

    for op_id, op_name, pattern in OPERATIONS:
        for line in output_text.splitlines():
            if pattern in line:
                m = re.search(r"\|\s*([0-9.]+)\s*ns/op", line)
                if m:
                    results[op_id] = float(m.group(1))
                break

    for line in output_text.splitlines():
        if line.startswith("sink="):
            sink = line.strip().split("=")[1]
            break

    return results, sink


def run_benchmark(binary_name: str, entities: int, iterations: int):
    bin_path = BUILD_DIR / binary_name
    if not bin_path.exists():
        raise FileNotFoundError(f"Binary not found: {bin_path}")

    cmd = [
        str(bin_path),
        "--entities", str(entities),
        "--iterations", str(iterations),
        "--seed", "1337",
    ]

    start_time = time.time()
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    duration = time.time() - start_time

    if proc.returncode != 0:
        print(f"Error running {binary_name}:\n{proc.stderr}", file=sys.stderr)
        raise RuntimeError(f"{binary_name} failed with return code {proc.returncode}")

    results, sink = parse_benchmark_output(proc.stdout)
    return results, sink, duration


def main():
    parser = argparse.ArgumentParser(description="Run ECS benchmark matrix")
    parser.add_argument("--scales", type=str, help="Comma-separated entity counts (e.g. 20000,100000)")
    parser.add_argument("--iterations", type=int, default=None, help="Override iterations for all scales")
    parser.add_argument("--quick", action="store_true", help="Quick run with 1 iteration per scale")
    args = parser.parse_args()

    if args.scales:
        scale_counts = [int(s.strip().replace("_", "")) for s in args.scales.split(",")]
        scales = []
        for s in scale_counts:
            iters = args.iterations or (1 if args.quick else (2 if s >= 10_000_000 else 3 if s >= 1_000_000 else 5))
            scales.append((s, iters))
    else:
        scales = []
        for s, iters in DEFAULT_SCALES:
            actual_iters = 1 if args.quick else (args.iterations if args.iterations else iters)
            scales.append((s, actual_iters))

    RESULTS_DIR.mkdir(parents=True, exist_ok=True)

    print("=" * 80)
    print("ECS COMPARATIVE BENCHMARK MATRIX RUNNER")
    print(f"Scales: {', '.join(f'{s:,} ({it} iter)' for s, it in scales)}")
    print(f"Benchmarks: {', '.join(label for _, label in BENCHMARKS)}")
    print("=" * 80)

    all_data = {
        "metadata": {
            "date": time.strftime("%Y-%m-%d %H:%M:%S"),
            "system": "Apple M3 Pro (macOS)",
            "compiler": "Apple Clang / Release -O3 -march=native",
        },
        "benchmarks": [b[1] for b in BENCHMARKS],
        "operations": [(op[0], op[1]) for op in OPERATIONS],
        "scales": [s[0] for s in scales],
        "results": {},  # str(scale) -> benchmark_label -> op_id -> ns_per_op
        "sinks": {},    # str(scale) -> sink_val
    }

    flat_records = []

    total_matrix_start = time.time()

    for entities, iters in scales:
        print(f"\n>>> Running scale: {entities:,} entities ({iters} iterations)...")
        scale_key = str(entities)
        all_data["results"][scale_key] = {}

        scale_sinks = {}

        for bin_name, label in BENCHMARKS:
            print(f"  [{label:10s}] running...", end="", flush=True)
            res, sink, dur = run_benchmark(bin_name, entities, iters)
            all_data["results"][scale_key][label] = res
            scale_sinks[label] = sink
            print(f" done ({dur:.2f}s, {len(res)} ops parsed)")

            for op_id, op_name, _ in OPERATIONS:
                val = res.get(op_id)
                flat_records.append({
                    "scale": entities,
                    "benchmark": label,
                    "operation_id": op_id,
                    "operation_name": op_name,
                    "ns_per_op": val,
                })

        # Verify sink consistency
        distinct_sinks = set(scale_sinks.values())
        if len(distinct_sinks) == 1:
            sink_val = next(iter(distinct_sinks))
            all_data["sinks"][scale_key] = sink_val
            print(f"  [SINK OK] All 6 benchmarks produced identical sink: {sink_val}")
        else:
            print(f"  [WARNING] Sink mismatch across benchmarks: {scale_sinks}")

        # Print brief table for this scale
        print("\n  Summary (ns/op, lower is faster):")
        hdr = f"  {'Operation':<32} | " + " | ".join(f"{b[1]:>9}" for b in BENCHMARKS) + " | Fastest"
        print("  " + "-" * (len(hdr) - 2))
        print(hdr)
        print("  " + "-" * (len(hdr) - 2))

        for op_id, op_name, _ in OPERATIONS:
            vals = [all_data["results"][scale_key][b[1]].get(op_id, float("inf")) for b in BENCHMARKS]
            min_v = min(vals) if vals else None
            row_items = []
            for b in BENCHMARKS:
                v = all_data["results"][scale_key][b[1]].get(op_id)
                if v is not None:
                    row_items.append(f"{v:8.2f} ")
                else:
                    row_items.append("      —  ")

            fastest_label = "—"
            for b, v in zip(BENCHMARKS, vals):
                if v == min_v and v != float("inf"):
                    fastest_label = b[1]
                    break

            print(f"  {op_name:<32} | " + " | ".join(row_items) + f" | {fastest_label}")

        print("  " + "-" * (len(hdr) - 2))

    total_matrix_time = time.time() - total_matrix_start
    print(f"\nAll runs completed in {total_matrix_time:.1f}s.")

    # Save JSON
    json_path = RESULTS_DIR / "benchmark_results.json"
    with open(json_path, "w", encoding="utf-8") as f:
        json.dump(all_data, f, indent=2, ensure_ascii=False)
    print(f"Saved JSON results to: {json_path}")

    # Save CSV
    csv_path = RESULTS_DIR / "benchmark_results.csv"
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=["scale", "benchmark", "operation_id", "operation_name", "ns_per_op"])
        writer.writeheader()
        writer.writerows(flat_records)
    print(f"Saved CSV results to: {csv_path}")


if __name__ == "__main__":
    main()
