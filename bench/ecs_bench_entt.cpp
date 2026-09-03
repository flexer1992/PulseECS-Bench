// bench/ecs_bench_entt.cpp
// EnTT-версия того же бенчмарка что и ecs_bench.cpp.
// Цель: честное сравнение на тех же сценариях, той же машине.
//
// ВАЖНО: используем 64-bit entity (entity_mask = 0xFFFFFFFF = ~4 млрд),
// а не дефолтный 32-bit (entity_mask = 0xFFFFF = ~1 млн). Иначе на 2M
// сущностях EnTT молча теряет половину emplace: при достижении лимита
// идентификаторов storage делает bump(version) вместо overwrite данных,
// и вторые 1M Pos-компонентов оказываются пустыми. Sink расходится с
// остальными фреймворками, цифры в таблице — нечестные.

#include <entt/entt.hpp>

using Entity = std::uint64_t;
using Registry = entt::basic_registry<Entity>;

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <random>
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

    // Components — те же что в ecs_bench.cpp, без owner (EnTT его не требует)
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
    struct Extra
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

    std::cout << "ECS benchmark (EnTT)\n";
    std::cout << "  entities   : " << args.entities << "\n";
    std::cout << "  iterations : " << args.iterations << "\n";
    std::cout << "  seed       : " << args.seed << "\n\n";

    std::mt19937 rng(static_cast<std::uint32_t>(args.seed));

    // Sink — не даём компилятору выкинуть работу.
    std::uint64_t sink = 0;

    // ============================================================
    // add components (Pos,Vel + 50% Tag)
    // ============================================================
    {
        double total = 0.0;
        std::size_t ops = 0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            Registry r;
            total += timeSeconds([&]
            {
                for (std::size_t i = 0; i < args.entities; ++i)
                {
                    auto e = r.create();
                    r.emplace<Pos>(e, Pos{});
                    r.emplace<Vel>(e, Vel{});
                    if ((i & 1u) == 0u)
                        r.emplace<Tag>(e, Tag{});
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
        Registry r;
        std::vector<Entity> eids;
        eids.reserve(args.entities);
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = r.create();
            eids.push_back(e);
            r.emplace<Pos>(e, Pos{});
            r.emplace<Vel>(e, Vel{});
        }
        std::shuffle(eids.begin(), eids.end(), rng);

        double total = 0.0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            total += timeSeconds([&]
            {
                std::uint64_t local = 0;
                auto view = r.view<Pos, Vel>();
                view.each([&](Pos &p, Vel &v)
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
    // each<Pos, Vel, Tag>
    // ============================================================
    {
        Registry r;
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = r.create();
            r.emplace<Pos>(e, Pos{});
            r.emplace<Vel>(e, Vel{});
            if ((i & 1u) == 0u)
                r.emplace<Tag>(e, Tag{static_cast<std::uint32_t>(i)});
        }

        double total = 0.0;
        std::size_t hits = 0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            total += timeSeconds([&]
            {
                std::uint64_t local = 0;
                std::size_t h = 0;
                auto view = r.view<Pos, Vel, Tag>();
                view.each([&](Pos &p, Vel &v, Tag &t)
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
    // query().exclude<Extra>
    // ============================================================
    {
        Registry r;
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = r.create();
            r.emplace<ReqA>(e, ReqA{static_cast<float>(i)});
            r.emplace<ReqB>(e, ReqB{static_cast<float>(i)});
            r.emplace<ReqC>(e, ReqC{static_cast<float>(i)});
            r.emplace<ReqD>(e, ReqD{static_cast<float>(i)});
            if ((i & 7u) == 0u)
                r.emplace<Extra>(e, Extra{});
        }

        // req=4 exclude<Extra>
        {
            double total = 0.0;
            std::size_t hits = 0;
            for (std::size_t it = 0; it < args.iterations; ++it)
            {
                total += timeSeconds([&]
                {
                    std::uint64_t local = 0;
                    std::size_t h = 0;
                    auto view = r.view<ReqA, ReqB, ReqC, ReqD>(entt::exclude<Extra>);
                    view.each([&](ReqA &a, ReqB &b, ReqC &c, ReqD &d)
                    {
                        a.w += b.w + c.w + d.w;
                        local += 1;
                        ++h;
                    });
                    sink += local;
                    hits = h;
                });
            }
            printRow("query<ReqA,B,C,D> exclude<Extra> avg (per hit)", total / args.iterations, hits);
        }
    }

    // ============================================================
    // Destroy entity
    // ============================================================
    {
        double total = 0.0;
        std::size_t ops = 0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            Registry r;
            std::vector<Entity> eids;
            eids.reserve(args.entities);
            for (std::size_t i = 0; i < args.entities; ++i)
            {
                auto e = r.create();
                eids.push_back(e);
                r.emplace<Pos>(e, Pos{});
                r.emplace<Vel>(e, Vel{});
            }
            std::shuffle(eids.begin(), eids.end(), rng);

            total += timeSeconds([&]
            {
                for (std::size_t i = 0; i < args.entities; ++i)
                    r.destroy(eids[i]);
                ops = args.entities;
            });
        }
        printRow("destroyEntity avg", total / args.iterations, ops);
    }

    // ============================================================
    // get<Pos>
    // ============================================================
    {
        Registry r;
        std::vector<Entity> eids;
        eids.reserve(args.entities);
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = r.create();
            eids.push_back(e);
            r.emplace<Pos>(e, Pos{static_cast<float>(i), static_cast<float>(i)});
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
                    auto &p = r.get<Pos>(e);
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
        Registry r;
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = r.create();
            r.emplace<Pos>(e, Pos{static_cast<float>(i), 0.0f});
            r.emplace<Vel>(e, Vel{1.0f, 0.0f});
            if ((i & 1u) == 0u)
                r.emplace<Tag>(e, Tag{static_cast<std::uint32_t>(i)});
            if ((i & 3u) == 0u)
                r.emplace<Health>(e, Health{100.0f});
            if ((i & 7u) == 0u)
                r.emplace<Armor>(e, Armor{50.0f});
            if ((i & 15u) == 0u)
                r.emplace<Mana>(e, Mana{20.0f});
        }

        double total = 0.0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            total += timeSeconds([&]
            {
                std::uint64_t local = 0;

                // sys 1: Pos+Vel
                r.view<Pos, Vel>().each([&](Pos &p, Vel &v)
                {
                    p.x += v.vx; p.y += v.vy; local += 1;
                });
                // sys 2: Pos+Vel+Tag
                r.view<Pos, Vel, Tag>().each([&](Pos &p, Vel &v, Tag &t)
                {
                    p.x += v.vx * 0.5f; t.v ^= 1u; local += 1;
                });
                // sys 3: Health
                r.view<Health>().each([&](Health &h) { h.hp -= 0.001f; local += 1; });
                // sys 4: Health+Armor
                r.view<Health, Armor>().each([&](Health &h, Armor &a)
                {
                    h.hp += a.def * 0.0001f; local += 1;
                });
                // sys 5: Pos+Tag
                r.view<Pos, Tag>().each([&](Pos &p, Tag &t) { p.y += 0.01f * t.v; local += 1; });
                // sys 6: Mana
                r.view<Mana>().each([&](Mana &m) { m.mp -= 0.002f; local += 1; });
                // sys 7: Vel+Armor
                r.view<Vel, Armor>().each([&](Vel &v, Armor &a) { v.vx += a.def * 0.0001f; local += 1; });
sink += local;

            });
        }
        printRow("7 systems mixed avg (per hit)", total / args.iterations, args.entities);
    }

    // ============================================================
    // Fragmented: create N, destroy 30%, create 20%
    // ============================================================
    {
        Registry r;
        std::vector<Entity> initial;
        initial.reserve(args.entities);
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = r.create();
            initial.push_back(e);
            r.emplace<Pos>(e, Pos{});
            r.emplace<Vel>(e, Vel{});
            if ((i & 1u) == 0u) r.emplace<Tag>(e, Tag{});
            if ((i & 3u) == 0u) r.emplace<Health>(e, Health{});
            if ((i & 7u) == 0u) r.emplace<Armor>(e, Armor{});
            if ((i & 15u) == 0u) r.emplace<Mana>(e, Mana{});
            if ((i & 31u) == 0u) r.emplace<Marker>(e, Marker{});
        }
        std::shuffle(initial.begin(), initial.end(), rng);
        const std::size_t toDestroy = args.entities * 30u / 100u;
        for (std::size_t i = 0; i < toDestroy; ++i)
            r.destroy(initial[i]);

        const std::size_t toCreate = args.entities * 20u / 100u;
        for (std::size_t i = 0; i < toCreate; ++i)
        {
            auto e = r.create();
            r.emplace<Pos>(e, Pos{});
            r.emplace<Vel>(e, Vel{});
            if ((i & 1u) == 0u) r.emplace<Tag>(e, Tag{});
        }

        // EnTT не имеет registry.alive() — посчитаем через view всех entity
        std::size_t alive = 0;
        for (auto _ : r.view<Pos>()) { (void)_; ++alive; }

        double total = 0.0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            total += timeSeconds([&]
            {
                std::uint64_t local = 0;
                r.view<Pos, Vel>().each([&](Pos &p, Vel &v) { p.x += v.vx; local += 1; });
                r.view<Pos, Vel, Tag>().each([&](Pos &p, Vel &v, Tag &t) { t.v ^= 1u; local += 1; });
                r.view<Health>().each([&](Health &h) { h.hp -= 0.001f; local += 1; });
                r.view<Health, Armor>().each([&](Health &h, Armor &a) { h.hp += a.def * 0.0001f; local += 1; });
                r.view<Pos, Tag>().each([&](Pos &p, Tag &t) { p.y += 0.01f * t.v; local += 1; });
                r.view<Mana>().each([&](Mana &m) { m.mp -= 0.002f; local += 1; });
                r.view<Vel, Armor>().each([&](Vel &v, Armor &a) { v.vx += a.def * 0.0001f; local += 1; });
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