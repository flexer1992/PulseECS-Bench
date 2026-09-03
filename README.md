# PulseECS — Building & Benchmarking a C++20 ECS at 10 Million Entities

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Compiler](https://img.shields.io/badge/compiler-Apple%20Clang-lightgrey.svg)](https://clang.llvm.org/)
[![Hardware](https://img.shields.io/badge/tested%20on-Apple%20M3%20Pro%20(36GB)-orange.svg)]()
[![Max Scale](https://img.shields.io/badge/max%20entities-10%2C000%2C000-purple.svg)]()
[![Workload Verified](https://img.shields.io/badge/sink%20check-100%25%20matched-success.svg)]()
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

An empirical performance study and benchmark harness comparing **PulseECS** (a custom C++20 Sparse-Set engine core) against five established industry implementations: **EnTT v3.13**, **flecs v4.1.1**, **EntityX 1.3.0**, **gaia-ecs v0.8.10**, and **pico_ecs**, scaled from **20,000 to 10,000,000 entities**.

---

![PulseECS Benchmark Hero](assets/hero_benchmark_card.png)

---

## 🎯 Why I Built This

When building a custom 2D game engine, I needed to manage high-volume dynamic workloads (projectiles, particle cascades, spatial entities). As scene complexity grew toward hundreds of thousands of active entities, profiling revealed cache line thrashing and instruction overhead in component lookups.

Instead of guessing what optimizations actually matter, I built **PulseECS** around cache-conscious Sparse-Set storage, and then built a reproducible benchmark harness to test it against industry standards under identical conditions up to **10 million entities**.

The goal was not to claim a universally "fastest" ECS, but to answer a concrete engineering question:

> **What actually happens to ECS data structures when memory exceeds CPU cache limits and spills into main RAM at 10M entities?**

---

## ⚖️ The Core Takeaway: No Universally "Fastest" ECS

Different ECS data structures dominate different workloads:

| Workload Category | Representative Operation | PulseECS *(Sparse-Set)* | EnTT *(Sparse-Set)* | flecs *(Archetype)* | pico_ecs *(Flat C API)* |
| :--- | :--- | :---: | :---: | :---: | :---: |
| **Point Lookup** | `get<Pos>` | 🟢 **Fast** (`7.8 ns`) | 🔴 Slower (`41.8 ns`) | 🔴 Slower (`82.3 ns`) | 🟢 **Fast** (`5.7 ns`) |
| **Packed Iteration** | `each<Pos, Vel>` | 🟢 **Fast** (`0.65 ns`) | 🟡 Moderate (`1.66 ns`) | 🟢 **Fast** (`0.52 ns`) | 🟢 **Fast** (`0.90 ns`) |
| **Structural Mutation** | `add components` | 🟡 Moderate (`15.7 ns`) | 🟢 **Fast** (`11.1 ns`) | 🔴 Heavy (`74.7 ns`) | 🟢 **Fast** (`5.2 ns`) |
| **Complex Queries** | `query<4> without<1>` | 🟡 Moderate (`4.44 ns`) | 🟡 Moderate (`5.18 ns`) | 🟢 **Blazing** (`0.18 ns`) | 🟢 **Fast** (`1.47 ns`) |
| **Entity Destruction** | `destroyEntity` | 🟢 **Fast** (`76.0 ns`) | 🔴 Slower (`165.5 ns`) | 🔴 Slower (`160.6 ns`) | 🟢 **Fast** (`30.7 ns`) |
| **Fragmented World** | `frag 7 systems` | 🟡 Degrades (`12.0 ns`) | 🟡 Degrades (`24.0 ns`) | 🟢 **Immune** (`0.48 ns`) | 🔴 Degrades (`18.6 ns`) |

*Measured at 10,000,000 entities on Apple M3 Pro (Release -O3 -march=native). Nanoseconds per operation, lower is faster.*

---

## 🧪 Benchmark Integrity

To prevent the common pitfalls of synthetic microbenchmarks, all tests adhere to strict fairness constraints:

* **Identical Workloads:** Every library instantiates identical component structs with identical memory footprints.
* **Identical Component Distributions:** Tag is attached to 50% of entities (`i & 1`), Health to 25% (`i & 3`), Armor to 12.5% (`i & 7`), etc.
* **Deterministic RNG & Shuffle:** Exactly four `std::shuffle` calls with seed `1337` are performed in identical order across all benchmarks so fragmented scenarios delete the exact same entities.
* **Cross-Checked Output Accumulation (`sink`):** Results of arithmetic transformations are accumulated into a 64-bit `sink` variable. Across all 6 implementations at 10M entities, every benchmark produced:
  $$\text{sink} = 100\,000\,061\,314\,363$$
  This confirms that every framework performed equivalent computational work.
* **Single-Threaded:** No background worker threads or job systems were used, isolating core data structure performance.

---

## 📊 Visual Analysis & Findings

### 1. Scaling Curves (20K → 10,000,000 Entities)

![Scaling Curves](assets/scaling_curves.png)

* **Cache Cliff:** At 20K–100K entities, working sets fit largely within CPU L2/L3 caches, keeping operations under 1–3 ns. Once entity counts cross 500K–1M entities, working sets exceed cache capacity (M3 Pro has 36MB L2), and uncached DRAM fetches drive random access times upward.
* **Packed Iteration Resilience:** `each<Pos, Vel>` remains under 1 ns/op even at 10M entities for contiguous array layouts (PulseECS: `0.65 ns`, flecs: `0.52 ns`) thanks to hardware stream prefetchers.

---

### 2. Archetype vs. Sparse-Set Tradeoff

![Archetype vs Sparse-Set](assets/archetype_vs_sparseset.png)

* **Archetype Engines (`flecs`, `gaia-ecs`):** Group entities with identical component sets into contiguous chunked tables. Complex multi-component queries run at `0.18–0.50 ns/op` with zero indirection. However, adding/removing components or deleting entities requires copying component data between archetype tables, resulting in `75–160 ns/op`.
* **Sparse-Set Engines (`PulseECS`, `EnTT`):** Store each component type in independent dense/sparse arrays. Adding components (`11–15 ns`) and point lookups (`7–8 ns`) are fast, making Sparse-Sets well-suited for dynamic gameplay with frequent component attachments.

---

### 3. Head-to-Head: PulseECS vs. EnTT

![PulseECS vs EnTT Speedup](assets/pulse_ecs_vs_entt_speedup.png)

At 10M entities, PulseECS demonstrates lower latency across 7 of 8 measured scenarios:

* **`get<Pos>` (7.77 ns vs 41.78 ns):** The sparse index uses `uint32_t` rather than `size_t`, cutting index table memory footprint in half and improving cache/TLB efficiency during random lookups. Additionally, splitting `getContainer` into a slim fast-path (`containerPtr`) avoids instruction cache pollution.
* **`destroyEntity` (76.03 ns vs 165.53 ns):** A 64-bit component mask allows `destroyEntity()` to check active components in a single bitwise operation, skipping component pools that were never attached to that entity.
* **`frag 7 systems` (11.98 ns vs 23.98 ns):** Storing `owner` directly inside the component struct keeps the owning entity ID in the same cache line as component data, avoiding a secondary traversal into `denseToEntity_`.

---

### 4. Memory Fragmentation Impact

![Fragmentation Impact](assets/fragmentation_impact.png)

In Sparse-Sets, deleting entities via swap-and-pop disrupts the sequential memory order of dense arrays.
* In a fresh ("packed") world, entities are sequential, and multi-component queries enjoy near-linear memory access.
* In a fragmented world (after 30% deletions and 20% re-insertions), iterating secondary components turns into pseudo-random memory access, incurring a **2.0× to 3.5× latency penalty** at 10M entities.
* Archetype engines (`flecs`) maintain grouping internally and remain practically immune to this effect (`0.48 ns/op`).

---

## ⚡ Quick Start & Core Usage

PulseECS is header-only and requires a C++20 compiler:

```cpp
#include "engine/ecs/World.hpp"

// 1. Components require an owner field as the first member (managed by SparseSet)
struct Position { engine::EntityId owner{engine::InvalidEntity}; float x{0.f}, y{0.f}; };
struct Velocity { engine::EntityId owner{engine::InvalidEntity}; float vx{1.f}, vy{2.f}; };

// 2. Initialize World and create entities
engine::World world;
auto entity = world.createEntity();
world.addComponent<Position>(entity, Position{ .x = 10.f, .y = 20.f });
world.addComponent<Velocity>(entity, Velocity{ .vx = 1.f, .vy = 0.5f });

// 3. Fast packed iteration over systems (O(1) contiguous dense array)
world.each<Position, Velocity>([](Position &pos, const Velocity &vel) {
    pos.x += vel.vx;
    pos.y += vel.vy;
});

// 4. Point lookup or entity destruction
auto *pos = world.getComponent<Position>(entity);
world.destroyEntity(entity);
```

---

## 📈 Detailed Benchmark Data Tables

<details>
<summary><b>Click to expand: 10,000,000 Entities (Full Stress Test)</b></summary>

| Operation | PulseECS | EnTT | flecs | EntityX | gaia-ecs | pico_ecs | Fastest |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **add components (Pos+Vel+50%Tag)** | 15.73 ns | 11.08 ns | 74.72 ns | 12.26 ns | 45.94 ns | **5.24 ns** | pico_ecs |
| **each\<Pos,Vel\>** | 0.65 ns | 1.66 ns | **0.52 ns** | 4.02 ns | 0.75 ns | 0.90 ns | flecs |
| **each\<Pos,Vel,Tag\>** | 1.42 ns | 4.71 ns | **0.26 ns** | 5.84 ns | 1.33 ns | 1.31 ns | flecs |
| **query\<4\> without\<1\>** | 4.44 ns | 5.18 ns | **0.18 ns** | 9.72 ns | 0.86 ns | 1.47 ns | flecs |
| **destroyEntity** | 76.03 ns | 165.53 ns | 160.59 ns | 71.65 ns | 318.26 ns | **30.73 ns** | pico_ecs |
| **get\<Pos\>** | 7.77 ns | 41.78 ns | 82.30 ns | 18.32 ns | 35.08 ns | **5.68 ns** | pico_ecs |
| **7 systems mixed (full)** | 2.38 ns | 6.65 ns | **0.63 ns** | 11.79 ns | 2.96 ns | 2.59 ns | flecs |
| **frag 7sys mixed (alive)** | 11.98 ns | 23.98 ns | **0.48 ns** | 14.98 ns | 2.86 ns | 18.65 ns | flecs |

*Validated with `sink=100000061314363` across all 6 engines.*
</details>

<details>
<summary><b>Click to expand: 1,000,000 Entities (High Scale)</b></summary>

| Operation | PulseECS | EnTT | flecs | EntityX | gaia-ecs | pico_ecs | Fastest |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **add components (Pos+Vel+50%Tag)** | 12.37 ns | 10.87 ns | 74.91 ns | 12.48 ns | 46.65 ns | **5.29 ns** | pico_ecs |
| **each\<Pos,Vel\>** | 0.64 ns | 1.49 ns | **0.38 ns** | 4.10 ns | 0.76 ns | 0.80 ns | flecs |
| **each\<Pos,Vel,Tag\>** | 1.23 ns | 4.11 ns | **0.50 ns** | 5.93 ns | 1.26 ns | 1.45 ns | flecs |
| **query\<4\> without\<1\>** | 2.99 ns | 5.35 ns | **0.19 ns** | 9.60 ns | 0.79 ns | 1.44 ns | flecs |
| **destroyEntity** | 56.20 ns | 140.82 ns | 133.24 ns | 27.68 ns | 214.61 ns | **10.73 ns** | pico_ecs |
| **get\<Pos\>** | 4.36 ns | 23.09 ns | 49.36 ns | 10.80 ns | 23.61 ns | **1.58 ns** | pico_ecs |
| **7 systems mixed (full)** | 2.28 ns | 6.35 ns | **0.66 ns** | 11.93 ns | 2.32 ns | 2.50 ns | flecs |
| **frag 7sys mixed (alive)** | 6.10 ns | 13.86 ns | **0.54 ns** | 14.68 ns | 2.92 ns | 8.99 ns | flecs |

*Validated with `sink=3000018393552` across all 6 engines.*
</details>

<details>
<summary><b>Click to expand: 200,000 Entities (Mid Scale Baseline)</b></summary>

| Operation | PulseECS | EnTT | flecs | EntityX | gaia-ecs | pico_ecs | Fastest |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **add components (Pos+Vel+50%Tag)** | 11.21 ns | 10.47 ns | 86.46 ns | 12.19 ns | 62.03 ns | **5.23 ns** | pico_ecs |
| **each\<Pos,Vel\>** | 1.12 ns | 1.22 ns | **0.24 ns** | 3.98 ns | 0.41 ns | 0.73 ns | flecs |
| **each\<Pos,Vel,Tag\>** | 1.18 ns | 3.14 ns | **0.31 ns** | 5.60 ns | 1.36 ns | 1.27 ns | flecs |
| **query\<4\> without\<1\>** | 3.08 ns | 4.57 ns | **0.19 ns** | 9.55 ns | 0.64 ns | 1.48 ns | flecs |
| **destroyEntity** | 21.92 ns | 56.13 ns | 150.88 ns | 29.75 ns | 134.66 ns | **8.86 ns** | pico_ecs |
| **get\<Pos\>** | 1.43 ns | 8.97 ns | 18.46 ns | 2.59 ns | 12.37 ns | **1.04 ns** | pico_ecs |
| **7 systems mixed (full)** | 1.82 ns | 4.89 ns | **0.62 ns** | 11.43 ns | 0.97 ns | 2.26 ns | flecs |
| **frag 7sys mixed (alive)** | 2.29 ns | 6.86 ns | **0.52 ns** | 26.14 ns | 1.00 ns | 3.34 ns | flecs |

*Validated with `sink=200006127435` across all 6 engines.*
</details>

<details>
<summary><b>Click to expand: 20,000 Entities (Game-sized World)</b></summary>

| Operation | PulseECS | EnTT | flecs | EntityX | gaia-ecs | pico_ecs | Fastest |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **add components (Pos+Vel+50%Tag)** | 17.22 ns | 25.87 ns | 95.55 ns | 16.75 ns | 68.97 ns | **5.21 ns** | pico_ecs |
| **each\<Pos,Vel\>** | 1.66 ns | 1.07 ns | **0.33 ns** | 3.99 ns | 0.38 ns | 1.16 ns | flecs |
| **each\<Pos,Vel,Tag\>** | 1.19 ns | 2.48 ns | **0.46 ns** | 5.74 ns | 0.50 ns | 1.25 ns | flecs |
| **query\<4\> without\<1\>** | 3.03 ns | 22.06 ns | **0.23 ns** | 10.02 ns | 0.39 ns | 1.41 ns | flecs |
| **destroyEntity** | 33.96 ns | 25.82 ns | 35.25 ns | 17.16 ns | 69.31 ns | **6.66 ns** | pico_ecs |
| **get\<Pos\>** | 0.94 ns | 3.88 ns | 43.33 ns | 2.66 ns | 8.57 ns | **0.66 ns** | pico_ecs |
| **7 systems mixed (full)** | 4.88 ns | 4.26 ns | 4.55 ns | 18.40 ns | **2.13 ns** | 2.21 ns | gaia-ecs |
| **frag 7sys mixed (alive)** | 5.71 ns | 3.91 ns | **1.29 ns** | 14.31 ns | 1.78 ns | 3.01 ns | flecs |

*Validated with `sink=2000614060` across all 6 engines.*
</details>

---

## 🏗️ Project Structure

```
include/engine/ecs/
├── Entity.hpp          # EntityId definition and generational handling
├── EntityManager.hpp   # Entity lifecycle, recycle queue, generational IDs
├── SparseSet.hpp       # Cache-aligned dense/sparse storage, O(1) swap-and-pop
└── World.hpp           # Core ECS World: createEntity, destroyEntity,
                        # addComponent, getComponent, variadic each<> & query<>

bench/
├── CMakeLists.txt      # Self-contained build; pulls 3rd-party libs via FetchContent
├── ecs_bench_mine.cpp  # PulseECS benchmark scenario
├── ecs_bench_entt.cpp  # EnTT v3.13.0 mirror
├── ecs_bench_flecs.cpp # flecs v4.1.1 mirror
├── ecs_bench_gaia.cpp  # gaia-ecs v0.8.10 mirror
├── ecs_bench_entityx.cpp # EntityX 1.3.0 mirror
├── ecs_bench_pico.cpp  # pico_ecs mirror
└── compare_ecs.sh      # Bash comparison runner with sink validation

scripts/
├── run_benchmarks.py   # Automated matrix runner across all entity scales
└── generate_charts.py  # Publication chart generator (Matplotlib / Seaborn)

assets/                 # Generated 300-DPI charts & LinkedIn hero graphics
results/                # Raw JSON and CSV data matrices
```

---

## ⚠️ Limitations & Caveats

1. **Synthetic Microbenchmarks ≠ Game Frame Rate:** In a real game engine, ECS logic typically consumes 1–5% of the frame budget; rendering, physics broadphase, animation, and audio dominate the remainder.
2. **Workload Specificity:** A title with 95% iteration and rare structural mutations will benefit heavily from archetype storage (like flecs). A title with high-frequency particle spawning and dynamic component attachment benefits from Sparse-Sets.
3. **Single-Threaded Context:** All benchmarks execute on a single core. Multi-threaded archetype chunking or parallel sparse-set schedules were intentionally excluded.
4. **Library Capabilities:** Flecs supports entity hierarchies, relationships, and queries that PulseECS does not attempt to implement. These microbenchmarks evaluate fundamental component storage patterns only.

---

## 🚀 How to Reproduce

### 1. Requirements
* **CMake 3.20+**
* **C++20 compliant compiler** (Apple Clang 15+, GCC 12+, MSVC 19.30+)
* External libraries are automatically fetched by CMake on the first build.

### 2. Build & Run
```bash
# 1. Configure and build in Release mode
cmake -S bench -B build/bench-release -DCMAKE_BUILD_TYPE=Release
cmake --build build/bench-release -j

# 2. Run the quick comparison table (200K entities)
./bench/compare_ecs.sh

# 3. Run the full matrix (20K -> 10M) and save JSON/CSV
python3 scripts/run_benchmarks.py

# 4. Generate all charts in assets/
uv run --with matplotlib,numpy,pandas python3 scripts/generate_charts.py
```

---

## 💻 Test Machine Specifications
* **Hardware:** MacBook Pro (Apple Silicon)
* **Processor:** Apple M3 Pro (12-core: 6 performance + 6 efficiency)
* **Memory:** 36 GB Unified LPDDR5
* **OS:** macOS Darwin (arm64)
* **Compiler:** Apple Clang version 16.0.0 (`-O3 -march=native -std=c++20`)

---

## 📜 License

This project is licensed under the [MIT License](LICENSE) — feel free to use, modify, and integrate it into your personal or commercial game engines.
