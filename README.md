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
| **Point Lookup** | `get<Pos>` | 🟢 **Fast** (`7.45 ns`) | 🔴 Slower (`40.61 ns`) | 🔴 Slower (`81.35 ns`) | 🟢 **Fast** (`5.72 ns`) |
| **Packed Iteration** | `each<Pos, Vel>` | 🟢 **Fast** (`0.70 ns`) | 🟡 Moderate (`1.81 ns`) | 🟢 **Fast** (`0.53 ns`) | 🟢 **Fast** (`1.08 ns`) |
| **Structural Mutation** | `add components` | 🟢 **Fast** (`9.29 ns`) 🏆 | 🟢 **Fast** (`11.19 ns`) | 🔴 Heavy (`77.60 ns`) | 🟢 **Fast** (`5.40 ns`) |
| **Complex Queries** | `query<4> without<1>` | 🟢 **Fast** (`3.88 ns`) | 🟡 Moderate (`5.35 ns`) | 🟢 **Blazing** (`0.18 ns`) | 🟢 **Fast** (`1.45 ns`) |
| **Entity Destruction** | `destroyEntity` | 🟢 **Fast** (`71.13 ns`) | 🔴 Slower (`161.94 ns`) | 🔴 Slower (`170.25 ns`) | 🟢 **Fast** (`33.46 ns`) |
| **Fragmented World** | `frag 7 systems` | 🟡 Normal (`11.11 ns`)<br>🟢 **Post-Defrag (`1.77 ns`)** | 🟡 Degrades (`24.56 ns`) | 🟢 **Immune** (`0.50 ns`) | 🔴 Degrades (`18.69 ns`) |

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

At 10M entities, PulseECS demonstrates lower latency across **all 8 measured scenarios** compared to EnTT:

* **`add components` (9.29 ns vs 11.19 ns):** Geometric capacity scaling in `sparse_`, `EntityManager`, and `compMask_` eliminates tens of millions of redundant vector resize checks during massive entity creation.
* **`get<Pos>` (7.45 ns vs 40.61 ns):** The sparse index uses `uint32_t` rather than `size_t`, cutting index table memory footprint in half and improving cache/TLB efficiency during random lookups. Additionally, splitting `getContainer` into a slim fast-path (`containerPtr`) avoids instruction cache pollution.
* **`destroyEntity` (71.13 ns vs 161.94 ns):** A 64-bit component mask allows `destroyEntity()` to check active components in a single bitwise operation, skipping component pools that were never attached to that entity.
* **Pure POD & Zero-Sized Tag Architecture (`[[no_unique_address]]`):**
  - **Clean POD components:** User structs are standard layout (`struct Position { float x, y; };`), wrapped internally in a contiguous `Slot { EntityId owner; T data; }` so that `owner` and component data share the same cache line.
  - **Zero-Sized Tags:** Empty structs (where `std::is_empty_v<T>` is true) consume **zero bytes** of memory for instances. The `Slot` size collapses to exactly 4 bytes (`sizeof(EntityId)`), functioning as a pure entity ID vector.
  - **Instant Bitmask Filtering:** `hasComponent<T>()` and `query().exclude<T>()` resolve against the 64-bit component bitmask in a single CPU cycle instead of traversing sparse maps.
* **Defragmentation via `world.defragment()`:** Optional on-demand sorting of all dense arrays restores linear memory access after massive swap-and-pop deletions, reducing fragmented iteration latency from **10.48 ns** down to **1.77 ns/op** (a 6.3× speedup).

---

### 4. Memory Fragmentation Impact

![Fragmentation Impact](assets/fragmentation_impact.png)

In Sparse-Sets, deleting entities via swap-and-pop disrupts the sequential memory order of dense arrays.
* In a fresh ("packed") world, entities are sequential, and multi-component queries enjoy near-linear memory access.
* In a fragmented world (after 30% deletions and 20% re-insertions), iterating secondary components turns into pseudo-random memory access, incurring a **2.0× to 3.5× latency penalty** at 10M entities.
* Archetype engines (`flecs`) maintain grouping internally and remain practically immune to this effect (`0.49 ns/op`).

---

## ⚡ Quick Start & Core Usage

PulseECS is header-only and requires a C++20 compiler. Components are pure POD structs:

```cpp
#include "engine/ecs/World.hpp"

// 1. Components are 100% clean POD structs (no base classes, no injected metadata)
struct Position { float x{0.f}, y{0.f}; };
struct Velocity { float vx{1.f}, vy{2.f}; };

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
| **add components (Pos+Vel+50%Tag)** | 9.29 ns | 11.19 ns | 77.60 ns | 12.47 ns | 47.63 ns | **5.40 ns** | pico_ecs |
| **each<Pos,Vel>** | 0.70 ns | 1.81 ns | **0.53 ns** | 4.07 ns | 0.74 ns | 1.08 ns | flecs |
| **each<Pos,Vel,Tag>** | 1.15 ns | 4.55 ns | **0.28 ns** | 5.98 ns | 1.30 ns | 1.32 ns | flecs |
| **query<4> without<1>** | 3.88 ns | 5.35 ns | **0.18 ns** | 10.07 ns | 0.87 ns | 1.45 ns | flecs |
| **destroyEntity** | 71.13 ns | 161.94 ns | 170.25 ns | 70.58 ns | 337.48 ns | **33.46 ns** | pico_ecs |
| **get<Pos>** | 7.45 ns | 40.61 ns | 81.35 ns | 17.73 ns | 35.45 ns | **5.72 ns** | pico_ecs |
| **7 systems mixed (full)** | 1.99 ns | 6.69 ns | **0.66 ns** | 12.24 ns | 3.05 ns | 2.75 ns | flecs |
| **frag 7sys mixed (alive)** | 11.11 ns | 24.56 ns | **0.50 ns** | 15.20 ns | 2.90 ns | 18.69 ns | flecs |
| **post-defrag 7sys mixed** | **1.77 ns** 🚀 | — | — | — | — | — | PulseECS |

*Validated with `sink=100000061314363` across all 6 engines.*
</details>

<details>
<summary><b>Click to expand: 1,000,000 Entities (High Scale)</b></summary>

| Operation | PulseECS | EnTT | flecs | EntityX | gaia-ecs | pico_ecs | Fastest |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **add components (Pos+Vel+50%Tag)** | 6.43 ns | 10.17 ns | 74.93 ns | 12.15 ns | 47.19 ns | **5.15 ns** | pico_ecs |
| **each<Pos,Vel>** | 0.61 ns | 1.98 ns | **0.21 ns** | 3.95 ns | 0.69 ns | 0.72 ns | flecs |
| **each<Pos,Vel,Tag>** | 1.11 ns | 3.71 ns | **0.32 ns** | 5.59 ns | 0.77 ns | 1.26 ns | flecs |
| **query<4> without<1>** | 2.95 ns | 4.80 ns | **0.17 ns** | 9.61 ns | 0.87 ns | 1.43 ns | flecs |
| **destroyEntity** | 39.96 ns | 117.76 ns | 117.33 ns | 26.43 ns | 177.67 ns | **10.02 ns** | pico_ecs |
| **get<Pos>** | 2.21 ns | 10.15 ns | 49.91 ns | 6.22 ns | 16.55 ns | **1.46 ns** | pico_ecs |
| **7 systems mixed (full)** | 2.01 ns | 5.46 ns | **0.54 ns** | 11.59 ns | 1.73 ns | 2.41 ns | flecs |
| **frag 7sys mixed (alive)** | 4.15 ns | 9.80 ns | **0.55 ns** | 14.78 ns | 1.94 ns | 7.79 ns | flecs |
| **post-defrag 7sys mixed** | **1.57 ns** 🚀 | — | — | — | — | — | PulseECS |

*Validated with `sink=3000018393552` across all 6 engines.*
</details>

<details>
<summary><b>Click to expand: 200,000 Entities (Mid Scale Baseline)</b></summary>

| Operation | PulseECS | EnTT | flecs | EntityX | gaia-ecs | pico_ecs | Fastest |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **add components (Pos+Vel+50%Tag)** | 6.60 ns | 9.84 ns | 73.00 ns | 12.35 ns | 45.17 ns | **5.12 ns** | pico_ecs |
| **each<Pos,Vel>** | 0.63 ns | 1.26 ns | **0.21 ns** | 3.98 ns | 0.34 ns | 0.79 ns | flecs |
| **each<Pos,Vel,Tag>** | 1.16 ns | 2.82 ns | **0.27 ns** | 5.69 ns | 0.49 ns | 1.30 ns | flecs |
| **query<4> without<1>** | 2.93 ns | 3.99 ns | **0.16 ns** | 13.02 ns | 0.44 ns | 1.36 ns | flecs |
| **destroyEntity** | 15.90 ns | 37.59 ns | 43.38 ns | 17.78 ns | 95.12 ns | **7.34 ns** | pico_ecs |
| **get<Pos>** | 1.19 ns | 4.39 ns | 12.85 ns | 2.72 ns | 10.74 ns | **0.80 ns** | pico_ecs |
| **7 systems mixed (full)** | 1.81 ns | 4.30 ns | **0.59 ns** | 11.41 ns | 0.77 ns | 2.32 ns | flecs |
| **frag 7sys mixed (alive)** | 2.45 ns | 4.30 ns | **0.52 ns** | 14.10 ns | 0.85 ns | 3.09 ns | flecs |

*Validated with `sink=200006127435` across all 6 engines.*
</details>

<details>
<summary><b>Click to expand: 20,000 Entities (Game-sized World)</b></summary>

| Operation | PulseECS | EnTT | flecs | EntityX | gaia-ecs | pico_ecs | Fastest |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **add components (Pos+Vel+50%Tag)** | 8.28 ns | 9.92 ns | 80.81 ns | 15.73 ns | 58.41 ns | **5.39 ns** | pico_ecs |
| **each<Pos,Vel>** | 0.65 ns | 1.24 ns | **0.29 ns** | 5.16 ns | 0.34 ns | 0.93 ns | flecs |
| **each<Pos,Vel,Tag>** | 1.13 ns | 3.06 ns | **0.37 ns** | 7.08 ns | 0.50 ns | 1.58 ns | flecs |
| **query<4> without<1>** | 2.77 ns | 5.32 ns | **0.21 ns** | 11.62 ns | 0.43 ns | 1.79 ns | flecs |
| **destroyEntity** | **10.07 ns** | 25.98 ns | 30.19 ns | 19.64 ns | 64.88 ns | 8.67 ns | PulseECS |
| **get<Pos>** | **0.77 ns** | 4.19 ns | 11.21 ns | 2.78 ns | 7.16 ns | 0.83 ns | PulseECS |
| **7 systems mixed (full)** | 1.63 ns | 4.90 ns | 0.97 ns | 12.62 ns | **0.86 ns** | 2.61 ns | gaia-ecs |
| **frag 7sys mixed (alive)** | 1.72 ns | 3.75 ns | 0.91 ns | 14.81 ns | **0.77 ns** | 3.40 ns | gaia-ecs |

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
