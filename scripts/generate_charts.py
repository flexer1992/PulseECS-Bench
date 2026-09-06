#!/usr/bin/env python3
"""
Generate publication-quality charts for GitHub README & LinkedIn.
Reads benchmark data from results/benchmark_results.json and creates:
1. hero_benchmark_card.png - Headline summary comparing PulseECS with EnTT & Flecs
2. scaling_curves.png - Multi-panel scaling behavior from 20K to 10M entities
3. my_ecs_vs_entt_speedup.png - Speedup ratio of PulseECS vs EnTT across operations
4. archetype_vs_sparseset.png - Archetype vs Sparse-Set architecture tradeoff
5. fragmentation_penalty.png - Impact of memory fragmentation across scales
"""

import json
from pathlib import Path
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np

ROOT_DIR = Path(__file__).resolve().parent.parent
RESULTS_DIR = ROOT_DIR / "results"
ASSETS_DIR = ROOT_DIR / "assets"

# Colors tailored for high-contrast dark GitHub/LinkedIn aesthetic
PALETTE = {
    "bg_dark": "#0d1117",
    "bg_card": "#161b22",
    "border": "#30363d",
    "text_main": "#f0f6fc",
    "text_muted": "#8b949e",
    "grid": "#21262d",
    # ECS Library Colors
    "PulseECS": "#38bdf8",  # Electric cyan / sky blue
    "EnTT": "#f43f5e",  # Rose / crimson
    "flecs": "#10b981",  # Emerald green
    "gaia-ecs": "#f59e0b",  # Amber / orange
    "pico_ecs": "#a855f7",  # Purple
    "EntityX": "#64748b",  # Slate gray
}

STYLES = {
    "PulseECS": {
        "color": PALETTE["PulseECS"],
        "marker": "o",
        "linewidth": 2.8,
        "zorder": 10,
    },
    "EnTT": {"color": PALETTE["EnTT"], "marker": "s", "linewidth": 2.0, "zorder": 8},
    "flecs": {"color": PALETTE["flecs"], "marker": "^", "linewidth": 2.0, "zorder": 9},
    "gaia-ecs": {
        "color": PALETTE["gaia-ecs"],
        "marker": "v",
        "linewidth": 1.6,
        "zorder": 6,
    },
    "pico_ecs": {
        "color": PALETTE["pico_ecs"],
        "marker": "D",
        "linewidth": 1.6,
        "zorder": 7,
    },
    "EntityX": {
        "color": PALETTE["EntityX"],
        "marker": "x",
        "linewidth": 1.4,
        "zorder": 5,
        "linestyle": "--",
    },
}


def setup_dark_style():
    plt.rcParams.update(
        {
            "figure.facecolor": PALETTE["bg_dark"],
            "axes.facecolor": PALETTE["bg_card"],
            "axes.edgecolor": PALETTE["border"],
            "axes.labelcolor": PALETTE["text_main"],
            "text.color": PALETTE["text_main"],
            "xtick.color": PALETTE["text_muted"],
            "ytick.color": PALETTE["text_muted"],
            "grid.color": PALETTE["grid"],
            "grid.linestyle": ":",
            "grid.linewidth": 0.8,
            "grid.alpha": 0.9,
            "font.family": "sans-serif",
            "font.sans-serif": [
                "SF Pro Display",
                "Helvetica Neue",
                "Arial",
                "DejaVu Sans",
            ],
            "font.size": 11,
        }
    )


def load_data():
    json_path = RESULTS_DIR / "benchmark_results.json"
    with open(json_path, "r", encoding="utf-8") as f:
        return json.load(f)


def plot_scaling_curves(data):
    """Multi-panel scaling plot showing how operations scale from 20K to 10M entities."""
    scales = data["scales"]
    results = data["results"]
    benches = data["benchmarks"]

    # Key operations representing diverse workloads
    key_ops = [
        ("each_packed", "each<Pos, Vel> — Packed Iteration", "ns / entity"),
        ("get_component", "get<Pos> — Random Component Lookup", "ns / lookup"),
        ("destroy_entity", "destroyEntity — Entity Destruction", "ns / entity"),
        ("systems_frag", "frag 7 systems mixed — Fragmented World", "ns / hit"),
    ]

    fig, axes = plt.subplots(2, 2, figsize=(16, 11), dpi=300)
    fig.patch.set_facecolor(PALETTE["bg_dark"])

    fig.suptitle(
        "ECS Performance Scaling: 20K to 10,000,000 Entities (Apple M3 Pro)",
        fontsize=20,
        fontweight="bold",
        color=PALETTE["text_main"],
        y=0.98,
    )

    scale_labels = [
        f"{s // 1000}K" if s < 1_000_000 else f"{s // 1_000_000}M" for s in scales
    ]

    for ax, (op_id, title, ylabel) in zip(axes.flatten(), key_ops):
        ax.set_facecolor(PALETTE["bg_card"])
        ax.set_title(
            title, fontsize=14, fontweight="bold", pad=12, color=PALETTE["text_main"]
        )
        ax.set_xlabel(
            "Entity Count (log scale)", fontsize=11, color=PALETTE["text_muted"]
        )
        ax.set_ylabel(ylabel, fontsize=11, color=PALETTE["text_muted"])
        ax.grid(True)
        ax.set_xscale("log")

        for bench in benches:
            y_vals = []
            for s in scales:
                val = results[str(s)][bench].get(op_id, np.nan)
                y_vals.append(val)

            st = STYLES.get(bench, {})
            ax.plot(
                scales,
                y_vals,
                label=bench,
                color=st.get("color"),
                marker=st.get("marker"),
                linewidth=st.get("linewidth", 2),
                linestyle=st.get("linestyle", "-"),
                zorder=st.get("zorder", 5),
                markersize=6,
            )

        ax.set_xticks(scales)
        ax.set_xticklabels(scale_labels)

        # Highlight PulseECS advantage on get_component and destroy_entity
        if op_id == "get_component":
            my_10m = results[str(scales[-1])]["PulseECS"]["get_component"]
            entt_10m = results[str(scales[-1])]["EnTT"]["get_component"]
            ratio = entt_10m / my_10m
            ax.annotate(
                f"PulseECS: {my_10m:.1f} ns\nEnTT: {entt_10m:.1f} ns\n({ratio:.1f}x faster)",
                xy=(scales[-1], my_10m),
                xytext=(scales[-4] * 1.5, 62),
                arrowprops=dict(
                    facecolor=PALETTE["PulseECS"],
                    edgecolor=PALETTE["PulseECS"],
                    arrowstyle="->",
                    lw=1.5,
                ),
                bbox=dict(
                    boxstyle="round,pad=0.5",
                    fc=PALETTE["bg_dark"],
                    ec=PALETTE["PulseECS"],
                    lw=1.2,
                ),
                color=PALETTE["text_main"],
                fontsize=9.5,
                fontweight="semibold",
            )
        elif op_id == "destroy_entity":
            my_10m = results[str(scales[-1])]["PulseECS"]["destroy_entity"]
            entt_10m = results[str(scales[-1])]["EnTT"]["destroy_entity"]
            ratio = entt_10m / my_10m
            ax.annotate(
                f"PulseECS: {my_10m:.1f} ns\nEnTT: {entt_10m:.1f} ns\n({ratio:.1f}x faster)",
                xy=(scales[-1], my_10m),
                xytext=(scales[-4] * 0.8, 235),
                arrowprops=dict(
                    facecolor=PALETTE["PulseECS"],
                    edgecolor=PALETTE["PulseECS"],
                    arrowstyle="->",
                    lw=1.5,
                ),
                bbox=dict(
                    boxstyle="round,pad=0.5",
                    fc=PALETTE["bg_dark"],
                    ec=PALETTE["PulseECS"],
                    lw=1.2,
                ),
                color=PALETTE["text_main"],
                fontsize=9.5,
                fontweight="semibold",
            )

    # Place a single clean legend at the bottom
    handles, labels = axes[0, 0].get_legend_handles_labels()
    fig.legend(
        handles,
        labels,
        loc="lower center",
        ncol=6,
        frameon=True,
        facecolor=PALETTE["bg_card"],
        edgecolor=PALETTE["border"],
        fontsize=12,
        bbox_to_anchor=(0.5, 0.015),
    )

    plt.tight_layout(rect=[0, 0.06, 1, 0.95])
    out_path = ASSETS_DIR / "scaling_curves.png"
    plt.savefig(out_path, dpi=300, facecolor=fig.get_facecolor(), edgecolor="none")
    plt.close()
    print(f"Generated: {out_path}")


def plot_speedup_vs_entt(data):
    """Horizontal bar chart showing PulseECS speedup over EnTT at 1M and 10M entities."""
    results = data["results"]
    ops = data["operations"]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 7.5), dpi=300, sharey=True)
    fig.patch.set_facecolor(PALETTE["bg_dark"])

    fig.suptitle(
        "PulseECS vs EnTT Speedup Ratio (Values > 1.0x mean PulseECS is faster)",
        fontsize=18,
        fontweight="bold",
        color=PALETTE["text_main"],
        y=0.98,
    )

    for ax, scale, title in [
        (ax1, "1000000", "At 1,000,000 Entities"),
        (ax2, "10000000", "At 10,000,000 Entities (Stress)"),
    ]:
        ax.set_facecolor(PALETTE["bg_card"])
        ax.set_title(title, fontsize=14, fontweight="bold", pad=12)
        ax.grid(True, axis="x")

        op_labels = [name for _, name in ops]
        speedups = []
        for op_id, _ in ops:
            my_val = results[scale]["PulseECS"].get(op_id, 1.0)
            entt_val = results[scale]["EnTT"].get(op_id, 1.0)
            ratio = entt_val / my_val if my_val > 0 else 1.0
            speedups.append(ratio)

        y_pos = np.arange(len(op_labels))
        colors = [
            PALETTE["PulseECS"] if s >= 1.0 else PALETTE["EnTT"] for s in speedups
        ]

        bars = ax.barh(
            y_pos,
            speedups,
            color=colors,
            height=0.62,
            edgecolor=PALETTE["border"],
            lw=0.8,
        )

        # Baseline at 1.0x
        ax.axvline(1.0, color="#ffffff", linestyle="--", linewidth=1.5, alpha=0.7)

        for bar, ratio in zip(bars, speedups):
            w = bar.get_width()
            text = f"{ratio:.2f}x"
            if ratio >= 1.0:
                ax.text(
                    w + 0.10,
                    bar.get_y() + bar.get_height() / 2,
                    text,
                    va="center",
                    ha="left",
                    fontsize=10.5,
                    fontweight="bold",
                    color=PALETTE["PulseECS"],
                )
            else:
                # Place label neatly to avoid collision with baseline 1.0x
                ax.text(
                    w + 0.18,
                    bar.get_y() + bar.get_height() / 2,
                    text,
                    va="center",
                    ha="left",
                    fontsize=10.5,
                    fontweight="bold",
                    color=PALETTE["EnTT"],
                )

        ax.set_xlabel(
            "Speedup Factor (EnTT time / PulseECS time)",
            fontsize=11,
            color=PALETTE["text_muted"],
        )
        ax.set_xlim(0, max(speedups) * 1.18)

    ax1.set_yticks(np.arange(len(ops)))
    ax1.set_yticklabels([name for _, name in ops], fontsize=11)
    ax1.invert_yaxis()

    plt.tight_layout(rect=[0, 0.03, 1, 0.94])
    out_path = ASSETS_DIR / "pulse_ecs_vs_entt_speedup.png"
    plt.savefig(out_path, dpi=300, facecolor=fig.get_facecolor(), edgecolor="none")
    plt.savefig(
        ASSETS_DIR / "my_ecs_vs_entt_speedup.png",
        dpi=300,
        facecolor=fig.get_facecolor(),
        edgecolor="none",
    )
    plt.close()
    print(f"Generated: {out_path}")


def plot_archetype_vs_sparseset(data):
    """Bar chart highlighting Archetype (flecs) vs Sparse-Set (PulseECS, EnTT) architectural tradeoff."""
    results = data["results"]
    scale = "1000000"  # 1M entities

    categories = [
        ("Structure Mutate\n(add components)", "add_components", "Higher is worse"),
        ("Entity Deletion\n(destroyEntity)", "destroy_entity", "Higher is worse"),
        ("Direct Lookup\n(get<Pos>)", "get_component", "Higher is worse"),
        ("Query Packed\n(each<Pos,Vel>)", "each_packed", "Higher is worse"),
        ("Mixed Query\n(7 systems)", "systems_packed", "Higher is worse"),
    ]

    fig, ax = plt.subplots(figsize=(14, 7), dpi=300)
    fig.patch.set_facecolor(PALETTE["bg_dark"])
    ax.set_facecolor(PALETTE["bg_card"])

    ax.set_title(
        "Architectural Tradeoff at 1M Entities: Archetype (flecs) vs Sparse-Set (PulseECS, EnTT)",
        fontsize=16,
        fontweight="bold",
        pad=15,
        color=PALETTE["text_main"],
    )

    x = np.arange(len(categories))
    width = 0.25

    target_benches = [
        ("flecs", "flecs (Archetype)"),
        ("PulseECS", "PulseECS (Sparse-Set)"),
        ("EnTT", "EnTT (Sparse-Set)"),
    ]

    for idx, (bench_key, display_name) in enumerate(target_benches):
        vals = [results[scale][bench_key][op_id] for _, op_id, _ in categories]
        rects = ax.bar(
            x + (idx - 1) * width,
            vals,
            width,
            label=display_name,
            color=PALETTE[bench_key],
            edgecolor=PALETTE["border"],
            lw=0.8,
        )
        for rect in rects:
            h = rect.get_height()
            ax.text(
                rect.get_x() + rect.get_width() / 2,
                h + max(vals) * 0.02,
                f"{h:.1f}",
                ha="center",
                va="bottom",
                fontsize=9.5,
                color=PALETTE["text_main"],
                rotation=0,
            )

    ax.set_ylabel(
        "Execution Time (ns / op, lower is better)",
        fontsize=12,
        color=PALETTE["text_muted"],
    )
    ax.set_xticks(x)
    ax.set_xticklabels([c[0] for c in categories], fontsize=11)
    ax.grid(True, axis="y")
    ax.set_ylim(0, 160)

    ax.legend(
        loc="upper left",
        facecolor=PALETTE["bg_card"],
        edgecolor=PALETTE["border"],
        fontsize=11,
    )

    # Narrative callouts cleanly positioned above bars
    ax.annotate(
        "Sparse-Sets dominate\nstructural operations\n& random lookups",
        xy=(1.0, 60),
        xytext=(0.4, 115),
        arrowprops=dict(
            facecolor=PALETTE["PulseECS"],
            edgecolor=PALETTE["PulseECS"],
            arrowstyle="->",
            lw=1.5,
        ),
        bbox=dict(
            boxstyle="round,pad=0.5",
            fc=PALETTE["bg_dark"],
            ec=PALETTE["PulseECS"],
            lw=1.2,
        ),
        fontsize=10.5,
        fontweight="semibold",
        color=PALETTE["text_main"],
    )

    ax.annotate(
        "Archetype dominates\ndense system queries\n(contiguous chunks)",
        xy=(4 - width, 0.7),
        xytext=(3.2, 55),
        arrowprops=dict(
            facecolor=PALETTE["flecs"],
            edgecolor=PALETTE["flecs"],
            arrowstyle="->",
            lw=1.5,
        ),
        bbox=dict(
            boxstyle="round,pad=0.5", fc=PALETTE["bg_dark"], ec=PALETTE["flecs"], lw=1.2
        ),
        fontsize=10.5,
        fontweight="semibold",
        color=PALETTE["text_main"],
    )

    plt.tight_layout()
    out_path = ASSETS_DIR / "archetype_vs_sparseset.png"
    plt.savefig(out_path, dpi=300, facecolor=fig.get_facecolor(), edgecolor="none")
    plt.close()
    print(f"Generated: {out_path}")


def plot_fragmentation_impact(data):
    """Plot fragmentation penalty (frag 7 systems / packed 7 systems) across scales."""
    scales = data["scales"]
    results = data["results"]
    benches = data["benchmarks"]

    fig, ax = plt.subplots(figsize=(13, 6.5), dpi=300)
    fig.patch.set_facecolor(PALETTE["bg_dark"])
    ax.set_facecolor(PALETTE["bg_card"])

    ax.set_title(
        "Memory Fragmentation Penalty: Ratio of Frag World to Packed World Time",
        fontsize=16,
        fontweight="bold",
        pad=15,
        color=PALETTE["text_main"],
    )

    scale_labels = [
        f"{s // 1000}K" if s < 1_000_000 else f"{s // 1_000_000}M" for s in scales
    ]

    for bench in benches:
        ratios = []
        for s in scales:
            frag = results[str(s)][bench].get("systems_frag", 0)
            packed = results[str(s)][bench].get("systems_packed", 1)
            ratios.append(frag / packed if packed > 0 else 1.0)

        st = STYLES.get(bench, {})
        ax.plot(
            scales,
            ratios,
            label=bench,
            color=st.get("color"),
            marker=st.get("marker"),
            linewidth=st.get("linewidth", 2),
            zorder=st.get("zorder", 5),
            markersize=7,
        )

    ax.set_xscale("log")
    ax.set_xticks(scales)
    ax.set_xticklabels(scale_labels)
    ax.set_xlabel("Entity Count (log scale)", fontsize=12, color=PALETTE["text_muted"])
    ax.set_ylabel(
        "Penalty Ratio (Frag time / Packed time)",
        fontsize=12,
        color=PALETTE["text_muted"],
    )
    ax.grid(True)
    ax.axhline(
        1.0,
        color="#ffffff",
        linestyle="--",
        linewidth=1.2,
        alpha=0.5,
        label="No Penalty (1.0x)",
    )

    ax.legend(
        loc="upper left",
        facecolor=PALETTE["bg_card"],
        edgecolor=PALETTE["border"],
        fontsize=11,
    )

    plt.tight_layout()
    out_path = ASSETS_DIR / "fragmentation_impact.png"
    plt.savefig(out_path, dpi=300, facecolor=fig.get_facecolor(), edgecolor="none")
    plt.close()
    print(f"Generated: {out_path}")


def plot_owning_groups(data):
    """Owning Groups: fragmented iteration via group vs each vs view, plus scaling vs EnTT's group."""
    scales = data["scales"]
    results = data["results"]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 7), dpi=300)
    fig.patch.set_facecolor(PALETTE["bg_dark"])

    fig.suptitle(
        "Owning Groups: Fragmented-World Iteration Stays Packed Without Manual Defragmentation",
        fontsize=17,
        fontweight="bold",
        color=PALETTE["text_main"],
        y=0.99,
    )

    # ---- Left panel: group<Pos,Vel> scaling, PulseECS vs EnTT vs flecs ----
    group_series = [
        ("PulseECS", "group_frag"),
        ("EnTT", "group_frag"),
        ("flecs", "each_packed"),  # archetype: always grouped by design
    ]
    scale_labels = [
        f"{s // 1000}K" if s < 1_000_000 else f"{s // 1_000_000}M" for s in scales
    ]

    for bench, op in group_series:
        vals = []
        for s in scales:
            v = results[str(s)][bench].get(op)
            vals.append(v if v is not None else np.nan)
        st = STYLES.get(bench, {})
        ax1.plot(
            scales,
            vals,
            label=f"{bench}"
            + (" (archetype each)" if bench == "flecs" else " group<Pos,Vel>"),
            color=st.get("color"),
            marker=st.get("marker"),
            linewidth=st.get("linewidth", 2),
            zorder=st.get("zorder", 5),
            markersize=7,
        )

    ax1.set_xscale("log")
    ax1.set_yscale("log")
    ax1.set_xticks(scales)
    ax1.set_xticklabels(scale_labels)
    ax1.set_xlabel("Entity Count (log scale)", fontsize=12, color=PALETTE["text_muted"])
    ax1.set_ylabel("ns / entity (log scale)", fontsize=12, color=PALETTE["text_muted"])
    ax1.set_title(
        "group<Pos,Vel> Iteration After 30% Delete + 20% Re-add", fontsize=13, pad=10
    )
    ax1.grid(True, which="both")
    ax1.legend(
        loc="upper left",
        facecolor=PALETTE["bg_card"],
        edgecolor=PALETTE["border"],
        fontsize=11,
    )

    # ---- Right panel: 10M fragmented world — plain iteration vs opt-in group ----
    # Только реально измеренные значения из JSON: systems_frag (голая итерация
    # в фрагментированном мире) и group_frag (та же структура мира, итерация
    # через owning-группу). flecs — archetype: его обычная итерация уже сгруппирована.
    scale_10m = results["10000000"]
    bars = [
        (
            "PulseECS\nplain each<> (frag 7 sys)",
            scale_10m["PulseECS"]["systems_frag"],
            PALETTE["PulseECS"],
        ),
        (
            "PulseECS\ngroup<Pos,Vel>",
            scale_10m["PulseECS"]["group_frag"],
            "#22d3ee",
        ),
        (
            "EnTT\nplain view<> (frag 7 sys)",
            scale_10m["EnTT"]["systems_frag"],
            PALETTE["EnTT"],
        ),
        (
            "EnTT\ngroup<Pos,Vel>",
            scale_10m["EnTT"]["group_frag"],
            "#fda4af",
        ),
        (
            "flecs\narchetype (frag 7 sys)",
            scale_10m["flecs"]["systems_frag"],
            PALETTE["flecs"],
        ),
        (
            "PulseECS\nfrag 7sys + group<Pos,Vel>",
            scale_10m["PulseECS"]["group_frag_7sys"],
            "#0ea5e9",
        ),
        (
            "EnTT\nfrag 7sys + group<Pos,Vel>",
            scale_10m["EnTT"]["group_frag_7sys"],
            "#f472b6",
        ),
    ]

    names = [b[0] for b in bars]
    vals = [b[1] for b in bars]
    cols = [b[2] for b in bars]

    bar_plot = ax2.bar(
        names, vals, color=cols, edgecolor=PALETTE["border"], lw=0.8, width=0.62
    )
    ax2.set_yscale("log")
    ax2.set_ylabel("ns / entity (log scale)", fontsize=12, color=PALETTE["text_muted"])
    ax2.set_title(
        "Fragmented World at 10,000,000 Entities: Plain Iteration vs Owning Group",
        fontsize=13,
        pad=10,
    )
    ax2.grid(True, which="both", axis="y")
    ax2.tick_params(axis="x", labelsize=9.5)
    plt.setp(ax2.get_xticklabels(), rotation=12, ha="right")

    for rect, v in zip(bar_plot, vals):
        ax2.text(
            rect.get_x() + rect.get_width() / 2,
            rect.get_height() * 1.12,
            f"{v:.2f}",
            ha="center",
            va="bottom",
            fontsize=10.5,
            fontweight="bold",
            color=PALETTE["text_main"],
        )

    plt.tight_layout(rect=[0, 0, 1, 0.95])
    out_path = ASSETS_DIR / "owning_groups.png"
    plt.savefig(out_path, dpi=300, facecolor=fig.get_facecolor(), edgecolor="none")
    plt.close()
    print(f"Generated: {out_path}")


def plot_hero_card(data):
    """Headline infographic card designed for the LinkedIn post & GitHub README header."""
    results = data["results"]
    scale_10m = "10000000"
    scale_1m = "1000000"

    fig = plt.figure(figsize=(15, 8.5), dpi=300)
    fig.patch.set_facecolor(PALETTE["bg_dark"])

    # Header section
    fig.text(
        0.08,
        0.92,
        "PULSE-ECS BENCHMARK: 10,000,000 ENTITIES STRESS TEST",
        fontsize=20,
        fontweight="bold",
        color=PALETTE["PulseECS"],
    )
    fig.text(
        0.08,
        0.87,
        "Workload & Architecture Comparison on Apple M3 Pro | Release -O3 -march=native | Validated via sink",
        fontsize=12,
        color=PALETTE["text_muted"],
    )

    # 3 Stat Cards on top dynamically computed
    my_get = results[scale_10m]["PulseECS"]["get_component"]
    entt_get = results[scale_10m]["EnTT"]["get_component"]
    get_speedup = entt_get / my_get

    my_dest = results[scale_10m]["PulseECS"]["destroy_entity"]
    entt_dest = results[scale_10m]["EnTT"]["destroy_entity"]
    dest_speedup = entt_dest / my_dest

    my_frag = results[scale_10m]["PulseECS"]["systems_frag"]
    entt_frag = results[scale_10m]["EnTT"]["systems_frag"]
    frag_speedup = entt_frag / my_frag

    stat_boxes = [
        (
            "Direct Component Access",
            "get<Pos>",
            f"{my_get:.2f} ns",
            f"{get_speedup:.1f}x",
            f"faster than EnTT ({entt_get:.1f} ns)",
        ),
        (
            "Entity Destruction",
            "destroyEntity",
            f"{my_dest:.1f} ns",
            f"{dest_speedup:.1f}x",
            f"faster than EnTT ({entt_dest:.1f} ns)",
        ),
        (
            "Fragmented Iteration",
            "frag 7 systems",
            f"{my_frag:.1f} ns",
            f"{frag_speedup:.1f}x",
            f"faster than EnTT ({entt_frag:.1f} ns)",
        ),
    ]

    card_coords = [
        (0.08, 0.65, 0.26, 0.17),
        (0.37, 0.65, 0.26, 0.17),
        (0.66, 0.65, 0.26, 0.17),
    ]

    for (title, metric, val, badge, comp), (x, y, w, h) in zip(stat_boxes, card_coords):
        card_ax = fig.add_axes([x, y, w, h], facecolor=PALETTE["bg_card"])
        card_ax.spines[:].set_color(PALETTE["border"])
        card_ax.spines[:].set_linewidth(1.2)
        card_ax.set_xticks([])
        card_ax.set_yticks([])

        card_ax.text(
            0.06,
            0.82,
            title.upper(),
            fontsize=10,
            fontweight="bold",
            color=PALETTE["text_muted"],
        )
        card_ax.text(
            0.06, 0.44, val, fontsize=22, fontweight="heavy", color=PALETTE["text_main"]
        )
        card_ax.text(
            0.55,
            0.44,
            f"[{badge}]",
            fontsize=15,
            fontweight="bold",
            color=PALETTE["PulseECS"],
        )
        card_ax.text(0.06, 0.14, comp, fontsize=9.5, color=PALETTE["text_muted"])

    # Bottom comparison chart: 10M entities head-to-head across all engines
    ax_bottom = fig.add_axes([0.08, 0.12, 0.84, 0.46], facecolor=PALETTE["bg_card"])
    ax_bottom.spines[:].set_color(PALETTE["border"])

    ops_to_show = [
        ("each<Pos,Vel>", "each_packed"),
        ("each<Pos,Vel,Tag>", "each_sparse"),
        ("query<4> no<1>", "query_filter"),
        ("get<Pos>", "get_component"),
        ("7 sys mixed", "systems_packed"),
        ("frag 7 sys", "systems_frag"),
        ("destroyEntity", "destroy_entity"),
    ]

    benches = ["PulseECS", "EnTT", "flecs", "gaia-ecs", "pico_ecs"]
    x = np.arange(len(ops_to_show))
    width = 0.15

    for i, bench in enumerate(benches):
        vals = [results[scale_10m][bench][op_id] for _, op_id in ops_to_show]
        ax_bottom.bar(
            x + (i - 2) * width,
            vals,
            width,
            label=bench,
            color=PALETTE[bench],
            edgecolor=PALETTE["border"],
            lw=0.6,
        )

    ax_bottom.set_ylabel(
        "Execution Time (ns / op, log scale)", fontsize=11, color=PALETTE["text_muted"]
    )
    ax_bottom.set_yscale("log")
    ax_bottom.set_xticks(x)
    ax_bottom.set_xticklabels(
        [lbl for lbl, _ in ops_to_show], fontsize=11, fontweight="medium"
    )
    ax_bottom.grid(True, which="both", axis="y")
    ax_bottom.legend(
        loc="upper right",
        ncol=5,
        facecolor=PALETTE["bg_card"],
        edgecolor=PALETTE["border"],
        fontsize=10.5,
    )
    ax_bottom.set_title(
        "Performance across Operations at 10,000,000 Entities (Lower is Better)",
        fontsize=13,
        pad=10,
    )

    out_path = ASSETS_DIR / "hero_benchmark_card.png"
    plt.savefig(out_path, dpi=300, facecolor=fig.get_facecolor(), edgecolor="none")
    plt.close()
    print(f"Generated: {out_path}")


def plot_social_preview(data):
    """
    Generate exact 1280x640px GitHub Social Media Preview (Open Graph card).
    Optimized for GitHub repository preview and LinkedIn link unfurls.
    """
    results = data["results"]
    scale_10m = "10000000"

    my_get = results[scale_10m]["PulseECS"]["get_component"]
    entt_get = results[scale_10m]["EnTT"]["get_component"]
    get_speedup = entt_get / my_get

    my_dest = results[scale_10m]["PulseECS"]["destroy_entity"]
    entt_dest = results[scale_10m]["EnTT"]["destroy_entity"]
    dest_speedup = entt_dest / my_dest

    my_add = results[scale_10m]["PulseECS"]["add_components"]
    entt_add = results[scale_10m]["EnTT"]["add_components"]

    flecs_query = results[scale_10m]["flecs"]["query_filter"]

    fig = plt.figure(figsize=(12.8, 6.4), dpi=100)
    fig.patch.set_facecolor(PALETTE["bg_dark"])

    # Outer decorative frame
    frame = fig.add_axes([0.02, 0.04, 0.96, 0.92], facecolor=PALETTE["bg_card"])
    frame.spines[:].set_color(PALETTE["border"])
    frame.spines[:].set_linewidth(1.5)
    frame.set_xticks([])
    frame.set_yticks([])

    # Left Section: Brand & Headline
    frame.text(
        0.05,
        0.86,
        "C++20 ENGINE CORE & ARCHITECTURE BENCHMARK",
        fontsize=11,
        fontweight="bold",
        color=PALETTE["text_muted"],
    )
    frame.text(
        0.05,
        0.70,
        "PulseECS",
        fontsize=40,
        fontweight="heavy",
        color=PALETTE["PulseECS"],
    )
    frame.text(
        0.05,
        0.58,
        "Benchmarking 6 ECS Frameworks up to 10M Entities",
        fontsize=16,
        fontweight="bold",
        color=PALETTE["text_main"],
    )
    frame.text(
        0.05,
        0.49,
        "PulseECS  •  EnTT  •  flecs  •  gaia-ecs  •  pico_ecs  •  EntityX",
        fontsize=12,
        color=PALETTE["text_muted"],
    )

    # Bullet takeaways
    takeaways = [
        (
            "• Pure POD structs & zero-sized tags via [[no_unique_address]]",
            PALETTE["PulseECS"],
        ),
        (
            "• On-demand pool defragmentation recovers iteration to 1.77 ns/op",
            PALETTE["text_muted"],
        ),
        (
            "• Empirical study on Apple M3 Pro with verified sink accumulation",
            PALETTE["text_muted"],
        ),
    ]
    for idx, (text, col) in enumerate(takeaways):
        frame.text(0.05, 0.38 - idx * 0.07, text, fontsize=11, color=col)

    # Badges row at bottom left
    badges = ["C++20", "Release -O3", "Sparse-Set", "Pure POD", "MIT License"]
    badge_x = 0.05
    for b in badges:
        frame.text(
            badge_x,
            0.10,
            f"[{b}]",
            fontsize=10.5,
            fontweight="bold",
            color=PALETTE["PulseECS"],
        )
        badge_x += 0.11

    # Right Section: 3 High-Impact Cards
    cards_data = [
        (
            "RANDOM LOOKUP (get<Pos>)",
            f"{my_get:.2f} ns",
            f"{get_speedup:.1f}x vs EnTT ({entt_get:.1f} ns)",
            PALETTE["PulseECS"],
        ),
        (
            "STRUCTURAL MUTATION (add)",
            f"{my_add:.2f} ns",
            f"vs EnTT ({entt_add:.1f} ns) & flecs (75 ns)",
            PALETTE["PulseECS"],
        ),
        (
            "DENSE ARCHETYPE QUERY",
            f"{flecs_query:.2f} ns",
            "flecs chunked storage",
            PALETTE["flecs"],
        ),
    ]

    card_y = 0.65
    for title, metric, sub, accent in cards_data:
        box = fig.add_axes([0.60, card_y, 0.35, 0.22], facecolor=PALETTE["bg_dark"])
        box.spines[:].set_color(PALETTE["border"])
        box.spines[:].set_linewidth(1.0)
        box.set_xticks([])
        box.set_yticks([])

        box.text(
            0.08,
            0.76,
            title,
            fontsize=9.5,
            fontweight="bold",
            color=PALETTE["text_muted"],
        )
        box.text(0.08, 0.38, metric, fontsize=22, fontweight="heavy", color=accent)
        box.text(0.08, 0.12, sub, fontsize=10, color=PALETTE["text_muted"])
        card_y -= 0.27

    out_path = ASSETS_DIR / "github_social_preview.png"
    plt.savefig(out_path, dpi=100, facecolor=fig.get_facecolor(), edgecolor="none")
    plt.close()
    print(f"Generated: {out_path} (exact 1280x640 px)")


def main():
    ASSETS_DIR.mkdir(parents=True, exist_ok=True)
    setup_dark_style()
    data = load_data()

    print("Generating benchmark visualization assets...")
    plot_scaling_curves(data)
    plot_speedup_vs_entt(data)
    plot_archetype_vs_sparseset(data)
    plot_fragmentation_impact(data)
    plot_owning_groups(data)
    plot_hero_card(data)
    plot_social_preview(data)
    print("All charts generated successfully in assets/!")


if __name__ == "__main__":
    main()
