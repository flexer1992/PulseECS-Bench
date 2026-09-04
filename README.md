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
| **Point Lookup** | `get<Pos>` | 🟢 **Fast** (`7.2 ns`) 🏆 | 🔴 Slower (`39.7 ns`) | 🔴 Slower (`80.9 ns`) | 🟢 **Fast** (`7.7 ns`) |
| **Packed Iteration** | `each<Pos, Vel>` | 🟢 **Fast** (`0.65 ns`) | 🟡 Moderate (`1.61 ns`) | 🟢 **Fast** (`0.50 ns`) | 🟢 **Fast** (`0.84 ns`) |
| **Structural Mutation** | `add components` | 🟡 Moderate (`14.3 ns`) | 🟢 **Fast** (`11.5 ns`) | 🔴 Heavy (`74.9 ns`) | 🟢 **Fast** (`5.9 ns`) |
| **Complex Queries** | `query<4> without<1>` | 🟡 Moderate (`4.12 ns`) | 🟡 Moderate (`5.34 ns`) | 🟢 **Blazing** (`0.19 ns`) | 🟢 **Fast** (`1.45 ns`) |
| **Entity Destruction** | `destroyEntity` | 🟢 **Fast** (`69.7 ns`) | 🔴 Slower (`157.5 ns`) | 🔴 Slower (`166.5 ns`) | 🟢 **Fast** (`30.1 ns`) |
| **Fragmented World** | `frag 7 systems` | 🟡 Degrades (`11.1 ns`) | 🟡 Degrades (`23.4 ns`) | 🟢 **Immune** (`0.47 ns`) | 🔴 Degrades (`18.1 ns`) |

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

* **`get<Pos>` (7.19 ns vs 39.74 ns):** The sparse index uses `uint32_t` rather than `size_t`, cutting index table memory footprint in half and improving cache/TLB efficiency during random lookups. Additionally, splitting `getContainer` into a slim fast-path (`containerPtr`) avoids instruction cache pollution.
* **`destroyEntity` (69.69 ns vs 157.49 ns):** A 64-bit component mask allows `destroyEntity()` to check active components in a single bitwise operation, skipping component pools that were never attached to that entity.
* **Pure POD & Zero-Sized Tag Architecture (`[[no_unique_address]]`):**
  - **Clean POD components:** User structs are standard layout (`struct Position { float x, y; };`), wrapped internally in a contiguous `Slot { EntityId owner; T data; }` so that `owner` and component data share the same cache line.
  - **Zero-Sized Tags:** Empty structs (where `std::is_empty_v<T>` is true) consume **zero bytes** of memory for instances. The `Slot` size collapses to exactly 4 bytes (`sizeof(EntityId)`), functioning as a pure entity ID vector.
  - **Instant Bitmask Filtering:** `hasComponent<T>()` and `query().exclude<T>()` resolve against the 64-bit component bitmask in a single CPU cycle instead of traversing sparse maps.

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
| **add components (Pos+Vel+50%Tag)** | 14.27 ns | 11.48 ns | 74.85 ns | 12.52 ns | 47.33 ns | **5.88 ns** | pico_ecs |
| **each<Pos,Vel>** | 0.65 ns | 1.61 ns | **0.50 ns** | 3.96 ns | 0.73 ns | 0.84 ns | flecs |
| **each<Pos,Vel,Tag>** | 1.24 ns | 4.20 ns | **0.28 ns** | 5.88 ns | 1.22 ns | 1.31 ns | flecs |
| **query<4> without<1>** | 4.12 ns | 5.34 ns | **0.19 ns** | 9.83 ns | 0.82 ns | 1.45 ns | flecs |
| **destroyEntity** | 69.69 ns | 157.49 ns | 166.46 ns | 68.78 ns | 315.48 ns | **30.08 ns** | pico_ecs |
| **get<Pos>** | **7.19 ns** | 39.74 ns | 80.85 ns | 17.50 ns | 33.47 ns | 7.68 ns | PulseECS |
| **7 systems mixed (full)** | 1.90 ns | 6.40 ns | **0.64 ns** | 12.08 ns | 2.85 ns | 2.58 ns | flecs |
| **frag 7sys mixed (alive)** | 11.09 ns | 23.39 ns | **0.47 ns** | 15.12 ns | 2.68 ns | 18.09 ns | flecs |

*Validated with `sink=100000061314363` across all 6 engines.*
</details>

<details>
<summary><b>Click to expand: 1,000,000 Entities (High Scale)</b></summary>

| Operation | PulseECS | EnTT | flecs | EntityX | gaia-ecs | pico_ecs | Fastest |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **add components (Pos+Vel+50%Tag)** | 10.54 ns | 10.08 ns | 73.39 ns | 11.89 ns | 44.76 ns | **5.04 ns** | pico_ecs |
| **each<Pos,Vel>** | 0.65 ns | 1.39 ns | **0.29 ns** | 3.99 ns | 0.67 ns | 0.82 ns | flecs |
| **each<Pos,Vel,Tag>** | 1.14 ns | 3.37 ns | **0.26 ns** | 5.87 ns | 0.74 ns | 1.23 ns | flecs |
| **query<4> without<1>** | 2.87 ns | 4.63 ns | **0.16 ns** | 10.02 ns | 0.81 ns | 1.41 ns | flecs |
| **destroyEntity** | 31.77 ns | 103.65 ns | 113.28 ns | 28.32 ns | 172.98 ns | **9.81 ns** | pico_ecs |
| **get<Pos>** | 1.67 ns | 7.75 ns | 50.80 ns | 5.52 ns | 15.13 ns | **1.30 ns** | pico_ecs |
| **7 systems mixed (full)** | 1.90 ns | 5.29 ns | **0.56 ns** | 11.36 ns | 1.67 ns | 2.43 ns | flecs |
| **frag 7sys mixed (alive)** | 3.94 ns | 9.60 ns | **0.45 ns** | 14.60 ns | 1.75 ns | 8.44 ns | flecs |

*Validated with `sink=3000018393552` across all 6 engines.*
</details>

<details>
<summary><b>Click to expand: 200,000 Entities (Mid Scale Baseline)</b></summary>

| Operation | PulseECS | EnTT | flecs | EntityX | gaia-ecs | pico_ecs | Fastest |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **add components (Pos+Vel+50%Tag)** | 10.30 ns | 10.01 ns | 73.72 ns | 11.52 ns | 43.63 ns | **4.95 ns** | pico_ecs |
| **each<Pos,Vel>** | 0.66 ns | 1.08 ns | **0.22 ns** | 3.68 ns | 0.33 ns | 0.77 ns | flecs |
| **each<Pos,Vel,Tag>** | 1.29 ns | 2.57 ns | **0.27 ns** | 5.73 ns | 0.52 ns | 1.22 ns | flecs |
| **query<4> without<1>** | 2.79 ns | 4.10 ns | **0.16 ns** | 9.54 ns | 0.37 ns | 1.36 ns | flecs |
| **destroyEntity** | 14.33 ns | 35.83 ns | 42.65 ns | 16.52 ns | 79.29 ns | **6.87 ns** | pico_ecs |
| **get<Pos>** | 1.15 ns | 4.23 ns | 11.65 ns | 2.91 ns | 10.33 ns | **0.91 ns** | pico_ecs |
| **7 systems mixed (full)** | 1.76 ns | 3.97 ns | **0.53 ns** | 11.28 ns | 0.91 ns | 2.20 ns | flecs |
| **frag 7sys mixed (alive)** | 2.10 ns | 5.83 ns | **0.50 ns** | 14.68 ns | 0.66 ns | 3.24 ns | flecs |

*Validated with `sink=200006127435` across all 6 engines.*
</details>

<details>
<summary><b>Click to expand: 20,000 Entities (Game-sized World)</b></summary>

| Operation | PulseECS | EnTT | flecs | EntityX | gaia-ecs | pico_ecs | Fastest |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **add components (Pos+Vel+50%Tag)** | 12.66 ns | 8.66 ns | 75.22 ns | 11.26 ns | 42.21 ns | **5.09 ns** | pico_ecs |
| **each<Pos,Vel>** | 0.66 ns | 1.12 ns | **0.24 ns** | 3.83 ns | 0.27 ns | 0.66 ns | flecs |
| **each<Pos,Vel,Tag>** | 0.98 ns | 2.45 ns | 0.38 ns | 5.67 ns | **0.34 ns** | 1.09 ns | gaia-ecs |
| **query<4> without<1>** | 2.44 ns | 3.75 ns | **0.24 ns** | 9.44 ns | 0.28 ns | 1.49 ns | flecs |
| **destroyEntity** | 10.72 ns | 22.70 ns | 25.52 ns | 15.67 ns | 57.99 ns | **6.98 ns** | pico_ecs |
| **get<Pos>** | **0.71 ns** | 3.56 ns | 9.91 ns | 2.51 ns | 6.79 ns | 0.76 ns | PulseECS |
| **7 systems mixed (full)** | 1.56 ns | 3.51 ns | 1.06 ns | 11.16 ns | **0.77 ns** | 2.05 ns | gaia-ecs |
| **frag 7sys mixed (alive)** | 1.58 ns | 3.69 ns | 0.77 ns | 13.36 ns | **0.66 ns** | 2.64 ns | gaia-ecs |

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
