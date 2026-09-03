// bench/ecs_bench_pico.cpp
// pico_ecs (empyreanx/pico_headers, main) — те же сценарии что и в других бенчмарках.
//
// КРИТИЧНО про модель pico_ecs (проверено экспериментально):
// Система хранит собственный sparse set подходящих entity. Он наполняется только
// внутри ecs_add → ecs_sync_add_remove, то есть В МОМЕНТ добавления компонента.
// Система, определённая ПОСЛЕ создания entity, остаётся пустой навсегда:
// ecs_get_entity_count вернёт 0, а ecs_run_system не вызовет callback ни разу.
// Это не баг библиотеки, а её инкрементальная модель — поэтому во всех сценариях
// ниже ecs_define_system + ecs_require/ecs_exclude идут ДО цикла создания entity.
//
// Исключающий фильтр делается нативно через ecs_exclude, ручной ecs_has-фильтр
// внутри callback не нужен.
//
// Замечание про «add components»: там системы намеренно НЕ определяются, чтобы
// мерить чистую стоимость вставки компонента (сравнимо с entt emplace). В реальном
// коде pico_ecs ecs_add дополнительно платит O(число систем) на sync sparse set.

#define PICO_ECS_IMPLEMENTATION
#include "pico_ecs.h"

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

    struct CompReg
    {
        ecs_comp_t Pos{};
        ecs_comp_t Vel{};
        ecs_comp_t Tag{};
        ecs_comp_t ExtraTag{};
        ecs_comp_t ReqA{}, ReqB{}, ReqC{}, ReqD{};
        ecs_comp_t Health{}, Armor{}, Mana{}, Marker{};
    };
    CompReg CR;

    void registerComps(ecs_t *e)
    {
        CR.Pos      = ecs_define_component(e, sizeof(Pos), nullptr);
        CR.Vel      = ecs_define_component(e, sizeof(Vel), nullptr);
        CR.Tag      = ecs_define_component(e, sizeof(Tag), nullptr);
        CR.ExtraTag = ecs_define_component(e, sizeof(ExtraTag), nullptr);
        CR.ReqA     = ecs_define_component(e, sizeof(ReqA), nullptr);
        CR.ReqB     = ecs_define_component(e, sizeof(ReqB), nullptr);
        CR.ReqC     = ecs_define_component(e, sizeof(ReqC), nullptr);
        CR.ReqD     = ecs_define_component(e, sizeof(ReqD), nullptr);
        CR.Health   = ecs_define_component(e, sizeof(Health), nullptr);
        CR.Armor    = ecs_define_component(e, sizeof(Armor), nullptr);
        CR.Mana     = ecs_define_component(e, sizeof(Mana), nullptr);
        CR.Marker   = ecs_define_component(e, sizeof(Marker), nullptr);
    }

    // Счётчик, обновляемый внутри system callbacks (аналог local в других бенчах).
    std::uint64_t g_local = 0;

    // --- callbacks для 7 систем ---

    ecs_ret_t sys_pv_cb(ecs_t *e, ecs_entity_t *ents, size_t count, void *udata)
    {
        (void)udata;
        std::uint64_t local = 0;
        for (size_t i = 0; i < count; ++i)
        {
            Pos *p = (Pos *)ecs_get(e, ents[i], CR.Pos);
            Vel *v = (Vel *)ecs_get(e, ents[i], CR.Vel);
            p->x += v->vx * 0.001f;
            p->y += v->vy * 0.001f;
            ++local;
        }
        g_local += local;
        return 0;
    }

    ecs_ret_t sys_pvt_cb(ecs_t *e, ecs_entity_t *ents, size_t count, void *udata)
    {
        (void)udata;
        std::uint64_t local = 0;
        for (size_t i = 0; i < count; ++i)
        {
            Pos *p = (Pos *)ecs_get(e, ents[i], CR.Pos);
            Vel *v = (Vel *)ecs_get(e, ents[i], CR.Vel);
            Tag *t = (Tag *)ecs_get(e, ents[i], CR.Tag);
            p->x += v->vx * 0.001f;
            p->y += v->vy * 0.001f;
            t->v = (t->v + 1u) & 0xFFu;
            ++local;
        }
        g_local += local;
        return 0;
    }

    // require<ReqA..ReqD> + exclude<ExtraTag> — фильтрация нативная, ecs_has не нужен.
    ecs_ret_t sys_abcd_no_x_cb(ecs_t *e, ecs_entity_t *ents, size_t count, void *udata)
    {
        (void)udata;
        std::uint64_t local = 0;
        for (size_t i = 0; i < count; ++i)
        {
            ReqA *a = (ReqA *)ecs_get(e, ents[i], CR.ReqA);
            ReqB *b = (ReqB *)ecs_get(e, ents[i], CR.ReqB);
            ReqC *c = (ReqC *)ecs_get(e, ents[i], CR.ReqC);
            ReqD *d = (ReqD *)ecs_get(e, ents[i], CR.ReqD);
            a->w += b->w + c->w + d->w;
            ++local;
        }
        g_local += local;
        return 0;
    }

    ecs_ret_t sys_health_cb(ecs_t *e, ecs_entity_t *ents, size_t count, void *udata)
    {
        (void)udata;
        std::uint64_t local = 0;
        for (size_t i = 0; i < count; ++i)
        {
            Health *h = (Health *)ecs_get(e, ents[i], CR.Health);
            h->hp -= 0.001f;
            ++local;
        }
        g_local += local;
        return 0;
    }

    ecs_ret_t sys_health_armor_cb(ecs_t *e, ecs_entity_t *ents, size_t count, void *udata)
    {
        (void)udata;
        std::uint64_t local = 0;
        for (size_t i = 0; i < count; ++i)
        {
            Health *h = (Health *)ecs_get(e, ents[i], CR.Health);
            Armor *a = (Armor *)ecs_get(e, ents[i], CR.Armor);
            h->hp += a->def * 0.0001f;
            ++local;
        }
        g_local += local;
        return 0;
    }

    ecs_ret_t sys_pos_tag_cb(ecs_t *e, ecs_entity_t *ents, size_t count, void *udata)
    {
        (void)udata;
        std::uint64_t local = 0;
        for (size_t i = 0; i < count; ++i)
        {
            Pos *p = (Pos *)ecs_get(e, ents[i], CR.Pos);
            Tag *t = (Tag *)ecs_get(e, ents[i], CR.Tag);
            p->y += 0.01f * static_cast<float>(t->v);
            ++local;
        }
        g_local += local;
        return 0;
    }

    ecs_ret_t sys_mana_cb(ecs_t *e, ecs_entity_t *ents, size_t count, void *udata)
    {
        (void)udata;
        std::uint64_t local = 0;
        for (size_t i = 0; i < count; ++i)
        {
            Mana *m = (Mana *)ecs_get(e, ents[i], CR.Mana);
            m->mp -= 0.002f;
            ++local;
        }
        g_local += local;
        return 0;
    }

    ecs_ret_t sys_vel_armor_cb(ecs_t *e, ecs_entity_t *ents, size_t count, void *udata)
    {
        (void)udata;
        std::uint64_t local = 0;
        for (size_t i = 0; i < count; ++i)
        {
            Vel   *v = (Vel *)ecs_get(e, ents[i], CR.Vel);
            Armor *a = (Armor *)ecs_get(e, ents[i], CR.Armor);
            v->vx += a->def * 0.0001f;
            ++local;
        }
        g_local += local;
        return 0;
    }
}

int main(int argc, char **argv)
{
    const Args args = parseArgs(argc, argv);

    std::cout << "ECS benchmark (pico_ecs)\n";
    std::cout << "  entities   : " << args.entities << "\n";
    std::cout << "  iterations : " << args.iterations << "\n";
    std::cout << "  seed       : " << args.seed << "\n\n";

    std::mt19937 rng(static_cast<std::uint32_t>(args.seed));
    std::uint64_t sink = 0;

    const ecs_mask_t ALL = ~(ecs_mask_t)0;

    // Запас ёмкости: фрагментированный сценарий создаёт до entities + 20%.
    // Без запаса ecs_create удваивает массив на ходу и в замер попадает realloc.
    const std::size_t CAP = args.entities + args.entities / 4u + 16u;

    // ============================================================
    // add components (Pos,Vel + 50% Tag) — без систем, чистая вставка
    // ============================================================
    {
        double total = 0.0;
        std::size_t ops = 0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            ecs_t *e = ecs_new(CAP, nullptr);
            registerComps(e);

            total += timeSeconds([&]
            {
                for (std::size_t i = 0; i < args.entities; ++i)
                {
                    ecs_entity_t ent = ecs_create(e);
                    ecs_add(e, ent, CR.Pos, nullptr);
                    ecs_add(e, ent, CR.Vel, nullptr);
                    if ((i & 1u) == 0u)
                        ecs_add(e, ent, CR.Tag, nullptr);
                }
                ops = args.entities * 2u + (args.entities / 2u);
            });

            ecs_free(e);
        }
        printRow("add components (Pos,Vel + 50% Tag) avg", total / args.iterations, ops);
    }

    // ============================================================
    // each<Pos, Vel>
    // ============================================================
    {
        ecs_t *e = ecs_new(CAP, nullptr);
        registerComps(e);

        // Система — ДО создания entity, иначе её sparse set останется пустым.
        ecs_system_t sys = ecs_define_system(e, sys_pv_cb, nullptr);
        ecs_require(e, sys, CR.Pos);
        ecs_require(e, sys, CR.Vel);

        std::vector<ecs_entity_t> eids;
        eids.reserve(args.entities);
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            ecs_entity_t ent = ecs_create(e);
            eids.push_back(ent);
            ecs_add(e, ent, CR.Pos, nullptr);
            ecs_add(e, ent, CR.Vel, nullptr);
        }
        // Итерация идёт через ecs_run_system и eids не использует, но shuffle оставлен
        // намеренно: остальные бенчи тасуют здесь же, и без этого вызова поток rng
        // разъезжается — в фрагментированном сценарии удалялись бы ДРУГИЕ entity,
        // и состав мира перестал бы совпадать с entt/flecs/entityx/gaia.
        // Не удалять как "мёртвый код".
        std::shuffle(eids.begin(), eids.end(), rng);

        const std::size_t hits = ecs_get_entity_count(e, sys);
        double total = 0.0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            g_local = 0;
            total += timeSeconds([&] { ecs_run_system(e, sys, ALL); });
            sink += g_local;
        }
        printRow("each<Pos,Vel> avg (per hit)", total / args.iterations, hits);
        ecs_free(e);
    }

    // ============================================================
    // each<Pos, Vel, Tag> (Tag у 50%)
    // ============================================================
    {
        ecs_t *e = ecs_new(CAP, nullptr);
        registerComps(e);

        ecs_system_t sys = ecs_define_system(e, sys_pvt_cb, nullptr);
        ecs_require(e, sys, CR.Pos);
        ecs_require(e, sys, CR.Vel);
        ecs_require(e, sys, CR.Tag);

        for (std::size_t i = 0; i < args.entities; ++i)
        {
            ecs_entity_t ent = ecs_create(e);
            ecs_add(e, ent, CR.Pos, nullptr);
            ecs_add(e, ent, CR.Vel, nullptr);
            if ((i & 1u) == 0u)
                ecs_add(e, ent, CR.Tag, nullptr);
        }

        const std::size_t hits = ecs_get_entity_count(e, sys);
        double total = 0.0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            g_local = 0;
            total += timeSeconds([&] { ecs_run_system(e, sys, ALL); });
            sink += g_local;
        }
        printRow("each<Pos,Vel,Tag> avg (per hit)", total / args.iterations, hits);
        ecs_free(e);
    }

    // ============================================================
    // query<4> without<ExtraTag> → require x4 + ecs_exclude
    // ============================================================
    {
        ecs_t *e = ecs_new(CAP, nullptr);
        registerComps(e);

        ecs_system_t sys = ecs_define_system(e, sys_abcd_no_x_cb, nullptr);
        ecs_require(e, sys, CR.ReqA);
        ecs_require(e, sys, CR.ReqB);
        ecs_require(e, sys, CR.ReqC);
        ecs_require(e, sys, CR.ReqD);
        ecs_exclude(e, sys, CR.ExtraTag);

        for (std::size_t i = 0; i < args.entities; ++i)
        {
            ecs_entity_t ent = ecs_create(e);
            ecs_add(e, ent, CR.ReqA, nullptr);
            ecs_add(e, ent, CR.ReqB, nullptr);
            ecs_add(e, ent, CR.ReqC, nullptr);
            ecs_add(e, ent, CR.ReqD, nullptr);
            if ((i & 7u) == 0u)
                ecs_add(e, ent, CR.ExtraTag, nullptr);
        }

        const std::size_t hits = ecs_get_entity_count(e, sys);
        double total = 0.0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            g_local = 0;
            total += timeSeconds([&] { ecs_run_system(e, sys, ALL); });
            sink += g_local;
        }
        printRow("query<ReqA,B,C,D> without<ExtraTag> avg (per hit)", total / args.iterations, hits);
        ecs_free(e);
    }

    // ============================================================
    // Destroy entity
    // ============================================================
    {
        double total = 0.0;
        std::size_t ops = 0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            ecs_t *e = ecs_new(CAP, nullptr);
            registerComps(e);
            std::vector<ecs_entity_t> eids;
            eids.reserve(args.entities);
            for (std::size_t i = 0; i < args.entities; ++i)
            {
                ecs_entity_t ent = ecs_create(e);
                eids.push_back(ent);
                ecs_add(e, ent, CR.Pos, nullptr);
                ecs_add(e, ent, CR.Vel, nullptr);
            }
            std::shuffle(eids.begin(), eids.end(), rng);

            total += timeSeconds([&]
            {
                for (std::size_t i = 0; i < args.entities; ++i)
                    ecs_destroy(e, eids[i]);
                ops = args.entities;
            });

            ecs_free(e);
        }
        printRow("destroyEntity avg", total / args.iterations, ops);
    }

    // ============================================================
    // get<Pos>
    // ============================================================
    {
        ecs_t *e = ecs_new(CAP, nullptr);
        registerComps(e);
        std::vector<ecs_entity_t> eids;
        eids.reserve(args.entities);
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            ecs_entity_t ent = ecs_create(e);
            eids.push_back(ent);
            Pos pp{static_cast<float>(i), static_cast<float>(i)};
            ecs_add(e, ent, CR.Pos, nullptr);
            ecs_set(e, ent, CR.Pos, &pp);
        }
        std::shuffle(eids.begin(), eids.end(), rng);

        double total = 0.0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            total += timeSeconds([&]
            {
                std::uint64_t local = 0;
                for (auto ent : eids)
                {
                    auto *p = (Pos *)ecs_get(e, ent, CR.Pos);
                    if (p) local += static_cast<std::uint64_t>(p->x + p->y);
                }
                sink += local;
            });
        }
        printRow("get<Pos> avg (per hit)", total / args.iterations, args.entities);
        ecs_free(e);
    }

    // ============================================================
    // 7 systems mixed update
    // ============================================================
    {
        ecs_t *e = ecs_new(CAP, nullptr);
        registerComps(e);

        ecs_system_t s1 = ecs_define_system(e, sys_pv_cb, nullptr);
        ecs_require(e, s1, CR.Pos); ecs_require(e, s1, CR.Vel);
        ecs_system_t s2 = ecs_define_system(e, sys_pvt_cb, nullptr);
        ecs_require(e, s2, CR.Pos); ecs_require(e, s2, CR.Vel); ecs_require(e, s2, CR.Tag);
        ecs_system_t s3 = ecs_define_system(e, sys_health_cb, nullptr);
        ecs_require(e, s3, CR.Health);
        ecs_system_t s4 = ecs_define_system(e, sys_health_armor_cb, nullptr);
        ecs_require(e, s4, CR.Health); ecs_require(e, s4, CR.Armor);
        ecs_system_t s5 = ecs_define_system(e, sys_pos_tag_cb, nullptr);
        ecs_require(e, s5, CR.Pos); ecs_require(e, s5, CR.Tag);
        ecs_system_t s6 = ecs_define_system(e, sys_mana_cb, nullptr);
        ecs_require(e, s6, CR.Mana);
        ecs_system_t s7 = ecs_define_system(e, sys_vel_armor_cb, nullptr);
        ecs_require(e, s7, CR.Vel); ecs_require(e, s7, CR.Armor);

        for (std::size_t i = 0; i < args.entities; ++i)
        {
            ecs_entity_t ent = ecs_create(e);
            ecs_add(e, ent, CR.Pos, nullptr);
            ecs_add(e, ent, CR.Vel, nullptr);
            if ((i & 1u) == 0u)  ecs_add(e, ent, CR.Tag, nullptr);
            if ((i & 3u) == 0u)  ecs_add(e, ent, CR.Health, nullptr);
            if ((i & 7u) == 0u)  ecs_add(e, ent, CR.Armor, nullptr);
            if ((i & 15u) == 0u) ecs_add(e, ent, CR.Mana, nullptr);
        }

        const std::size_t alive = ecs_get_entity_count(e, s1);
        double total = 0.0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            g_local = 0;
            total += timeSeconds([&]
            {
                ecs_run_system(e, s1, ALL);
                ecs_run_system(e, s2, ALL);
                ecs_run_system(e, s3, ALL);
                ecs_run_system(e, s4, ALL);
                ecs_run_system(e, s5, ALL);
                ecs_run_system(e, s6, ALL);
                ecs_run_system(e, s7, ALL);
            });
            sink += g_local;
        }
        printRow("7 systems mixed avg (per hit)", total / args.iterations, alive);
        ecs_free(e);
    }

    // ============================================================
    // Fragmented world
    // ============================================================
    {
        ecs_t *e = ecs_new(CAP, nullptr);
        registerComps(e);

        ecs_system_t s1 = ecs_define_system(e, sys_pv_cb, nullptr);
        ecs_require(e, s1, CR.Pos); ecs_require(e, s1, CR.Vel);
        ecs_system_t s2 = ecs_define_system(e, sys_pvt_cb, nullptr);
        ecs_require(e, s2, CR.Pos); ecs_require(e, s2, CR.Vel); ecs_require(e, s2, CR.Tag);
        ecs_system_t s3 = ecs_define_system(e, sys_health_cb, nullptr);
        ecs_require(e, s3, CR.Health);
        ecs_system_t s4 = ecs_define_system(e, sys_health_armor_cb, nullptr);
        ecs_require(e, s4, CR.Health); ecs_require(e, s4, CR.Armor);
        ecs_system_t s5 = ecs_define_system(e, sys_pos_tag_cb, nullptr);
        ecs_require(e, s5, CR.Pos); ecs_require(e, s5, CR.Tag);
        ecs_system_t s6 = ecs_define_system(e, sys_mana_cb, nullptr);
        ecs_require(e, s6, CR.Mana);
        ecs_system_t s7 = ecs_define_system(e, sys_vel_armor_cb, nullptr);
        ecs_require(e, s7, CR.Vel); ecs_require(e, s7, CR.Armor);

        std::vector<ecs_entity_t> initial;
        initial.reserve(args.entities);
        for (std::size_t i = 0; i < args.entities; ++i)
        {
            ecs_entity_t ent = ecs_create(e);
            initial.push_back(ent);
            ecs_add(e, ent, CR.Pos, nullptr);
            ecs_add(e, ent, CR.Vel, nullptr);
            if ((i & 1u) == 0u)  ecs_add(e, ent, CR.Tag, nullptr);
            if ((i & 3u) == 0u)  ecs_add(e, ent, CR.Health, nullptr);
            if ((i & 7u) == 0u)  ecs_add(e, ent, CR.Armor, nullptr);
            if ((i & 15u) == 0u) ecs_add(e, ent, CR.Mana, nullptr);
            if ((i & 31u) == 0u) ecs_add(e, ent, CR.Marker, nullptr);
        }
        std::shuffle(initial.begin(), initial.end(), rng);
        const std::size_t toDestroy = args.entities * 30u / 100u;
        for (std::size_t i = 0; i < toDestroy; ++i)
            ecs_destroy(e, initial[i]);

        const std::size_t toCreate = args.entities * 20u / 100u;
        for (std::size_t i = 0; i < toCreate; ++i)
        {
            ecs_entity_t ent = ecs_create(e);
            ecs_add(e, ent, CR.Pos, nullptr);
            ecs_add(e, ent, CR.Vel, nullptr);
            if ((i & 1u) == 0u) ecs_add(e, ent, CR.Tag, nullptr);
        }

        const std::size_t alive = ecs_get_entity_count(e, s1);
        double total = 0.0;
        for (std::size_t it = 0; it < args.iterations; ++it)
        {
            g_local = 0;
            total += timeSeconds([&]
            {
                ecs_run_system(e, s1, ALL);
                ecs_run_system(e, s2, ALL);
                ecs_run_system(e, s3, ALL);
                ecs_run_system(e, s4, ALL);
                ecs_run_system(e, s5, ALL);
                ecs_run_system(e, s6, ALL);
                ecs_run_system(e, s7, ALL);
            });
            sink += g_local;
        }
        std::ostringstream label;
        label << "frag 7sys mixed (alive=" << alive << ") avg (per hit)";
        printRow(label.str(), total / args.iterations, alive);
        ecs_free(e);
    }

    std::cout << "\nsink=" << sink << "\n";
    return 0;
}
