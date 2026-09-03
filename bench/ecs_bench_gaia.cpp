// bench/ecs_bench_gaia.cpp
// gaia-ecs v0.8.10 — те же сценарии что и в других бенчмарках.
//
// Особенности API v0.8.10 (проверено компиляцией, README библиотеки местами отстаёт):
//   * итерация только через Query, у World нет each() — w.query().all<...>().each(...)
//   * mutable-доступ требует ссылки в списке: all<Pos&, Vel&>()
//   * исключающий фильтр называется no<T>(), не without<T>() (в README опечатка "not<T>")
//   * get<T>(e) возвращает ССЫЛКУ, не указатель
//   * v0.9+ ломает вариативный all<A&, B&>() — поэтому пин на 0.8.10, см. CMakeLists.txt
//
// Query кэшируется внутри World, первый вызов дороже последующих. Поэтому запросы
// создаются и один раз прогреваются ДО замера — как view в EnTT-бенче.

#include <gaia.h>

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

    std::cout << "ECS benchmark (gaia-ecs)\n";
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
            gaia::ecs::World w;
            total += timeSeconds([&]
            {
                for (std::size_t i = 0; i < args.entities; ++i)
                {
                    auto e = w.add();
                    w.add<Pos>(e, Pos{});
                    w.add<Vel>(e, Vel{});
                    if ((i & 1u) == 0u)
                        w.add<Tag>(e, Tag{});
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
        gaia::ecs::World w;
        std::vector<gaia::ecs::Entity> eids;
        eids.reserve(args.entities);
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = w.add();
            eids.push_back(e);
            w.add<Pos>(e, Pos{});
            w.add<Vel>(e, Vel{});
        }
        // Итерация идёт через Query и eids не использует, но shuffle оставлен намеренно:
        // остальные бенчи тасуют здесь же, и без этого вызова поток rng разъезжается —
        // в фрагментированном сценарии удалялись бы ДРУГИЕ entity, и состав мира
        // перестал бы совпадать с entt/flecs/entityx. Не удалять как "мёртвый код".
        std::shuffle(eids.begin(), eids.end(), rng);

        auto q = w.query().all<Pos &, Vel &>();
        q.each([](Pos &, Vel &) {}); // прогрев кэша запроса

        double total = 0.0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            total += timeSeconds([&]
            {
                std::uint64_t local = 0;
                q.each([&](Pos &p, Vel &v)
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
        gaia::ecs::World w;
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = w.add();
            w.add<Pos>(e, Pos{});
            w.add<Vel>(e, Vel{});
            if ((i & 1u) == 0u)
                w.add<Tag>(e, Tag{static_cast<std::uint32_t>(i)});
        }

        auto q = w.query().all<Pos &, Vel &, Tag &>();
        q.each([](Pos &, Vel &, Tag &) {});

        double total = 0.0;
        std::size_t hits = 0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            total += timeSeconds([&]
            {
                std::uint64_t local = 0;
                std::size_t h = 0;
                q.each([&](Pos &p, Vel &v, Tag &t)
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
    // query<4> without<ExtraTag>  →  all<4>().no<ExtraTag>()
    // ============================================================
    {
        gaia::ecs::World w;
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = w.add();
            w.add<ReqA>(e, ReqA{static_cast<float>(i)});
            w.add<ReqB>(e, ReqB{static_cast<float>(i)});
            w.add<ReqC>(e, ReqC{static_cast<float>(i)});
            w.add<ReqD>(e, ReqD{static_cast<float>(i)});
            if ((i & 7u) == 0u)
                w.add<ExtraTag>(e, ExtraTag{});
        }

        auto q = w.query().all<ReqA &, ReqB &, ReqC &, ReqD &>().no<ExtraTag>();
        q.each([](ReqA &, ReqB &, ReqC &, ReqD &) {});

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
            gaia::ecs::World w;
            std::vector<gaia::ecs::Entity> eids;
            eids.reserve(args.entities);
            for (std::size_t i = 0; i < args.entities; ++i)
            {
                auto e = w.add();
                eids.push_back(e);
                w.add<Pos>(e, Pos{});
                w.add<Vel>(e, Vel{});
            }
            std::shuffle(eids.begin(), eids.end(), rng);

            total += timeSeconds([&]
            {
                for (std::size_t i = 0; i < args.entities; ++i)
                    w.del(eids[i]);
                ops = args.entities;
            });
        }
        printRow("destroyEntity avg", total / args.iterations, ops);
    }

    // ============================================================
    // get<Pos> — возвращает ссылку, не указатель
    // ============================================================
    {
        gaia::ecs::World w;
        std::vector<gaia::ecs::Entity> eids;
        eids.reserve(args.entities);
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = w.add();
            eids.push_back(e);
            w.add<Pos>(e, Pos{static_cast<float>(i), static_cast<float>(i)});
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
                    const auto &p = w.get<Pos>(e);
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
        gaia::ecs::World w;
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = w.add();
            w.add<Pos>(e, Pos{static_cast<float>(i), 0.0f});
            w.add<Vel>(e, Vel{1.0f, 0.0f});
            if ((i & 1u) == 0u) w.add<Tag>(e, Tag{static_cast<std::uint32_t>(i)});
            if ((i & 3u) == 0u) w.add<Health>(e, Health{100.0f});
            if ((i & 7u) == 0u) w.add<Armor>(e, Armor{50.0f});
            if ((i & 15u) == 0u) w.add<Mana>(e, Mana{20.0f});
        }

        auto q1 = w.query().all<Pos &, Vel &>();
        auto q2 = w.query().all<Pos &, Vel &, Tag &>();
        auto q3 = w.query().all<Health &>();
        auto q4 = w.query().all<Health &, Armor &>();
        auto q5 = w.query().all<Pos &, Tag &>();
        auto q6 = w.query().all<Mana &>();
        auto q7 = w.query().all<Vel &, Armor &>();

        // прогрев всех запросов
        q1.each([](Pos &, Vel &) {});
        q2.each([](Pos &, Vel &, Tag &) {});
        q3.each([](Health &) {});
        q4.each([](Health &, Armor &) {});
        q5.each([](Pos &, Tag &) {});
        q6.each([](Mana &) {});
        q7.each([](Vel &, Armor &) {});

        double total = 0.0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            total += timeSeconds([&]
            {
                std::uint64_t local = 0;

                q1.each([&](Pos &p, Vel &v) { p.x += v.vx; p.y += v.vy; local += 1; });
                q2.each([&](Pos &p, Vel &v, Tag &t) { p.x += v.vx * 0.5f; t.v ^= 1u; local += 1; });
                q3.each([&](Health &h) { h.hp -= 0.001f; local += 1; });
                q4.each([&](Health &h, Armor &a) { h.hp += a.def * 0.0001f; local += 1; });
                q5.each([&](Pos &p, Tag &t) { p.y += 0.01f * t.v; local += 1; });
                q6.each([&](Mana &m) { m.mp -= 0.002f; local += 1; });
                q7.each([&](Vel &v, Armor &a) { v.vx += a.def * 0.0001f; local += 1; });

                sink += local;
            });
        }
        printRow("7 systems mixed avg (per hit)", total / args.iterations, args.entities);
    }

    // ============================================================
    // Fragmented world
    // ============================================================
    {
        gaia::ecs::World w;
        std::vector<gaia::ecs::Entity> initial;
        initial.reserve(args.entities);
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            auto e = w.add();
            initial.push_back(e);
            w.add<Pos>(e, Pos{});
            w.add<Vel>(e, Vel{});
            if ((i & 1u) == 0u) w.add<Tag>(e, Tag{});
            if ((i & 3u) == 0u) w.add<Health>(e, Health{});
            if ((i & 7u) == 0u) w.add<Armor>(e, Armor{});
            if ((i & 15u) == 0u) w.add<Mana>(e, Mana{});
            if ((i & 31u) == 0u) w.add<Marker>(e, Marker{});
        }
        std::shuffle(initial.begin(), initial.end(), rng);
        const std::size_t toDestroy = args.entities * 30u / 100u;
        for (std::size_t i = 0; i < toDestroy; ++i)
            w.del(initial[i]);

        const std::size_t toCreate = args.entities * 20u / 100u;
        for (std::size_t i = 0; i < toCreate; ++i)
        {
            auto e = w.add();
            w.add<Pos>(e, Pos{});
            w.add<Vel>(e, Vel{});
            if ((i & 1u) == 0u) w.add<Tag>(e, Tag{});
        }

        auto q1 = w.query().all<Pos &, Vel &>();
        auto q2 = w.query().all<Pos &, Vel &, Tag &>();
        auto q3 = w.query().all<Health &>();
        auto q4 = w.query().all<Health &, Armor &>();
        auto q5 = w.query().all<Pos &, Tag &>();
        auto q6 = w.query().all<Mana &>();
        auto q7 = w.query().all<Vel &, Armor &>();

        q1.each([](Pos &, Vel &) {});
        q2.each([](Pos &, Vel &, Tag &) {});
        q3.each([](Health &) {});
        q4.each([](Health &, Armor &) {});
        q5.each([](Pos &, Tag &) {});
        q6.each([](Mana &) {});
        q7.each([](Vel &, Armor &) {});

        const std::size_t alive = w.query().all<Pos &>().count();

        double total = 0.0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            total += timeSeconds([&]
            {
                std::uint64_t local = 0;
                q1.each([&](Pos &p, Vel &v) { p.x += v.vx; local += 1; });
                q2.each([&](Pos &, Vel &, Tag &t) { t.v ^= 1u; local += 1; });
                q3.each([&](Health &h) { h.hp -= 0.001f; local += 1; });
                q4.each([&](Health &h, Armor &a) { h.hp += a.def * 0.0001f; local += 1; });
                q5.each([&](Pos &p, Tag &t) { p.y += 0.01f * t.v; local += 1; });
                q6.each([&](Mana &m) { m.mp -= 0.002f; local += 1; });
                q7.each([&](Vel &v, Armor &a) { v.vx += a.def * 0.0001f; local += 1; });
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
