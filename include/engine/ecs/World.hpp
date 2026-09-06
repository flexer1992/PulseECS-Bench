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
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <unordered_map>
#include <tuple>
#include <typeindex>
#include <typeinfo>
#include <utility>
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

    // Хелпер для получения читаемого имени типа. Вырезает имя из сигнатуры
    // шаблонной функции, поэтому работает без RTTI (-fno-rtti не ломается) и
    // одинаково на Clang/GCC (__PRETTY_FUNCTION__) и MSVC (__FUNCSIG__).
    template <typename T>
    constexpr const char *typeNameRaw() noexcept
    {
#if defined(_MSC_VER) && !defined(__clang__)
        return __FUNCSIG__;
#else
        return __PRETTY_FUNCTION__;
#endif
    }

    template <typename T>
    constexpr std::string_view getTypeName()
    {
        const std::string_view s = typeNameRaw<T>();

#if defined(_MSC_VER) && !defined(__clang__)
        std::string_view name = s.substr(s.find('<') + 1, s.rfind('>') - s.find('<') - 1);
        // MSVC украшает имена: 'struct Pos', 'class Foo', ...
        for (const std::string_view kw : {"struct ", "class ", "union ", "enum "})
        {
            if (name.starts_with(kw))
            {
                name.remove_prefix(kw.size());
                break;
            }
        }
        return name;
#elif defined(__clang__) || defined(__GNUC__)
        // "... [with T = engine::Pos]" либо "... [with T = engine::Pos; ...]"
        const auto b = s.find("T = ") + 4;
        auto e = s.find(';', b);
        const auto close = s.find(']', b);
        if (e == std::string_view::npos || (close != std::string_view::npos && close < e))
            e = close;
        return s.substr(b, e - b);
#else
        return typeid(T).name(); // редкий компилятор: старое поведение, требует RTTI
#endif
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

    // ==================== Owning-группы ====================
    //
    // Группа держит сущности со ВСЕМИ компонентами Ts плотно в НАЧАЛЕ
    // dense-массива КАЖДОГО принадлежащего ей пула: позиции [0, len).
    // Якорь в начале выбран не случайно: физическое удаление (swap-and-pop)
    // всегда трогает ПОСЛЕДНИЙ слот массива и потому никогда не задевает
    // регион группы — вся поддержка инварианта сводится к синхронным
    // swap-цепочкам swapAt(pos, id) по всем пулам сразу.
    //
    // Итерация группы — прямой проход по [0, len) каждого пула: ноль поисков
    // по sparse, ноль проверок присутствия. Это путь к архетипным скоростям
    // (~0.2-0.5 ns/op) поверх sparse-set хранения.
    //
    // Ограничения (как в EnTT): два пула не могут принадлежать двум разным
    // группам; sort()/defragment() owned-пулов запрещены (ломают инвариант).

    class IGroup
    {
    public:
        virtual ~IGroup() = default;
        /// @brief Сущность получила компонент, принадлежащий группе (после create).
        virtual void onComponentAdded(EntityId id) = 0;
        /// @brief Сущность теряет компонент группы (вызывается ДО remove из пула).
        virtual void onComponentRemoved(EntityId id) = 0;
        virtual void reset() = 0;
        virtual bool ownsSameTypes(const std::vector<std::size_t> &sortedTypeIds) const = 0;
        virtual const std::size_t *lenPtr() const = 0;
    };

    namespace detail
    {
        /// @brief Хранитель инварианта группы: живёт в World, знает пулы и len.
        template <typename... Ts>
        class GroupHandler final : public IGroup
        {
        public:
            using Pools = std::tuple<SparseSet<Ts> *...>;

            GroupHandler(Pools pools, std::vector<std::size_t> sortedTypeIds)
                : pools_(std::move(pools)), ownedTypeIds_(std::move(sortedTypeIds)) {}

            /// @brief Первичная сортировка: члены группы — в начало всех пулов.
            /// Проход по dense ведущего пула: кто не в регионе и имеет все
            /// остальные компоненты — переезжает на позицию len синхронно везде.
            void arrange()
            {
                auto *drv = std::get<0>(pools_);
                const std::size_t n = drv->size();
                for (std::size_t p = 0; p < n; ++p)
                    tryPush(drv->getOwner(p));
            }

            void onComponentAdded(EntityId id) override { tryPush(id); }

            void onComponentRemoved(EntityId id) override
            {
                auto *drv = std::get<0>(pools_);
                if (drv->contains(id) && drv->indexOf(id) < len_)
                    swapElements(--len_, id); // уводим id в конец региона, регион сжимается справа
            }

            void reset() override { len_ = 0; }

            bool ownsSameTypes(const std::vector<std::size_t> &sortedTypeIds) const override
            {
                return sortedTypeIds == ownedTypeIds_;
            }

            const std::size_t *lenPtr() const override { return &len_; }

        private:
            void tryPush(EntityId id)
            {
                auto *drv = std::get<0>(pools_);
                if (!drv->contains(id) || drv->indexOf(id) < len_)
                    return; // нет компонента-драйвера либо уже член группы

                // Остальные (кроме ведущего) пулы: наличие = членство.
                const bool hasAll = std::apply(
                    [id](auto *first, auto *...rest)
                    {
                        (void)first; // ведущий проверен выше
                        return (rest->contains(id) && ...);
                    }, pools_);
                if (hasAll)
                    swapElements(len_++, id);
            }

            void swapElements(std::size_t pos, EntityId id)
            {
                std::apply([pos, id](auto *...p)
                           { (p->swapAt(id, pos), ...); }, pools_);
            }

            Pools pools_;
            std::size_t len_ = 0;
            std::vector<std::size_t> ownedTypeIds_;
        };
    } // namespace detail

    /// @brief Дескриптор группы для пользователя: НЕ владеет handler'ом (им
    /// владеет World), читает len через указатель — размер меняется на лету.
    template <typename... Ts>
    class BasicGroup
    {
    public:
        using Pools = std::tuple<SparseSet<Ts> *...>;

        BasicGroup() = default;
        BasicGroup(Pools pools, const std::size_t *len)
            : pools_(std::move(pools)), len_(len) {}

        std::size_t size() const
        {
            assert(len_ && "Группа не привязана: создавайте через world.group<Ts...>()");
            return *len_;
        }

        /// @brief Итерация [0, len): id и все компоненты — прямой доступ к dense.
        /// @param func: (Ts&...) -> void либо (EntityId, Ts&...) -> void
        template <typename F>
        void each(F &&func) const
        {
            const std::size_t n = size();
            for (std::size_t i = 0; i < n; ++i)
            {
                auto ptrs = std::apply([i](auto *...p)
                                       { return std::tuple{&p->rawSlot(i).data...}; },
                                       pools_);
                const EntityId id = std::get<0>(pools_)->getOwner(i);
                std::apply([&](auto *...p)
                           {
                    if constexpr (std::is_invocable_v<F, EntityId, Ts &...>)
                        func(id, *p...);
                    else
                        func(*p...); },
                           ptrs);
            }
        }

        class iterator
        {
            const BasicGroup *g_ = nullptr;
            std::size_t i_ = 0;

        public:
            iterator() = default;
            iterator(const BasicGroup *g, std::size_t i) : g_(g), i_(i) {}

            decltype(auto) operator*() const
            {
                const std::size_t i = i_;
                auto ptrs = std::apply([i](auto *...p)
                                       { return std::tuple{&p->rawSlot(i).data...}; },
                                       g_->pools_);
                const EntityId id = std::get<0>(g_->pools_)->getOwner(i);
                return std::apply([&](auto *...p)
                                  { return std::tuple<EntityId, Ts &...>(id, *p...); },
                                  ptrs);
            }

            iterator &operator++()
            {
                ++i_;
                return *this;
            }

            iterator operator++(int)
            {
                iterator tmp = *this;
                ++i_;
                return tmp;
            }

            bool operator==(const iterator &o) const { return i_ == o.i_; }
            bool operator!=(const iterator &o) const { return i_ != o.i_; }
        };

        iterator begin() const { return iterator{this, 0}; }
        iterator end() const { return iterator{this, size()}; }

    private:
        Pools pools_;
        const std::size_t *len_ = nullptr;
    };

    // ==================== Класс мира ====================

    class World
    {
    private:
        EntityManager entities_;

        // Основной массив контейнеров
        std::vector<std::unique_ptr<ISparseSet>> containers_;
        // Removers: неконтролирующие указатели на те же пулы (владеет containers_).
        // Раньше здесь были std::function с захватом — два indirect call на удаление
        // компонента; виртуальный ISparseSet::remove даёт один dispatch без
        // аллокаций при регистрации типа.
        std::vector<ISparseSet *> removers_;

        // Owning-группы: handler'ы (владеет World) и карта typeId -> группа.
        // У пула не может быть двух групп: поддерживать два независимых
        // региона [0, len) в одном dense-массиве невозможно.
        // hasGroups_ дублирует !groups_.empty() для быстрого выхода из хуков
        // add/remove: у большинства миров групп нет вовсе.
        std::vector<std::unique_ptr<IGroup>> groups_;
        std::vector<IGroup *> poolGroup_;
        bool hasGroups_ = false;

        // Маска типов компонентов на сущность теперь живёт ВНУТРИ EntityManager
        // (EntityManager::Record::compMask): поколение и маска делят одну кэш-линию,
        // так что isAlive + чтение/сброс маски при destroyEntity — один промах
        // памяти вместо двух-трёх.

        // В маску влезает kMaskBits типов. Если типов станет больше, маска перестаёт
        // описывать мир целиком — тогда честно откатываемся на полный обход removers,
        // вместо того чтобы молча терять компоненты при удалении сущности.
        // Биты типов < kMaskBits при этом остаются корректными, поэтому
        // hasComponent использует фолбэк per-type, а не глобально.
        static constexpr size_t kMaskBits = 64;
        bool maskUsable_ = true;

        void markComponentBit(EntityId id, size_t typeId)
        {
            if (typeId >= kMaskBits)
            {
                maskUsable_ = false;
                return;
            }
            assert(id < entities_.slotCount() && "Entity slot must exist (entity must be alive)");
            entities_.compMaskRef(id) |= (std::uint64_t(1) << typeId);
        }

        /// @brief Уведомить owning-группу (если есть) о новом компоненте.
        /// Быстрый выход по флагу: у большинства миров групп нет вовсе.
        template <typename T>
        void notifyGroupAdded(EntityId id)
        {
            if (!hasGroups_)
                return;
            const size_t typeId = getTypeId<T>();
            if (typeId < poolGroup_.size() && poolGroup_[typeId])
                poolGroup_[typeId]->onComponentAdded(id);
        }

        void notifyGroupRemoved(size_t typeId, EntityId id)
        {
            if (!hasGroups_)
                return;
            if (typeId < poolGroup_.size() && poolGroup_[typeId])
                poolGroup_[typeId]->onComponentRemoved(id);
        }

        template <typename T>
        bool isPoolOwned() const
        {
            const size_t typeId = getTypeId<T>();
            return typeId < poolGroup_.size() && poolGroup_[typeId] != nullptr;
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

        /// @brief Предвыделение памяти под сущности и их маски компонентов
        void reserveEntities(size_t count)
        {
            entities_.reserve(count);
        }

        void destroyEntity(EntityId id)
        {
            if (!entities_.isAlive(id))
                return;

            if (maskUsable_)
            {
                // Быстрый путь: зовём removers только для реально имеющихся компонентов.
                // isAlive, чтение маски, сброс маски и инкремент поколения в
                // EntityManager::destroy — всё в одной кэш-линии records_[id].
                if (id < entities_.slotCount())
                {
                    std::uint64_t m = entities_.compMask(id);
                    while (m != 0)
                    {
                        const size_t bit = static_cast<size_t>(std::countr_zero(m));
                        m &= (m - 1); // сбросить младший установленный бит
                        if (bit < removers_.size() && removers_[bit])
                        {
                            // owning-группы узнают об удалении ДО физического remove
                            if (hasGroups_) [[unlikely]]
                                notifyGroupRemoved(bit, id);
                            removers_[bit]->remove(id);
                        }
                    }
                }
            }
            else
            {
                // Типов больше kMaskBits — маска неполная, идём старым путём.
                for (size_t i = 0; i < removers_.size(); ++i)
                {
                    if (removers_[i])
                    {
                        if (hasGroups_) [[unlikely]]
                            notifyGroupRemoved(i, id);
                        removers_[i]->remove(id);
                    }
                }
            }

            entities_.destroy(id); // gen++ и compMask = 0
        }

        bool isAlive(EntityId id) const
        {
            return entities_.isAlive(id);
        }

        // ==================== Generational handles ====================
        //
        // Голый EntityId не отличает переиспользованный слот от старой
        // сущности: после destroy + create старый id молча указывает на нового
        // жильца. EntityHandle хранит поколение слота и делает use-after-recycle
        // обнаружимым (assert в debug через assertValidHandle-семантику).

        /// @brief Хендл существующей сущности (id + поколение слота).
        EntityHandle handleOf(EntityId id) const { return entities_.createHandle(id); }

        /// @brief Создать сущность и сразу получить её хендл.
        EntityHandle createEntityHandle()
        {
            const EntityId id = entities_.create();
            return entities_.createHandle(id);
        }

        /// @brief Жива ли сущность, на которую ссылается хендл, и не сменился
        /// ли жилец слота с момента его создания.
        bool isAlive(EntityHandle h) const { return entities_.validateHandle(h); }

        template <typename T, typename... Args>
        T &addComponent(EntityHandle h, Args &&...args)
        {
            assert(entities_.validateHandle(h) &&
                   "Stale entity handle: сущность удалена либо слот переиспользован");
            return addComponent<T>(h.id, std::forward<Args>(args)...);
        }

        template <typename T>
        T *getComponent(EntityHandle h)
        {
            assert(entities_.validateHandle(h) &&
                   "Stale entity handle: сущность удалена либо слот переиспользован");
            return getComponent<T>(h.id);
        }

        template <typename T>
        const T *getComponent(EntityHandle h) const
        {
            assert(entities_.validateHandle(h) &&
                   "Stale entity handle: сущность удалена либо слот переиспользован");
            return getComponent<T>(h.id);
        }

        template <typename T>
        bool hasComponent(EntityHandle h) const
        {
            assert(entities_.validateHandle(h) &&
                   "Stale entity handle: сущность удалена либо слот переиспользован");
            return hasComponent<T>(h.id);
        }

        template <typename T>
        void removeComponent(EntityHandle h)
        {
            assert(entities_.validateHandle(h) &&
                   "Stale entity handle: сущность удалена либо слот переиспользован");
            removeComponent<T>(h.id);
        }

        void destroyEntity(EntityHandle h)
        {
            assert(entities_.validateHandle(h) &&
                   "Stale entity handle: сущность удалена либо слот переиспользован");
            destroyEntity(h.id);
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

                // Remover = тот же пул через виртуальный ISparseSet::remove.
                // Время жизни указателя обеспечивает containers_ (unique_ptr).
                removers_[id] = containers_[id].get();

                typeNames_[id].assign(getTypeName<T>());
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
            T &comp = container.create(id, std::forward<Args>(args)...);
            if (hasGroups_) [[unlikely]]
                notifyGroupAdded<T>(id);
            return comp;
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
            T &comp = container.create(id, std::forward<Args>(args)...);
            if (hasGroups_) [[unlikely]]
                notifyGroupAdded<T>(id);
            return comp;
        }

        template <typename T>
        bool hasComponent(EntityId id) const
        {
            const size_t typeId = getTypeId<T>();
            // Биты типов < kMaskBits поддерживаются маской всегда, даже когда
            // maskUsable_ == false (его портят только типы >= 64): фолбэк per-type.
            if (typeId < kMaskBits && id < entities_.slotCount())
                return (entities_.compMask(id) & (std::uint64_t(1) << typeId)) != 0;
            const auto *c = findContainer<T>();
            return c && c->contains(id);
        }

        template <typename T>
        void removeComponent(EntityId id)
        {
            // findContainer, а не getContainer: удаление несуществующего типа
            // не должно создавать пустой пул.
            auto *container = findContainer<T>();
            if (!container)
                return;

            if (hasGroups_) [[unlikely]]
                notifyGroupRemoved(getTypeId<T>(), id); // ДО физического remove
            container->remove(id);

            const size_t typeId = getTypeId<T>();
            if (typeId < kMaskBits && id < entities_.slotCount())
                entities_.compMaskRef(id) &= ~(std::uint64_t(1) << typeId);
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
        /// @note Пулы, принадлежащие owning-группе, сортировать нельзя —
        /// это ломает инвариант [0, len). Вызов пропускается (assert в debug).
        template <typename T>
        void sort()
        {
            assert(!isPoolOwned<T>() &&
                   "Cannot sort a pool owned by a group: group invariant [0, len) would break");
            if (!isPoolOwned<T>())
                getContainer<T>().sort();
        }

        /// @brief Дефрагментация пула с пользовательским компаратором
        template <typename T, typename Compare>
        void sort(Compare &&comp)
        {
            assert(!isPoolOwned<T>() &&
                   "Cannot sort a pool owned by a group: group invariant [0, len) would break");
            if (!isPoolOwned<T>())
                getContainer<T>().sort(std::forward<Compare>(comp));
        }

        /// @brief Дефрагментирует ВСЕ зарегистрированные пулы компонентов
        /// (принадлежащие owning-группам пропускаются)
        void defragment()
        {
            for (size_t i = 0; i < containers_.size(); ++i)
            {
                if (containers_[i] && !(i < poolGroup_.size() && poolGroup_[i]))
                    containers_[i]->sort();
            }
        }

        void clear()
        {
            for (auto &container : containers_)
            {
                if (container)
                    container->clear();
            }
            entities_.reset(); // records_ сбрасывает и поколения, и маски компонентов
            for (auto &group : groups_)
                group->reset(); // регионы групп опустели вместе с пулами
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
                s.memoryBytes = containers_[i]->memoryBytes();

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

        private:
            /// @brief Если exclude-пул E МЕНЬШЕ ведущего, складывает владельцев его
            /// слотов в битовую карту (плотная проверка L2 вместо случайного чтения
            /// маски из DRAM на каждую сущность). Иначе — только помечает,
            /// что карта покрытие неполное и нужна честная проверка hasComponent.
            template <typename E>
            void accumulateSkip(std::size_t driverSize,
                                std::vector<std::uint64_t> &skipBits,
                                bool &allCovered) const
            {
                // findContainer, а не getContainer: тип ни разу не регистрировался —
                // исключать нечего, и пул создавать не нужно.
                auto *c = world->template findContainer<E>();
                if (!c)
                    return;

                if (c->size() >= driverSize)
                {
                    allCovered = false;
                    return;
                }

                if (skipBits.empty())
                    skipBits.assign((world->entitySlotCount() + 63) / 64, 0);

                for (const auto &slot : *c)
                    skipBits[slot.owner >> 6] |= (std::uint64_t(1) << (slot.owner & 63));
            }

        public:
            /// @param f: (Ts&...) -> void либо (EntityId, Ts&...) -> void
            template <typename... Es, typename F>
            void exclude(F &&f) &&
            {
                static_assert(sizeof...(Es) >= 1, "exclude<> требует хотя бы один тип компонента");
                World *w = world;

                // Тот же выбор ведущего, что внутри each<>: наименьший из требуемых пулов.
                const std::size_t driverSize = std::min({w->template getContainer<Ts>().size()...});

                std::vector<std::uint64_t> skipBits;
                bool allCovered = true;
                (accumulateSkip<Es>(driverSize, skipBits, allCovered), ...);

                w->template each<Ts...>(
                    [w, skipBits = std::move(skipBits), allCovered, fn = std::forward<F>(f)](
                        EntityId id, Ts &...comps) mutable
                    {
                        // Быстрый путь: биты пулов меньше ведущего собраны заранее,
                        // проверка — одна L2-загрузка вместо промаха по records_[id].
                        if (!skipBits.empty() &&
                            (skipBits[id >> 6] & (std::uint64_t(1) << (id & 63))))
                            return;

                        // Пулы >= ведущего битовой картой не покрыты — честная проверка.
                        if (!allCovered && (w->template hasComponent<Es>(id) || ...))
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

        // ==================== view — range-for запрос ====================
        //
        //     for (auto [id, pos, vel] : world.view<Pos, Vel>()) { ... }
        //     for (auto [pos, vel]      : world.view<Pos, Vel>().comps()) { ... }
        //     for (auto [id, a, b]      : world.view<A, B>().exclude<Extra>()) { ... }
        //
        // Схема та же, что у each<>: ведущий — наименьший пул, идём по его dense.
        // Отличие: все компоненты, включая ведущий, добираются get(id). Это цена
        // типобезопасного итератора без рантайм-диспатча на каждом шаге (тип
        // ведущего выбирается в рантайме, а кортеж должен быть статическим).
        // Для горячих систем each<>(callback) остаётся самым быстрым путём:
        // там ведущий компонент читается прямо из dense-слота.
        //
        // Владение: view хранит неконтролирующие указатели на пулы. Пока идёт
        // итерация, нельзя добавлять/удалять компоненты и сущности.
        template <bool kWithEntity, typename... Ts>
        class BasicView
        {
            template <bool, typename...>
            friend class BasicView;
            friend class World; // World::view() собирает view через приватную часть

        public:
            class iterator
            {
                friend class BasicView;

                const BasicView *view_ = nullptr;
                std::size_t i_ = 0;
                EntityId id_ = InvalidEntity;
                std::tuple<Ts *...> comps_;

                iterator(const BasicView *view, std::size_t start)
                    : view_(view), i_(start)
                {
                    resolve();
                }

                /// @brief Продвигает i_ к следующей сущности, у которой есть ВСЕ
                /// компоненты Ts (и ни один из exclude-типов), и кэширует указатели.
                void resolve()
                {
                    const BasicView &v = *view_;
                    const std::size_t n = v.drvCount_;

                    if constexpr (sizeof...(Ts) == 1)
                    {
                        // Единственный пул — он же ведущий (тип известен во время
                        // компиляции): компонент есть у каждой записи dense, читаем
                        // прямо из слота, без поиска по sparse.
                        auto *set = std::get<0>(v.pools_);
                        while (i_ < n)
                        {
                            auto &slot = set->rawSlot(i_);

                            if (!v.skipBits_.empty() &&
                                (v.skipBits_[slot.owner >> 6] &
                                 (std::uint64_t(1) << (slot.owner & 63))))
                            {
                                ++i_;
                                continue;
                            }

                            id_ = slot.owner;
                            std::get<0>(comps_) = &slot.data;
                            return;
                        }
                        return;
                    }

                    while (i_ < n)
                    {
                        std::memcpy(&id_, v.drvBase_ + i_ * v.drvStride_, sizeof(EntityId));

                        if (!v.skipBits_.empty() &&
                            (v.skipBits_[id_ >> 6] & (std::uint64_t(1) << (id_ & 63))))
                        {
                            ++i_;
                            continue;
                        }

                        auto ptrs = std::apply(
                            [&](auto *...p)
                            { return std::tuple{p->get(id_)...}; }, v.pools_);
                        const bool present =
                            std::apply([](auto *...p)
                                       { return ((p != nullptr) && ...); }, ptrs);
                        if (present)
                        {
                            comps_ = std::move(ptrs);
                            return;
                        }
                        ++i_;
                    }
                }

            public:
                iterator() = default;

                decltype(auto) operator*() const
                {
                    if constexpr (kWithEntity)
                        return std::apply(
                            [&](auto *...p)
                            { return std::tuple<EntityId, Ts &...>(id_, *p...); }, comps_);
                    else
                        return std::apply(
                            [](auto *...p)
                            { return std::tuple<Ts &...>(*p...); }, comps_);
                }

                iterator &operator++()
                {
                    ++i_;
                    resolve();
                    return *this;
                }

                iterator operator++(int)
                {
                    iterator tmp = *this;
                    ++(*this);
                    return tmp;
                }

                bool operator==(const iterator &other) const { return i_ == other.i_; }
                bool operator!=(const iterator &other) const { return i_ != other.i_; }
            };

            iterator begin() const { return iterator(this, 0); }
            iterator end() const { return iterator(this, drvCount_); }

            /// @brief Исключить сущности, имеющие ХОТЯ БЫ ОДИН из компонентов Es...
            template <typename... Es>
            BasicView exclude() &&
            {
                static_assert(sizeof...(Es) >= 1, "exclude<> требует хотя бы один тип компонента");
                (accumulateExclude<Es>(), ...);
                return std::move(*this);
            }

            /// @brief Тот же запрос без EntityId: for (auto [pos, vel] : world.view<Pos, Vel>().comps())
            BasicView<false, Ts...> comps() const requires (kWithEntity)
            {
                BasicView<false, Ts...> v;
                v.world_ = world_;
                v.pools_ = pools_;
                v.drvBase_ = drvBase_;
                v.drvStride_ = drvStride_;
                v.drvCount_ = drvCount_;
                v.skipBits_ = skipBits_;
                return v;
            }

        private:
            BasicView() = default;
            explicit BasicView(World *world) : world_(world) {}

            /// @brief Выбор ведущего (наименьшего) пула и параметров обхода его dense.
            void init(std::tuple<SparseSet<Ts> *...> pools)
            {
                pools_ = std::move(pools);

                const std::array<std::size_t, sizeof...(Ts)> sizes = std::apply(
                    [](auto *...p)
                    { return std::array<std::size_t, sizeof...(Ts)>{p->size()...}; },
                    pools_);

                std::size_t driver = 0;
                for (std::size_t i = 1; i < sizes.size(); ++i)
                {
                    if (sizes[i] < sizes[driver])
                        driver = i;
                }

                // owner — первый член Slot с нулевым смещением у ЛЮБОГО T, поэтому
                // dense ведущего пула можно обходить как массив байтов с шагом
                // sizeof(Slot), не зная тип ведущего на каждом шаге итерации.
                World::dispatchIndex<sizeof...(Ts)>(driver, [&](auto idx)
                {
                    auto *drv = std::get<decltype(idx)::value>(pools_);
                    using DrvSet = std::remove_pointer_t<decltype(drv)>;
                    drvBase_ = reinterpret_cast<const char *>(drv->data());
                    drvStride_ = sizeof(typename DrvSet::Slot);
                });
                drvCount_ = sizes[driver];
            }

            template <typename E>
            void accumulateExclude()
            {
                // findContainer, а не getContainer: тип ни разу не регистрировался —
                // исключать нечего, и пул создавать не нужно.
                auto *c = world_->template findContainer<E>();
                if (!c)
                    return;

                if (skipBits_.empty())
                    skipBits_.assign((world_->entitySlotCount() + 63) / 64, 0);

                for (const auto &slot : *c)
                    skipBits_[slot.owner >> 6] |= (std::uint64_t(1) << (slot.owner & 63));
            }

            World *world_ = nullptr;
            std::tuple<SparseSet<Ts> *...> pools_;
            const char *drvBase_ = nullptr;
            std::size_t drvStride_ = 0;
            std::size_t drvCount_ = 0;
            std::vector<std::uint64_t> skipBits_;
        };

        /// @brief Итерируемый запрос: for (auto [id, pos, vel] : world.view<Pos, Vel>()) { ... }
        /// @note Для горячих систем each<>(callback) быстрее: ведущий компонент
        /// берётся напрямую из dense-слота, без поиска по sparse.
        template <typename... Ts>
        BasicView<true, Ts...> view()
        {
            static_assert(sizeof...(Ts) >= 1, "view<> требует хотя бы один тип компонента");
            BasicView<true, Ts...> v{this};
            v.init(std::tuple<SparseSet<Ts> *...>{&getContainer<Ts>()...});
            return v;
        }

        // ==================== group — owning-группа ====================
        //
        //     auto g = world.group<Pos, Vel>();
        //     for (auto [id, pos, vel] : g) { ... }   // ~0.2-0.5 ns/op
        //     g.each([](EntityId id, Pos&, Vel&) { ... });
        //
        // Сущности со ВСЕМИ компонентами Ts держатся плотно в начале каждого
        // пула [0, len); add/remove компонента поддерживают инвариант
        // автоматически. Правила: один пул — только одна группа; sort()/
        // defragment() owned-пулов запрещены (пропускаются). Дескриптор не
        // владеет состоянием: размер группы читается на лету.

        /// @brief Owning-группа по компонентам Ts. Повторный вызов с тем же
        /// набором типов возвращает дескриптор существующей группы.
        template <typename... Ts>
        BasicGroup<Ts...> group()
        {
            static_assert(sizeof...(Ts) >= 1, "group<> требует хотя бы один тип компонента");

            auto pools = std::tuple<SparseSet<Ts> *...>{&getContainer<Ts>()...};

            std::vector<std::size_t> sig{detail::componentTypeId<Ts>...};
            std::sort(sig.begin(), sig.end());

            for (const auto &g : groups_)
            {
                if (g->ownsSameTypes(sig))
                    return BasicGroup<Ts...>{std::move(pools), g->lenPtr()};
            }

            // Конфликт владения: у пула уже есть другая группа.
            for (std::size_t tid : sig)
            {
                (void)tid;
                assert(!(tid < poolGroup_.size() && poolGroup_[tid]) &&
                       "Conflicting groups: пул уже принадлежит другой owning-группе");
            }

            auto handler = std::make_unique<detail::GroupHandler<Ts...>>(
                pools, std::vector<std::size_t>(sig));
            detail::GroupHandler<Ts...> *raw = handler.get();
            groups_.push_back(std::move(handler));
            hasGroups_ = true;

            for (std::size_t tid : sig)
            {
                if (poolGroup_.size() <= tid)
                    poolGroup_.resize(tid + 1, nullptr);
                poolGroup_[tid] = raw;
            }

            raw->arrange();
            return BasicGroup<Ts...>{std::move(pools), raw->lenPtr()};
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
