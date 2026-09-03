// bench/ecs_bench_entityx.cpp
// EntityX v0.3.0 — те же сценарии что и в других бенчмарках.
//
// EntityX использует compile-time bitmask queries — каждая компонента
// получает статически назначенный бит. exclude делается вручную через has<>.

#include <entityx/entityx.h>

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

    struct Pos { float x{0}, y{0}; };
    struct Vel { float vx{0}, vy{0}; };
    struct Tag { std::uint32_t v{0}; };
    struct ExtraTag { std::uint8_t pad{0}; };
    struct ReqA { float w{0}; };
    struct ReqB { float w{0}; };
    struct ReqC { float w{0}; };
    struct ReqD { float w{0}; };
    struct Health { float hp{0}; };
    struct Armor { float def{0}; };
    struct Mana { float mp{0}; };
    struct Marker { std::uint8_t flag{0}; };
}

int main(int argc, char **argv)
{
    const Args args = parseArgs(argc, argv);

    std::cout << "ECS benchmark (EntityX)\n";
    std::cout << "  entities   : " << args.entities << "\n";
    std::cout << "  iterations : " << args.iterations << "\n";
    std::cout << "  seed       : " << args.seed << "\n\n";

    std::mt19937 rng(static_cast<std::uint32_t>(args.seed));
    std::uint64_t sink = 0;
    (void)sink;

    // ============================================================
    // add components (Pos,Vel + 50% Tag)
    // ============================================================
    {
        double total = 0.0;
        std::size_t ops = 0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            entityx::EntityX w;
            total += timeSeconds([&]
            {
                for (std::size_t i = 0; i < args.entities; ++i)
                {
                    auto e = w.entities.create();
                    e.assign<Pos>(Pos{});
                    e.assign<Vel>(Vel{});
                    if ((i & 1u) == 0u)
                        e.assign<Tag>(Tag{});
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
        entityx::EntityX w;
        std::vector<entityx::Entity> eids;
        eids.reserve(args.entities);
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = w.entities.create();
            eids.push_back(e);
            e.assign<Pos>(Pos{});
            e.assign<Vel>(Vel{});
        }
        std::shuffle(eids.begin(), eids.end(), rng);

        double total = 0.0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            total += timeSeconds([&]
            {
                std::uint64_t local = 0;
                w.entities.each<Pos, Vel>([&](entityx::Entity, Pos &p, Vel &v)
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
        entityx::EntityX w;
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = w.entities.create();
            e.assign<Pos>(Pos{});
            e.assign<Vel>(Vel{});
            if ((i & 1u) == 0u)
                e.assign<Tag>(Tag{static_cast<std::uint32_t>(i)});
        }

        double total = 0.0;
        std::size_t hits = 0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            total += timeSeconds([&]
            {
                std::uint64_t local = 0;
                std::size_t h = 0;
                w.entities.each<Pos, Vel, Tag>([&](entityx::Entity, Pos &p, Vel &v, Tag &t)
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
    // query<4> without<ExtraTag>
    // EntityX не имеет native exclude — делаем вручную через has<>.
    // ============================================================
    {
        entityx::EntityX w;
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = w.entities.create();
            e.assign<ReqA>(ReqA{static_cast<float>(i)});
            e.assign<ReqB>(ReqB{static_cast<float>(i)});
            e.assign<ReqC>(ReqC{static_cast<float>(i)});
            e.assign<ReqD>(ReqD{static_cast<float>(i)});
            if ((i & 7u) == 0u)
                e.assign<ExtraTag>(ExtraTag{});
        }

        double total = 0.0;
        std::size_t hits = 0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            total += timeSeconds([&]
            {
                std::uint64_t local = 0;
                std::size_t h = 0;
                w.entities.each<ReqA, ReqB, ReqC, ReqD>([&](entityx::Entity ent, ReqA &a, ReqB &b, ReqC &c, ReqD &d)
                {
                    if (ent.has_component<ExtraTag>()) return;
                    a.w += b.w + c.w + d.w;
                    local += 1;
                    ++h;
                });
                sink += local;
                hits = h;
            });
        }
        printRow("query<ReqA,B,C,D> without<ExtraTag> avg (per hit)", total / args.iterations, hits);
    }

    // ============================================================
    // Destroy entity
    // ============================================================
    {
        double total = 0.0;
        std::size_t ops = 0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            entityx::EntityX w;
            std::vector<entityx::Entity> eids;
            eids.reserve(args.entities);
            for (std::size_t i = 0; i < args.entities; ++i)
            {
                auto e = w.entities.create();
                eids.push_back(e);
                e.assign<Pos>(Pos{});
                e.assign<Vel>(Vel{});
            }
            std::shuffle(eids.begin(), eids.end(), rng);

            total += timeSeconds([&]
            {
                for (std::size_t i = 0; i < args.entities; ++i)
                    eids[i].destroy();
                ops = args.entities;
            });
        }
        printRow("destroyEntity avg", total / args.iterations, ops);
    }

    // ============================================================
    // get<Pos>
    // ============================================================
    {
        entityx::EntityX w;
        std::vector<entityx::Entity> eids;
        eids.reserve(args.entities);
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = w.entities.create();
            eids.push_back(e);
            e.assign<Pos>(Pos{static_cast<float>(i), static_cast<float>(i)});
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
                    const Pos* p = e.component<Pos>().get();
                    local += static_cast<std::uint64_t>(p->x + p->y);
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
        entityx::EntityX w;
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = w.entities.create();
            e.assign<Pos>(Pos{static_cast<float>(i), 0.0f});
            e.assign<Vel>(Vel{1.0f, 0.0f});
            if ((i & 1u) == 0u) e.assign<Tag>(Tag{static_cast<std::uint32_t>(i)});
            if ((i & 3u) == 0u) e.assign<Health>(Health{100.0f});
            if ((i & 7u) == 0u) e.assign<Armor>(Armor{50.0f});
            if ((i & 15u) == 0u) e.assign<Mana>(Mana{20.0f});
        }

        double total = 0.0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            total += timeSeconds([&]
            {
                std::uint64_t local = 0;

                w.entities.each<Pos, Vel>([&](entityx::Entity, Pos &p, Vel &v) { p.x += v.vx; p.y += v.vy; local += 1; });
                w.entities.each<Pos, Vel, Tag>([&](entityx::Entity, Pos &p, Vel &v, Tag &t) { p.x += v.vx * 0.5f; t.v ^= 1u; local += 1; });
                w.entities.each<Health>([&](entityx::Entity, Health &h) { h.hp -= 0.001f; local += 1; });
                w.entities.each<Health, Armor>([&](entityx::Entity, Health &h, Armor &a) { h.hp += a.def * 0.0001f; local += 1; });
                w.entities.each<Pos, Tag>([&](entityx::Entity, Pos &p, Tag &t) { p.y += 0.01f * t.v; local += 1; });
                w.entities.each<Mana>([&](entityx::Entity, Mana &m) { m.mp -= 0.002f; local += 1; });
                w.entities.each<Vel, Armor>([&](entityx::Entity, Vel &v, Armor &a) { v.vx += a.def * 0.0001f; local += 1; });

                sink += local;
            });
        }
        printRow("7 systems mixed avg (per hit)", total / args.iterations, args.entities);
    }

    // ============================================================
    // Fragmented world
    // ============================================================
    {
        entityx::EntityX w;
        std::vector<entityx::Entity> initial;
        initial.reserve(args.entities);
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = w.entities.create();
            initial.push_back(e);
            e.assign<Pos>(Pos{});
            e.assign<Vel>(Vel{});
            if ((i & 1u) == 0u) e.assign<Tag>(Tag{});
            if ((i & 3u) == 0u) e.assign<Health>(Health{});
            if ((i & 7u) == 0u) e.assign<Armor>(Armor{});
            if ((i & 15u) == 0u) e.assign<Mana>(Mana{});
            if ((i & 31u) == 0u) e.assign<Marker>(Marker{});
        }
        std::shuffle(initial.begin(), initial.end(), rng);
        const std::size_t toDestroy = args.entities * 30u / 100u;
        for (std::size_t i = 0; i < toDestroy; ++i)
            initial[i].destroy();

        const std::size_t toCreate = args.entities * 20u / 100u;
        for (std::size_t i = 0; i < toCreate; ++i)
        {
            auto e = w.entities.create();
            e.assign<Pos>(Pos{});
            e.assign<Vel>(Vel{});
            if ((i & 1u) == 0u) e.assign<Tag>(Tag{});
        }

        std::size_t alive = 0;
        w.entities.each<Pos>([&](entityx::Entity, Pos &) { ++alive; });

        double total = 0.0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            total += timeSeconds([&]
            {
                std::uint64_t local = 0;
                w.entities.each<Pos, Vel>([&](entityx::Entity, Pos &p, Vel &v) { p.x += v.vx; local += 1; });
                w.entities.each<Pos, Vel, Tag>([&](entityx::Entity, Pos &p, Vel &v, Tag &t) { t.v ^= 1u; local += 1; });
                w.entities.each<Health>([&](entityx::Entity, Health &h) { h.hp -= 0.001f; local += 1; });
                w.entities.each<Health, Armor>([&](entityx::Entity, Health &h, Armor &a) { h.hp += a.def * 0.0001f; local += 1; });
                w.entities.each<Pos, Tag>([&](entityx::Entity, Pos &p, Tag &t) { p.y += 0.01f * t.v; local += 1; });
                w.entities.each<Mana>([&](entityx::Entity, Mana &m) { m.mp -= 0.002f; local += 1; });
                w.entities.each<Vel, Armor>([&](entityx::Entity, Vel &v, Armor &a) { v.vx += a.def * 0.0001f; local += 1; });
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