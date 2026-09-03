// bench/ecs_bench_flecs.cpp
// flecs v4.1.1 — те же сценарии что в ecs_bench.cpp и ecs_bench_entt.cpp.

#include <flecs.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;

    struct Args
    {
        std::size_t entities = 200'000;
        std::size_t iterations = 5;
        std::uint64_t seed = 1337;
    };

    bool parseFlagValue(int argc, char **argv, const std::string &flag, std::string &out)
    {
        for (int i = 1; i + 1 < argc; ++i)
        {
            if (flag == argv[i])
            {
                out = argv[i + 1];
                return true;
            }
        }
        return false;
    }

    Args parseArgs(int argc, char **argv)
    {
        Args a;
        std::string v;
        if (parseFlagValue(argc, argv, "--entities", v))
            a.entities = static_cast<std::size_t>(std::stoull(v));
        if (parseFlagValue(argc, argv, "--iterations", v))
            a.iterations = static_cast<std::size_t>(std::stoull(v));
        if (parseFlagValue(argc, argv, "--seed", v))
            a.seed = static_cast<std::uint64_t>(std::stoull(v));
        return a;
    }

    template <class F>
    double timeSeconds(F &&f)
    {
        const auto t0 = Clock::now();
        f();
        const auto t1 = Clock::now();
        return std::chrono::duration<double>(t1 - t0).count();
    }

    void printRow(const std::string &name, double seconds, std::size_t ops)
    {
        const double nsPerOp = ops ? (seconds * 1e9) / static_cast<double>(ops) : 0.0;
        std::cout << name << ": " << seconds * 1000.0 << " ms"
                  << " | " << nsPerOp << " ns/op"
                  << " | ops=" << ops << "\n";
    }

    // Компоненты — те же что в ecs_bench.cpp
    struct Pos
    {
        float x{0}, y{0};
    };
    struct Vel
    {
        float vx{0}, vy{0};
    };
    struct Tag
    {
        std::uint32_t v{0};
    };
    struct ExtraTag
    {
        std::uint8_t pad{0};
    };
    struct ReqA
    {
        float w{0};
    };
    struct ReqB
    {
        float w{0};
    };
    struct ReqC
    {
        float w{0};
    };
    struct ReqD
    {
        float w{0};
    };
    struct Health
    {
        float hp{0};
    };
    struct Armor
    {
        float def{0};
    };
    struct Mana
    {
        float mp{0};
    };
    struct Marker
    {
        std::uint8_t flag{0};
    };
}

int main(int argc, char **argv)
{
    const Args args = parseArgs(argc, argv);

    std::cout << "ECS benchmark (flecs " << FLECS_VERSION_MAJOR << "."
              << FLECS_VERSION_MINOR << "." << FLECS_VERSION_PATCH << ")\n";
    std::cout << "  entities   : " << args.entities << "\n";
    std::cout << "  iterations : " << args.iterations << "\n";
    std::cout << "  seed       : " << args.seed << "\n\n";

    std::mt19937 rng(static_cast<std::uint32_t>(args.seed));
    std::uint64_t sink = 0;
    (void)sink;

    std::vector<flecs::entity> ids;
    ids.reserve(args.entities);

    // ============================================================
    // add components (Pos,Vel + 50% Tag)
    // ============================================================
    {
        double total = 0.0;
        std::size_t ops = 0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            flecs::world w;
            total += timeSeconds([&]
            {
                for (std::size_t i = 0; i < args.entities; ++i)
                {
                    auto e = w.entity();
                    e.set<Pos>({});
                    e.set<Vel>({});
                    if ((i & 1u) == 0u)
                        e.set<Tag>({});
                }
                ops = args.entities * 2u + (args.entities / 2u);
            });
        }
        printRow("add components (Pos,Vel + 50% Tag) avg", total / args.iterations, ops);
    }

    // ============================================================
    // each<Pos, Vel>
    // ============================================================
    {
        flecs::world w;
        std::vector<flecs::entity> eids;
        eids.reserve(args.entities);
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = w.entity();
            eids.push_back(e);
            e.set<Pos>({});
            e.set<Vel>({});
        }
        std::shuffle(eids.begin(), eids.end(), rng);

        double total = 0.0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            total += timeSeconds([&]
            {
                std::uint64_t local = 0;
                w.each([&](Pos &p, Vel &v)
                {
                    p.x += v.vx * 0.001f;
                    p.y += v.vy * 0.001f;
                    local += 1;
                });
                sink += local;
            });
        }
        printRow("each<Pos,Vel> avg", total / args.iterations, args.entities);
    }

    // ============================================================
    // each<Pos, Vel, Tag> (Tag у 50%)
    // ============================================================
    {
        flecs::world w;
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = w.entity();
            e.set<Pos>({});
            e.set<Vel>({});
            if ((i & 1u) == 0u)
                e.set<Tag>({static_cast<std::uint32_t>(i)});
        }

        double total = 0.0;
        std::size_t hits = 0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            total += timeSeconds([&]
            {
                std::uint64_t local = 0;
                std::size_t h = 0;
                w.each([&](Pos &p, Vel &v, Tag &t)
                {
                    p.x += v.vx * 0.001f;
                    p.y += v.vy * 0.001f;
                    t.v = (t.v + 1u) & 0xFFu;
                    local += 1;
                    ++h;
                });
                sink += local;
                hits = h;
            });
        }
        printRow("each<Pos,Vel,Tag> avg (per hit)", total / args.iterations, hits);
    }

    // ============================================================
    // query<4> without<Extra>
    // ============================================================
    {
        flecs::world w;
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = w.entity();
            e.set<ReqA>({static_cast<float>(i)});
            e.set<ReqB>({static_cast<float>(i)});
            e.set<ReqC>({static_cast<float>(i)});
            e.set<ReqD>({static_cast<float>(i)});
            if ((i & 7u) == 0u)
                e.set<ExtraTag>({});
        }

        auto q = w.query_builder<ReqA, ReqB, ReqC, ReqD>().without<ExtraTag>().build();

        double total = 0.0;
        std::size_t hits = 0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            total += timeSeconds([&]
            {
                std::uint64_t local = 0;
                std::size_t h = 0;
                q.each([&](ReqA &a, ReqB &b, ReqC &c, ReqD &d)
                {
                    a.w += b.w + c.w + d.w;
                    local += 1;
                    ++h;
                });
                sink += local;
                hits = h;
            });
        }
        printRow("query<ReqA,B,C,D> without<Extra> avg (per hit)", total / args.iterations, hits);
    }

    // ============================================================
    // Destroy entity
    // ============================================================
    {
        double total = 0.0;
        std::size_t ops = 0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            flecs::world w;
            std::vector<flecs::entity> eids;
            eids.reserve(args.entities);
            for (std::size_t i = 0; i < args.entities; ++i)
            {
                auto e = w.entity();
                eids.push_back(e);
                e.set<Pos>({});
                e.set<Vel>({});
            }
            std::shuffle(eids.begin(), eids.end(), rng);

            total += timeSeconds([&]
            {
                for (std::size_t i = 0; i < args.entities; ++i)
                    eids[i].destruct();
                ops = args.entities;
            });
        }
        printRow("destroyEntity avg", total / args.iterations, ops);
    }

    // ============================================================
    // get<Pos>
    // ============================================================
    {
        flecs::world w;
        std::vector<flecs::entity> eids;
        eids.reserve(args.entities);
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = w.entity();
            eids.push_back(e);
            e.set<Pos>({static_cast<float>(i), static_cast<float>(i)});
        }
        std::shuffle(eids.begin(), eids.end(), rng);

        double total = 0.0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            total += timeSeconds([&]
            {
                std::uint64_t local = 0;
                for (auto e : eids)
                {
                    auto &p = e.get<Pos>();
                    local += static_cast<std::uint64_t>(p.x + p.y);
                }
                sink += local;
            });
        }
        printRow("get<Pos> avg (per hit)", total / args.iterations, args.entities);
    }

    // ============================================================
    // 7 systems mixed update
    // ============================================================
    {
        flecs::world w;
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = w.entity();
            e.set<Pos>({static_cast<float>(i), 0.0f});
            e.set<Vel>({1.0f, 0.0f});
            if ((i & 1u) == 0u) e.set<Tag>({static_cast<std::uint32_t>(i)});
            if ((i & 3u) == 0u) e.set<Health>({100.0f});
            if ((i & 7u) == 0u) e.set<Armor>({50.0f});
            if ((i & 15u) == 0u) e.set<Mana>({20.0f});
        }

        double total = 0.0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            total += timeSeconds([&]
            {
                std::uint64_t local = 0;

                w.each([&](Pos &p, Vel &v) { p.x += v.vx; p.y += v.vy; local += 1; });
                w.each([&](Pos &p, Vel &v, Tag &t) { p.x += v.vx * 0.5f; t.v ^= 1u; local += 1; });
                w.each([&](Health &h) { h.hp -= 0.001f; local += 1; });
                w.each([&](Health &h, Armor &a) { h.hp += a.def * 0.0001f; local += 1; });
                w.each([&](Pos &p, Tag &t) { p.y += 0.01f * t.v; local += 1; });
                w.each([&](Mana &m) { m.mp -= 0.002f; local += 1; });
                w.each([&](Vel &v, Armor &a) { v.vx += a.def * 0.0001f; local += 1; });

                sink += local;
            });
        }
        printRow("7 systems mixed avg (per hit)", total / args.iterations, args.entities);
    }

    // ============================================================
    // Fragmented world: create N, destroy 30%, create 20%
    // ============================================================
    {
        flecs::world w;
        std::vector<flecs::entity> initial;
        initial.reserve(args.entities);
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = w.entity();
            initial.push_back(e);
            e.set<Pos>({});
            e.set<Vel>({});
            if ((i & 1u) == 0u) e.set<Tag>({});
            if ((i & 3u) == 0u) e.set<Health>({});
            if ((i & 7u) == 0u) e.set<Armor>({});
            if ((i & 15u) == 0u) e.set<Mana>({});
            if ((i & 31u) == 0u) e.set<Marker>({});
        }
        std::shuffle(initial.begin(), initial.end(), rng);
        const std::size_t toDestroy = args.entities * 30u / 100u;
        for (std::size_t i = 0; i < toDestroy; ++i)
            initial[i].destruct();

        const std::size_t toCreate = args.entities * 20u / 100u;
        for (std::size_t i = 0; i < toCreate; ++i)
        {
            auto e = w.entity();
            e.set<Pos>({});
            e.set<Vel>({});
            if ((i & 1u) == 0u) e.set<Tag>({});
        }

        // Подсчёт живых через query
        std::size_t alive = 0;
        w.each<Pos>([&](Pos &) { ++alive; });

        double total = 0.0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            total += timeSeconds([&]
            {
                std::uint64_t local = 0;
                w.each([&](Pos &p, Vel &v) { p.x += v.vx; local += 1; });
                w.each([&](Pos &p, Vel &v, Tag &t) { t.v ^= 1u; local += 1; });
                w.each([&](Health &h) { h.hp -= 0.001f; local += 1; });
                w.each([&](Health &h, Armor &a) { h.hp += a.def * 0.0001f; local += 1; });
                w.each([&](Pos &p, Tag &t) { p.y += 0.01f * t.v; local += 1; });
                w.each([&](Mana &m) { m.mp -= 0.002f; local += 1; });
                w.each([&](Vel &v, Armor &a) { v.vx += a.def * 0.0001f; local += 1; });
                sink += local;
            });
        }
        std::ostringstream label;
        label << "frag 7sys mixed (alive=" << alive << ") avg (per hit)";
        printRow(label.str(), total / args.iterations, alive);
    }

    std::cout << "\nsink=" << sink << "\n";
    return 0;
}