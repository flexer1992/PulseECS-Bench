#pragma once

#include "engine/ecs/EntityManager.hpp"
#include "engine/ecs/SparseSet.hpp"
#include "engine/ecs/systems/ISystem.hpp"
#include "engine/tools/profiler/Profiler.hpp"
#include "engine/core/Logger.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <tuple>
#include <typeindex>
#include <functional>
#include <utility>
#include <cctype>
#include <type_traits>

namespace engine
{
    // ==================== Структуры для статистики ====================

    // ==================== Структуры для статистики ====================
    struct ContainerStats
    {
        std::string typeName;
        size_t count;
        size_t memoryBytes;
    };

    // Хелпер для получения читаемого имени типа из mangled имени
    template <typename T>
    inline std::string getTypeName()
    {
        std::string name = typeid(T).name();
        std::string result;
        size_t i = 0;
        while (i < name.size())
        {
            if (std::isdigit(name[i]))
            {
                size_t numLen = 0;
                while (i + numLen < name.size() && std::isdigit(name[i + numLen]))
                    numLen++;
                std::string numStr = name.substr(i, numLen);
                size_t num = std::stoul(numStr);
                i += numLen;

                if (i + num <= name.size() && num > 0)
                {
                    if (!result.empty())
                        result += "::";
                    result += name.substr(i, num);
                    i += num;
                }
            }
            else if (name[i] == 'E')
            {
                break;
            }
            else if (name[i] == 'N')
            {
                i++;
            }
            else
            {
                i++;
            }
        }
        return result.empty() ? name : result;
    }

    // ==================== Класс мира ====================
    namespace detail
    {
        // Счётчик типов компонентов вынесен из World намеренно.
        // Раньше id выдавал function-local static внутри World::getTypeId<T>():
        //     static const size_t id = nextComponentId_++;
        // Такой static требует проверки thread-safe guard на КАЖДОМ вызове, а
        // getTypeId дёргается из getContainer, то есть из каждого add/get/each.
        // Variable template инициализируется один раз до main, чтение — обычная
        // загрузка без барьеров.
        inline std::size_t g_nextComponentId = 0;

        template <typename T>
        inline const std::size_t componentTypeId = g_nextComponentId++;
    }

    class World
    {
    private:
        EntityManager entities_;
        // Основной массив контейнеров
        std::vector<std::unique_ptr<ISparseSet>> containers_;
        // Для removers можно сделать так же, или использовать лямбды с захватом указателя
        std::vector<std::function<void(EntityId)>> removers_;

        // Битовая маска типов компонентов на сущность: бит i означает "есть компонент
        // с typeId == i". Нужна только для destroyEntity — раньше он звал ВСЕ removers
        // подряд, то есть делал по одному std::function-вызову на каждый
        // зарегистрированный тип. В проекте типов ~40, а у сущности их обычно 2-4,
        // так что ~37 вызовов из 40 были впустую: 127 ns/op против 15 ns/op.
        std::vector<std::uint64_t> compMask_;

        // В маску влезает kMaskBits типов. Если типов станет больше, маска перестаёт
        // описывать мир целиком — тогда честно откатываемся на полный обход removers,
        // вместо того чтобы молча терять компоненты при удалении сущности.
        static constexpr size_t kMaskBits = 64;
        bool maskUsable_ = true;

        void markComponentBit(EntityId id, size_t typeId)
        {
            if (typeId >= kMaskBits)
            {
                maskUsable_ = false;
                return;
            }
            if (id >= compMask_.size())
            {
                const size_t newCap = std::max(static_cast<size_t>(id + 1), compMask_.size() * 2);
                compMask_.resize(newCap, 0);
            }
            compMask_[id] |= (std::uint64_t(1) << typeId);
        }
        // Опционально: имена типов (для дебага / профилирования)
        std::vector<std::string> typeNames_;

        template <typename T>
        size_t getTypeId() const
        {
            return detail::componentTypeId<T>;
        }

        // Forward declaration — World не знает что такое Scene
        void *_ownerScene = nullptr;

        // ==================== Systems (из SystemManager) ====================
        std::vector<std::unique_ptr<ISystem>> systems_;
        bool systemsSorted_ = false;

    public:
        // ===== Scenes ======

        void setOwnerScene(void *scenePtr) { _ownerScene = scenePtr; }
        void *getOwnerScene() const { return _ownerScene; }

        template <typename T>
        T *getScene() { return static_cast<T *>(_ownerScene); }

        // ==================== Сущности ====================

        EntityId createEntity()
        {
            return entities_.create();
        }

        /// @brief Пакетное создание N сущностей
        std::vector<EntityId> createEntities(size_t count)
        {
            return entities_.createBulk(count);
        }

        /// @brief Пакетное создание N сущностей с записью в итератор
        template <typename OutputIt>
        void createEntities(size_t count, OutputIt out)
        {
            entities_.create(count, out);
        }

        /// @brief Предвыделение памяти под сущности и маски компонентов
        void reserveEntities(size_t count)
        {
            if (count > compMask_.size())
                compMask_.resize(count, 0);
        }

        void destroyEntity(EntityId id)
        {
            if (!entities_.isAlive(id))
                return;

            if (maskUsable_)
            {
                // Быстрый путь: зовём removers только для реально имеющихся компонентов.
                if (id < compMask_.size())
                {
                    std::uint64_t m = compMask_[id];
                    while (m != 0)
                    {
                        const size_t bit = static_cast<size_t>(std::countr_zero(m));
                        m &= (m - 1); // сбросить младший установленный бит
                        if (bit < removers_.size() && removers_[bit])
                            removers_[bit](id);
                    }
                    compMask_[id] = 0;
                }
            }
            else
            {
                // Типов больше kMaskBits — маска неполная, идём старым путём.
                for (size_t i = 0; i < removers_.size(); ++i)
                {
                    if (removers_[i])
                        removers_[i](id);
                }
                if (id < compMask_.size())
                    compMask_[id] = 0;
            }

            entities_.destroy(id);
        }

        bool isAlive(EntityId id) const
        {
            return entities_.isAlive(id);
        }

        size_t entityCount() const
        {
            return entities_.count();
        }

        /// @brief Число слотов id (макс. индекс + 1). Для векторов, индексируемых по EntityId.
        size_t entitySlotCount() const { return entities_.slotCount(); }

        // ==================== Компоненты ====================

    private:
        // Слим-доступ к контейнеру для ТОЧЕЧНЫХ обращений (getComponent/hasComponent/
        // exclude). Чтение НЕ создаёт контейнер: нет контейнера — нет компонента.
        //
        // Почему так: раньше здесь была ветка ленивого создания контейнера, пусть и
        // вынесенная в noinline-функцию. Но сам факт потенциального call в теле —
        // барьер по памяти для оптимизатора: call теоретически может изменить
        // containers_/sparse_/dense_, поэтому в цикле getComponent компилятор
        // перечитывал всю зависимую цепочку указателей на каждой итерации и не мог
        // выносить инварианты. Замерено: get<Pos> при 4M сущностей — 15.0 ns/op
        // с веткой создания против 6.5 ns/op без неё (2.3×). Версия для мутирующих
        // операций (addComponent/each) — getContainer ниже.
        template <typename T>
        SparseSet<T> *findContainer()
        {
            const size_t id = getTypeId<T>();

            if (id < containers_.size())
            {
                if (auto *p = containers_[id].get())
                    return static_cast<SparseSet<T> *>(p);
            }

            return nullptr;
        }

        template <typename T>
        const SparseSet<T> *findContainer() const
        {
            const size_t id = getTypeId<T>();

            if (id < containers_.size())
            {
                if (auto *p = containers_[id].get())
                    return static_cast<const SparseSet<T> *>(p);
            }

            return nullptr;
        }

    public:
        // Полный доступ к контейнеру. Намеренно оставлен инлайн-версией без
        // вызовов: each<> дёргает его один раз на запрос, и лишний вызов внутри
        // мешал бы компилятору оптимизировать саму итерацию (проверено: -12%).
        template <typename T>
        SparseSet<T> &getContainer()
        {
            const size_t id = getTypeId<T>();

            if (id >= containers_.size())
            {
                containers_.resize(id + 1);
                removers_.resize(id + 1);
                typeNames_.resize(id + 1);
            }

            if (!containers_[id])
            {
                containers_[id] = std::make_unique<SparseSet<T>>();

                removers_[id] = [ptr = containers_[id].get()](EntityId eid)
                {
                    static_cast<SparseSet<T> *>(ptr)->remove(eid);
                };

                typeNames_[id] = getTypeName<T>();
            }

            return *static_cast<SparseSet<T> *>(containers_[id].get());
        }

        template <typename T>
        void reserve(size_t n) { getContainer<T>().reserve(n); }

        template <typename T, typename... Args>
        T &addComponent(EntityId id, Args &&...args)
        {
            assert(entities_.isAlive(id) && "Entity must be alive!");
            auto &container = getContainer<T>();
            markComponentBit(id, getTypeId<T>());
            return container.create(id, std::forward<Args>(args)...);
        }

        template <typename T>
        T *getComponent(EntityId id)
        {
            auto *c = findContainer<T>();
            return c ? c->get(id) : nullptr;
        }

        template <typename T>
        const T *getComponent(EntityId id) const
        {
            const auto *c = findContainer<T>();
            return c ? c->get(id) : nullptr;
        }

        template <typename T, typename... Args>
        T &getOrCreateComponent(EntityId id, Args &&...args)
        {
            assert(entities_.isAlive(id) && "Entity must be alive!");
            auto &container = getContainer<T>();
            if (auto *existing = container.get(id))
            {
                return *existing;
            }
            markComponentBit(id, getTypeId<T>());
            return container.create(id, std::forward<Args>(args)...);
        }

        template <typename T>
        bool hasComponent(EntityId id) const
        {
            const size_t typeId = getTypeId<T>();
            if (maskUsable_ && typeId < kMaskBits && id < compMask_.size())
            {
                return (compMask_[id] & (std::uint64_t(1) << typeId)) != 0;
            }
            const auto *c = findContainer<T>();
            return c && c->contains(id);
        }

        template <typename T>
        void removeComponent(EntityId id)
        {
            const size_t typeId = getTypeId<T>();
            if (typeId < kMaskBits && id < compMask_.size())
                compMask_[id] &= ~(std::uint64_t(1) << typeId);
            getContainer<T>().remove(id);
        }

        /// @brief Итерация по компонентам типа T (получить контейнер)
        /// @example for (auto& comp : world.each<T>()) { ... }
        /// @note Для цепочки с исключением компонентов используй query<T...>().exclude<...>(fn)
        template <typename T>
        SparseSet<T> &each()
        {
            return getContainer<T>();
        }

        /// @brief Дефрагментация пула конкретного компонента
        template <typename T>
        void sort()
        {
            getContainer<T>().sort();
        }

        /// @brief Дефрагментация пула с пользовательским компаратором
        template <typename T, typename Compare>
        void sort(Compare &&comp)
        {
            getContainer<T>().sort(std::forward<Compare>(comp));
        }

        /// @brief Дефрагментирует ВСЕ зарегистрированные пулы компонентов
        void defragment()
        {
            for (auto &container : containers_)
            {
                if (container)
                    container->sort();
            }
        }

        void clear()
        {
            for (auto &container : containers_)
            {
                if (container)
                    container->clear();
            }
            compMask_.clear();
            entities_.reset();
        }

        // ==================== each() - прямые методы итерации ====================

        // ==================== Статистика ====================

        size_t getTotalComponentCount() const
        {
            size_t total = 0;
            for (const auto &container : containers_)
            {
                if (container)
                    total += container->size();
            }
            return total;
        }

        std::vector<ContainerStats> getContainerStats() const
        {
            std::vector<ContainerStats> stats;
            stats.reserve(containers_.size());

            for (size_t i = 0; i < containers_.size(); ++i)
            {
                if (!containers_[i])
                    continue;

                ContainerStats s;
                s.typeName = typeNames_[i];
                s.count = containers_[i]->size();
                s.memoryBytes = s.count * 64; // или s.count * componentSizes_[i] + ...

                stats.push_back(std::move(s));
            }

            // Опционально — сортировка по популярности
            std::ranges::sort(stats, std::greater<>{}, &ContainerStats::count);

            return stats;
        }

        // ==================== each / query / each_if ====================
        //
        // Здесь лежало ~1480 строк копипасты: each<T1>…each<T10> (у каждого N веток
        // «итерируемся по наименьшему контейнеру», то есть рост числа строк как O(N²)),
        // Query1…Query4 с двумя перегрузками exclude каждая, и each_if<T1>…each_if<T4>.
        // Свёрнуто в вариадические шаблоны: логика одна, арность любая.
        //
        // Схема прежняя и раскрывается компилятором в тот же код: выбираем наименьший
        // контейнер (ведущий), идём по его dense-массиву, остальные компоненты берём
        // через get(id). Единственное, что делается в рантайме, — выбор ведущего;
        // dispatchIndex превращает его в compile-time индекс, дальше всё статично.

    private:
        // Зовёт fn со std::integral_constant<size_t, K>, где K равен рантайм-индексу k.
        // Нужен, чтобы «какой контейнер оказался меньше» стало compile-time значением.
        template <std::size_t N, typename Fn>
        static void dispatchIndex(std::size_t k, Fn &&fn)
        {
            [&]<std::size_t... Is>(std::index_sequence<Is...>)
            {
                ((Is == k ? (void)fn(std::integral_constant<std::size_t, Is>{}) : (void)0), ...);
            }(std::make_index_sequence<N>{});
        }

        // Указатель на J-й компонент сущности. Для ведущего контейнера это данные из drvSlot,
        // на котором мы стоим, — поиск по sparse не нужен.
        template <std::size_t J, std::size_t I, typename Tup, typename DrvSlot>
        static auto pickPtr(Tup &cs, DrvSlot &drvSlot, EntityId id)
        {
            if constexpr (J == I)
                return &drvSlot.data;
            else
                return std::get<J>(cs).get(id);
        }

        // Итерация с ведущим контейнером номер I.
        template <std::size_t I, typename... Ts, typename Tup, typename F>
        static void eachDriven(Tup &cs, F &func)
        {
            // id нужен либо для поиска остальных компонентов, либо для колбэка с EntityId.
            // Для each<T1> с колбэком без id чтение sparse/owner пропускается целиком.
            constexpr bool kNeedId = (sizeof...(Ts) > 1) || std::is_invocable_v<F, EntityId, Ts &...>;

            auto &drv = std::get<I>(cs);
            // Прямая итерация по dense-массиву слотов (Slot { owner, data }).
            // id берём из слота компонента (drvSlot.owner) — он лежит в той же
            // кэш-линии, что и данные data. Пользовательский тип Ts остаётся чистым POD.
            for (auto &drvSlot : drv)
            {
                EntityId id{};
                if constexpr (kNeedId)
                    id = drvSlot.owner;

                auto ptrs = [&]<std::size_t... Js>(std::index_sequence<Js...>)
                {
                    return std::tuple{pickPtr<Js, I>(cs, drvSlot, id)...};
                }(std::index_sequence_for<Ts...>{});

                const bool present = std::apply([](auto *...p)
                                                { return ((p != nullptr) && ...); }, ptrs);
                if (!present)
                    continue;

                std::apply([&](auto *...p)
                           {
                    if constexpr (std::is_invocable_v<F, EntityId, Ts &...>)
                        func(id, *p...);
                    else
                        func(*p...); }, ptrs);
            }
        }

    public:
        /// @brief Итерация по сущностям, у которых есть ВСЕ компоненты Ts...
        /// @param func: (Ts&...) -> void либо (EntityId, Ts&...) -> void
        /// @note Ведущим берётся наименьший контейнер, остальные проверяются через get().
        template <typename... Ts, typename F>
        void each(F &&func)
        {
            static_assert(sizeof...(Ts) >= 1, "each<> требует хотя бы один тип компонента");

            auto cs = std::tie(getContainer<Ts>()...);

            const std::array<std::size_t, sizeof...(Ts)> sizes =
                std::apply([](auto &...c)
                           { return std::array<std::size_t, sizeof...(Ts)>{c.size()...}; }, cs);

            std::size_t driver = 0;
            for (std::size_t i = 1; i < sizes.size(); ++i)
            {
                if (sizes[i] < sizes[driver])
                    driver = i;
            }

            dispatchIndex<sizeof...(Ts)>(driver, [&](auto idx)
                                         { eachDriven<decltype(idx)::value, Ts...>(cs, func); });
        }

        // ==================== query().exclude — набор компонентов без указанных ====================

        template <typename... Ts>
        struct QueryN
        {
            World *world;

            /// @param f: (Ts&...) -> void либо (EntityId, Ts&...) -> void
            template <typename... Es, typename F>
            void exclude(F &&f) &&
            {
                static_assert(sizeof...(Es) >= 1, "exclude<> требует хотя бы один тип компонента");

                world->template each<Ts...>(
                    [w = world, fn = std::forward<F>(f)](EntityId id, Ts &...comps) mutable
                    {
                        // hasComponent, а не getContainer: проверка идёт на КАЖДОЙ
                        // сущности, а слим-путь не создаёт пустые контейнеры
                        // для exclude-типов и свободен от call в горячем коде.
                        if ((w->template hasComponent<Es>(id) || ...))
                            return;

                        if constexpr (std::is_invocable_v<F, EntityId, Ts &...>)
                            fn(id, comps...);
                        else
                            fn(comps...);
                    });
            }
        };

        /// @brief Запрос сущностей с набором компонентов; заверши цепочкой .exclude<E...>(fn)
        template <typename... Ts>
        QueryN<Ts...> query()
        {
            return QueryN<Ts...>{this};
        }

        // ==================== each_if — pred и fn с теми же сигнатурами, что у each ====================

        template <typename... Ts, typename Pred, typename Fn>
        void each_if(Pred &&pred, Fn &&fn)
        {
            each<Ts...>([&](EntityId id, Ts &...comps)
                        {
                bool ok;
                if constexpr (std::is_invocable_v<Pred, EntityId, Ts &...>)
                    ok = pred(id, comps...);
                else
                    ok = pred(comps...);

                if (!ok)
                    return;

                if constexpr (std::is_invocable_v<Fn, EntityId, Ts &...>)
                    fn(id, comps...);
                else
                    fn(comps...); });
        }



        // ==================== Systems API (из SystemManager) ====================

        /// @brief Добавление системы в мир
        template <typename T, typename... Args>
        T &addSystem(Args &&...args)
        {
            static_assert(std::is_base_of_v<ISystem, T>, "T must inherit from ISystem");
            auto system = std::make_unique<T>(std::forward<Args>(args)...);

            // Регистрация системы
            LOG_INFO("Registered system: {}", system->getName());

            auto &ref = *system;
            system->onInit(*this); // вызов onInit при добавлении
            systems_.push_back(std::move(system));
            systemsSorted_ = false; // после добавления новой системы нужно будет пересортировать
            return static_cast<T &>(ref);
        }

        /// @brief Минимальный приоритет систем, которые только готовят данные для рендера
        /// (без игровой симуляции). Совпадает с WorldTransformSystem и выше.
        static constexpr int kPresentationSystemPriorityMin = 90000;

        /// @brief Обновление всех систем (вызывается каждый кадр)
        void updateSystems(float dt)
        {
            if (!systemsSorted_)
            {
                // Сортируем системы по приоритету (от меньшего к большему)
                // гарантирует что системы с одинаковым приоритетом будут выполняться в порядке их регистрации
                std::stable_sort(systems_.begin(), systems_.end(),
                                 [](const auto &a, const auto &b)
                                 {
                                     return a->getPriority() < b->getPriority();
                                 });
                systemsSorted_ = true;
            }

            for (const auto &system : systems_)
            {
                if (system->isEnabled())
                {
                    PROFILE_SCOPE(system->getName());
                    TRACY_ZONE_DYNAMIC(system->getName());
                    system->update(*this, dt);
                }
            }
        }

        /// @brief Только «презентационный» проход: иерархия трансформов, сбор в RenderQueue,
        /// партиклы, трейлы, UI-бары — без геймплея (Edit/Pause в редакторе).
        void updatePresentationSystems(float dt)
        {
            if (!systemsSorted_)
            {
                std::stable_sort(systems_.begin(), systems_.end(),
                                 [](const auto &a, const auto &b)
                                 {
                                     return a->getPriority() < b->getPriority();
                                 });
                systemsSorted_ = true;
            }

            for (const auto &system : systems_)
            {
                if (!system->isEnabled() || system->getPriority() < kPresentationSystemPriorityMin)
                    continue;
                PROFILE_SCOPE(system->getName());
                TRACY_ZONE_DYNAMIC(system->getName());
                system->update(*this, dt);
            }
        }

        /// @brief Рендеринг всех систем (вызывается после update)
        void renderSystems()
        {
            for (const auto &system : systems_)
            {
                if (system->isEnabled())
                {
                    PROFILE_SCOPE(system->getName());
                    TRACY_ZONE_DYNAMIC(system->getName());
                    system->onRender(*this);
                }
            }
        }

        /// @brief Получение системы по типу
        template <typename T>
        T *getSystem()
        {
            static_assert(std::is_base_of_v<ISystem, T>, "T must inherit from ISystem");
            for (const auto &system : systems_)
            {
                if (auto casted = dynamic_cast<T *>(system.get()))
                {
                    return casted;
                }
            }
            return nullptr; // система не найдена
        }

        /// @brief Очистка всех систем
        void clearSystems()
        {
            // Вызов onDestroy для каждой системы перед удалением
            for (auto &system : systems_)
            {
                system->onDestroy(*this);
            }
            systems_.clear();
            systemsSorted_ = false;
        }
    };

} // namespace engine
