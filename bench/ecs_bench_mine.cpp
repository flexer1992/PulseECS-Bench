// bench/ecs_bench_mine.cpp
// Собственный ECS движка на ТЕХ ЖЕ сценариях, что entt/flecs/entityx/gaia/pico.
//
// Зачем отдельный файл, если есть ecs_bench.cpp:
// ecs_bench.cpp — детальный scaling-бенчмарк своего ECS. Он гоняет собственную
// лестницу масштабов {64 … 1'000'000} и игнорирует --entities, а плотности
// компонентов там свои (Tag у 10%, а не у 50%). Для сравнения с чужими ECS он
// не годится: сравнивались бы разные сценарии на разных размерах мира.
// Этот файл — построчное зеркало ecs_bench_entt.cpp, отличается только вызовами
// API. Он и попадает в таблицу compare_ecs.sh; ecs_bench.cpp остаётся как есть.
//
// Что обязано совпадать с остальными бенчами, иначе сравнение развалится:
//   * плотности компонентов: Tag i&1, Health i&3, Armor i&7, Mana i&15,
//     Marker i&31, Extra i&7;
//   * ровно четыре std::shuffle в тех же местах (поток rng влияет на то, какие
//     entity удаляются в фрагментированном сценарии);
//   * тексты меток в printRow — по ним скрипт находит строки;
//   * накопление в sink — он служит контролем эквивалентности.
//
// Компоненты — чистые POD-структуры без метаданных движка: обёртку
// Slot { EntityId owner; T data; } движок создаёт внутри SparseSet, поэтому
// агрегатная инициализация выглядит как Pos{x, y}, без служебных полей.

#include "engine/ecs/World.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
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

    // Компоненты — чистые POD-структуры без метаданных движка (без поля owner)
    struct Pos    { float x{0}, y{0}; };
    struct Vel    { float vx{0}, vy{0}; };
    struct Tag    { std::uint32_t v{0}; };
    struct Extra  {}; // Zero-sized Tag component (std::is_empty_v<Extra> == true)
    struct ReqA   { float w{0}; };
    struct ReqB   { float w{0}; };
    struct ReqC   { float w{0}; };
    struct ReqD   { float w{0}; };
    struct Health { float hp{0}; };
    struct Armor  { float def{0}; };
    struct Mana   { float mp{0}; };
    struct Marker { std::uint8_t flag{0}; };
}

int main(int argc, char **argv)
{
    const Args args = parseArgs(argc, argv);

    std::cout << "ECS benchmark (my ECS)\n";
    std::cout << "  entities   : " << args.entities << "\n";
    std::cout << "  iterations : " << args.iterations << "\n";
    std::cout << "  seed       : " << args.seed << "\n\n";

    std::mt19937 rng(static_cast<std::uint32_t>(args.seed));

    std::uint64_t sink = 0;

    // ============================================================
    // add components (Pos,Vel + 50% Tag)
    // ============================================================
    {
        double total = 0.0;
        std::size_t ops = 0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            engine::World world;
            total += timeSeconds([&]
            {
                for (std::size_t i = 0; i < args.entities; ++i)
                {
                    auto e = world.createEntity();
                    world.addComponent<Pos>(e, Pos{});
                    world.addComponent<Vel>(e, Vel{});
                    if ((i & 1u) == 0u)
                        world.addComponent<Tag>(e, Tag{});
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
        engine::World world;
        std::vector<engine::EntityId> eids;
        eids.reserve(args.entities);
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = world.createEntity();
            eids.push_back(e);
            world.addComponent<Pos>(e, Pos{});
            world.addComponent<Vel>(e, Vel{});
        }
        std::shuffle(eids.begin(), eids.end(), rng);

        double total = 0.0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            total += timeSeconds([&]
            {
                std::uint64_t local = 0;
                world.each<Pos, Vel>([&](Pos &p, Vel &v)
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
        engine::World world;
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = world.createEntity();
            world.addComponent<Pos>(e, Pos{});
            world.addComponent<Vel>(e, Vel{});
            if ((i & 1u) == 0u)
                world.addComponent<Tag>(e, Tag{static_cast<std::uint32_t>(i)});
        }

        double total = 0.0;
        std::size_t hits = 0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            total += timeSeconds([&]
            {
                std::uint64_t local = 0;
                std::size_t h = 0;
                world.each<Pos, Vel, Tag>([&](Pos &p, Vel &v, Tag &t)
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
    // query<4>().exclude<Extra>
    // ============================================================
    {
        engine::World world;
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = world.createEntity();
            world.addComponent<ReqA>(e, ReqA{static_cast<float>(i)});
            world.addComponent<ReqB>(e, ReqB{static_cast<float>(i)});
            world.addComponent<ReqC>(e, ReqC{static_cast<float>(i)});
            world.addComponent<ReqD>(e, ReqD{static_cast<float>(i)});
            if ((i & 7u) == 0u)
                world.addComponent<Extra>(e, Extra{});
        }

        double total = 0.0;
        std::size_t hits = 0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            total += timeSeconds([&]
            {
                std::uint64_t local = 0;
                std::size_t h = 0;
                world.query<ReqA, ReqB, ReqC, ReqD>().template exclude<Extra>(
                    [&](ReqA &a, ReqB &b, ReqC &c, ReqD &d)
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

    // ============================================================
    // Destroy entity
    // ============================================================
    {
        double total = 0.0;
        std::size_t ops = 0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            engine::World world;
            std::vector<engine::EntityId> eids;
            eids.reserve(args.entities);
            for (std::size_t i = 0; i < args.entities; ++i)
            {
                auto e = world.createEntity();
                eids.push_back(e);
                world.addComponent<Pos>(e, Pos{});
                world.addComponent<Vel>(e, Vel{});
            }
            std::shuffle(eids.begin(), eids.end(), rng);

            total += timeSeconds([&]
            {
                for (std::size_t i = 0; i < args.entities; ++i)
                    world.destroyEntity(eids[i]);
                ops = args.entities;
            });
        }
        printRow("destroyEntity avg", total / args.iterations, ops);
    }

    // ============================================================
    // get<Pos>
    // ============================================================
    {
        engine::World world;
        std::vector<engine::EntityId> eids;
        eids.reserve(args.entities);
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = world.createEntity();
            eids.push_back(e);
            world.addComponent<Pos>(e, Pos{static_cast<float>(i), static_cast<float>(i)});
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
                    auto *p = world.getComponent<Pos>(e);
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
        engine::World world;
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = world.createEntity();
            world.addComponent<Pos>(e, Pos{static_cast<float>(i), 0.0f});
            world.addComponent<Vel>(e, Vel{1.0f, 0.0f});
            if ((i & 1u) == 0u)
                world.addComponent<Tag>(e, Tag{static_cast<std::uint32_t>(i)});
            if ((i & 3u) == 0u)
                world.addComponent<Health>(e, Health{100.0f});
            if ((i & 7u) == 0u)
                world.addComponent<Armor>(e, Armor{50.0f});
            if ((i & 15u) == 0u)
                world.addComponent<Mana>(e, Mana{20.0f});
        }

        double total = 0.0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            total += timeSeconds([&]
            {
                std::uint64_t local = 0;

                // sys 1: Pos+Vel
                world.each<Pos, Vel>([&](Pos &p, Vel &v)
                {
                    p.x += v.vx; p.y += v.vy; local += 1;
                });
                // sys 2: Pos+Vel+Tag
                world.each<Pos, Vel, Tag>([&](Pos &p, Vel &v, Tag &t)
                {
                    p.x += v.vx * 0.5f; t.v ^= 1u; local += 1;
                });
                // sys 3: Health
                world.each<Health>([&](Health &h) { h.hp -= 0.001f; local += 1; });
                // sys 4: Health+Armor
                world.each<Health, Armor>([&](Health &h, Armor &a)
                {
                    h.hp += a.def * 0.0001f; local += 1;
                });
                // sys 5: Pos+Tag
                world.each<Pos, Tag>([&](Pos &p, Tag &t) { p.y += 0.01f * t.v; local += 1; });
                // sys 6: Mana
                world.each<Mana>([&](Mana &m) { m.mp -= 0.002f; local += 1; });
                // sys 7: Vel+Armor
                world.each<Vel, Armor>([&](Vel &v, Armor &a) { v.vx += a.def * 0.0001f; local += 1; });

                sink += local;
            });
        }
        printRow("7 systems mixed avg (per hit)", total / args.iterations, args.entities);
    }

    // ============================================================
    // Fragmented: create N, destroy 30%, create 20%
    // ============================================================
    {
        engine::World world;
        std::vector<engine::EntityId> initial;
        initial.reserve(args.entities);
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = world.createEntity();
            initial.push_back(e);
            world.addComponent<Pos>(e, Pos{});
            world.addComponent<Vel>(e, Vel{});
            if ((i & 1u) == 0u)  world.addComponent<Tag>(e, Tag{});
            if ((i & 3u) == 0u)  world.addComponent<Health>(e, Health{});
            if ((i & 7u) == 0u)  world.addComponent<Armor>(e, Armor{});
            if ((i & 15u) == 0u) world.addComponent<Mana>(e, Mana{});
            if ((i & 31u) == 0u) world.addComponent<Marker>(e, Marker{});
        }
        std::shuffle(initial.begin(), initial.end(), rng);
        const std::size_t toDestroy = args.entities * 30u / 100u;
        for (std::size_t i = 0; i < toDestroy; ++i)
            world.destroyEntity(initial[i]);

        const std::size_t toCreate = args.entities * 20u / 100u;
        for (std::size_t i = 0; i < toCreate; ++i)
        {
            auto e = world.createEntity();
            world.addComponent<Pos>(e, Pos{});
            world.addComponent<Vel>(e, Vel{});
            if ((i & 1u) == 0u) world.addComponent<Tag>(e, Tag{});
        }

        std::size_t alive = 0;
        world.each<Pos>([&](Pos &) { ++alive; });

        double total = 0.0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            total += timeSeconds([&]
            {
                std::uint64_t local = 0;
                world.each<Pos, Vel>([&](Pos &p, Vel &v) { p.x += v.vx; local += 1; });
                world.each<Pos, Vel, Tag>([&](Pos &, Vel &, Tag &t) { t.v ^= 1u; local += 1; });
                world.each<Health>([&](Health &h) { h.hp -= 0.001f; local += 1; });
                world.each<Health, Armor>([&](Health &h, Armor &a) { h.hp += a.def * 0.0001f; local += 1; });
                world.each<Pos, Tag>([&](Pos &p, Tag &t) { p.y += 0.01f * t.v; local += 1; });
                world.each<Mana>([&](Mana &m) { m.mp -= 0.002f; local += 1; });
                world.each<Vel, Armor>([&](Vel &v, Armor &a) { v.vx += a.def * 0.0001f; local += 1; });
                sink += local;
            });
        }
        std::ostringstream label;
        label << "frag 7sys mixed (alive=" << alive << ") avg (per hit)";
        printRow(label.str(), total / args.iterations, alive);

        // ============================================================
        // post-defrag 7 systems mixed update (дефрагментация через sort)
        // ============================================================
        double defragTime = timeSeconds([&] { world.defragment(); });
        std::cout << "world.defragment(): " << defragTime * 1000.0 << " ms\n";

        double postDefragTotal = 0.0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            postDefragTotal += timeSeconds([&]
            {
                std::uint64_t local = 0;
                world.each<Pos, Vel>([&](Pos &p, Vel &v) { p.x += v.vx; local += 1; });
                world.each<Pos, Vel, Tag>([&](Pos &, Vel &, Tag &t) { t.v ^= 1u; local += 1; });
                world.each<Health>([&](Health &h) { h.hp -= 0.001f; local += 1; });
                world.each<Health, Armor>([&](Health &h, Armor &a) { h.hp += a.def * 0.0001f; local += 1; });
                world.each<Pos, Tag>([&](Pos &p, Tag &t) { p.y += 0.01f * t.v; local += 1; });
                world.each<Mana>([&](Mana &m) { m.mp -= 0.002f; local += 1; });
                world.each<Vel, Armor>([&](Vel &v, Armor &a) { v.vx += a.def * 0.0001f; local += 1; });
                (void)local; // намеренно не добавляем в sink, чтобы не нарушать совпадение sink
            });
        }
        std::ostringstream defragLabel;
        defragLabel << "post-defrag 7sys mixed (alive=" << alive << ") avg (per hit)";
        printRow(defragLabel.str(), postDefragTotal / args.iterations, alive);
    }

    std::cout << "\nsink=" << sink << "\n";

    // ============================================================
    // Owning group: frag<Pos,Vel> iteration (в sink НЕ входит)
    //
    // Сценарий исключительно для owning-групп (EnTT/PulseECS): у flecs
    // archetype-итерация всегда «в группе», pico/EntityX/gaia аналога не
    // имеют — в сводной таблице у них будет «—».
    // Размещён ПОСЛЕ печати sink и использует собственный rng, поэтому не
    // влияет на контроль эквивалентности (состав мира выше не меняет).
    // ============================================================
    {
        engine::World world;
        std::mt19937 grng{static_cast<std::uint32_t>(args.seed)};

        std::vector<engine::EntityId> eids;
        eids.reserve(args.entities);
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = world.createEntity();
            eids.push_back(e);
            world.addComponent<Pos>(e, Pos{});
            world.addComponent<Vel>(e, Vel{});
        }
        std::shuffle(eids.begin(), eids.end(), grng);
        const std::size_t toDestroy = args.entities * 30u / 100u;
        for (std::size_t i = 0; i < toDestroy; ++i)
            world.destroyEntity(eids[i]);

        const std::size_t toCreate = args.entities * 20u / 100u;
        for (std::size_t i = 0; i < toCreate; ++i)
        {
            auto e = world.createEntity();
            world.addComponent<Pos>(e, Pos{});
            world.addComponent<Vel>(e, Vel{});
        }

        auto grp = world.group<Pos, Vel>();
        const std::size_t gsize = grp.size();
        std::uint64_t gsink = 0;

        double total = 0.0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            total += timeSeconds([&]
            {
                std::uint64_t local = 0;
                grp.each([&](Pos &p, Vel &v) { p.x += v.vx; local += 1; });
                gsink += local;
            });
        }
        std::ostringstream label;
        label << "group<Pos,Vel> frag avg (per hit)";
        printRow(label.str(), total / args.iterations, gsize);
        std::cout << "gsink=" << gsink << "\n";
    }

    // ============================================================
    // frag 7sys + group<Pos,Vel>: тот же смешанный ворклоад из 7 систем
    // в фрагментированном мире, но система Pos+Vel идёт через owning-
    // группу. Показывает ЧАСТИЧНОЕ покрытие: одна система из семи
    // ускоряется до групповой, остальные остаются на sparse-lookup'ах.
    // Группа создаётся после чёрна (arrange пакует пулы), в sink НЕ
    // входит; собственный rng + отдельная контрольная сумма gsink2,
    // которая обязана совпасть с EnTT-зеркалом.
    // ============================================================
    {
        engine::World world;
        std::mt19937 grng{static_cast<std::uint32_t>(args.seed)};

        std::vector<engine::EntityId> eids;
        eids.reserve(args.entities);
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = world.createEntity();
            eids.push_back(e);
            world.addComponent<Pos>(e, Pos{});
            world.addComponent<Vel>(e, Vel{});
            if ((i & 1u) == 0u)  world.addComponent<Tag>(e, Tag{});
            if ((i & 3u) == 0u)  world.addComponent<Health>(e, Health{});
            if ((i & 7u) == 0u)  world.addComponent<Armor>(e, Armor{});
            if ((i & 15u) == 0u) world.addComponent<Mana>(e, Mana{});
            if ((i & 31u) == 0u) world.addComponent<Marker>(e, Marker{});
        }
        std::shuffle(eids.begin(), eids.end(), grng);
        const std::size_t toDestroy = args.entities * 30u / 100u;
        for (std::size_t i = 0; i < toDestroy; ++i)
            world.destroyEntity(eids[i]);

        const std::size_t toCreate = args.entities * 20u / 100u;
        for (std::size_t i = 0; i < toCreate; ++i)
        {
            auto e = world.createEntity();
            world.addComponent<Pos>(e, Pos{});
            world.addComponent<Vel>(e, Vel{});
            if ((i & 1u) == 0u) world.addComponent<Tag>(e, Tag{});
        }

        std::size_t alive = 0;
        world.each<Pos>([&](Pos &) { ++alive; });

        auto grp = world.group<Pos, Vel>();
        std::uint64_t gsink2 = 0;

        // Сценарий шумит на малом числе итераций (памятные размещения/планировщик):
        // минимум 5 прогонов сглаживают выбросы. gsink2 от числа итераций растёт,
        // поэтому эквивалентность сверяется при одинаковом --iterations.
        const std::size_t iters = std::max(args.iterations, std::size_t{5});

        double total = 0.0;
        for (std::size_t it = 0; it < iters; ++it)
        {
            total += timeSeconds([&]
            {
                std::uint64_t local = 0;
                // sys 1: Pos+Vel — через owning-группу
                grp.each([&](Pos &p, Vel &v) { p.x += v.vx; local += 1; });
                // sys 2..7 — обычные each<>
                world.each<Pos, Vel, Tag>([&](Pos &, Vel &, Tag &t) { t.v ^= 1u; local += 1; });
                world.each<Health>([&](Health &h) { h.hp -= 0.001f; local += 1; });
                world.each<Health, Armor>([&](Health &h, Armor &a) { h.hp += a.def * 0.0001f; local += 1; });
                world.each<Pos, Tag>([&](Pos &p, Tag &t) { p.y += 0.01f * t.v; local += 1; });
                world.each<Mana>([&](Mana &m) { m.mp -= 0.002f; local += 1; });
                world.each<Vel, Armor>([&](Vel &v, Armor &a) { v.vx += a.def * 0.0001f; local += 1; });
                gsink2 += local;
            });
        }
        std::ostringstream label;
        label << "frag 7sys + group<Pos,Vel> (alive=" << alive << ") avg (per hit)";
        printRow(label.str(), total / iters, alive);
        std::cout << "gsink2=" << gsink2 << "\n";
    }

    return 0;
}
