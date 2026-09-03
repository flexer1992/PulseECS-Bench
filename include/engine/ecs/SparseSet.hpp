#pragma once

#include "engine/ecs/Entity.hpp"
#include <cassert>
#include <concepts>
#include <cstdint>
#include <limits>
#include <vector>

namespace engine
{
    /// Контракт компонента SparseSet: поле `EntityId owner` (по конвенции —
    /// первым, чтобы делить кэш-линию с данными). create() патчит его,
    /// getOwner()/remove() читают. Без поля remove() молча писал бы мусор.
    template <typename T>
    concept ComponentWithOwner = requires(T &t) {
        { t.owner } -> std::convertible_to<const EntityId &>;
    };

    class ISparseSet
    {
    public:
        virtual ~ISparseSet() = default;
        virtual void remove(EntityId id) = 0;
        virtual bool contains(EntityId id) const = 0;
        virtual size_t size() const = 0;
        virtual void clear() = 0;
        virtual bool empty() const = 0;
    };

    template <typename T>
    class SparseSet : public ISparseSet
    {
        static_assert(ComponentWithOwner<T>,
                      "SparseSet<T>: component must declare `EntityId owner` (conventionally the first "
                      "member). SparseSet::create patches it, getOwner()/remove() rely on it.");

    private:
        std::vector<T> dense_;
        // Раньше здесь был std::vector<EntityId> denseToEntity_ — отдельный массив
        // для маппинга dense-индекс → entity id. На 2M сущностей и 6 типов это
        // 48 МБ оверхеда при том, что те же id уже лежат в поле owner каждого
        // компонента (dense_[i].owner). getOwner(index) теперь читает прямо из
        // dense_[index].owner, убирая второй поток по памяти в итерации.
        // Индекс в dense_, а не EntityId — хватает uint32_t. Раньше был size_t:
        // 8 байт на слот сущности, из которых половина всегда нули. sparse_
        // читается вразнобой на каждом get(), поэтому его размер напрямую бьёт
        // по кэшу. Сужение до 4 байт дало −7% на get<Pos> и −13% на итерации
        // фрагментированного мира. Потолок — 4 млрд компонентов одного типа.
        std::vector<uint32_t> sparse_;

    public:
        using iterator = typename std::vector<T>::iterator;
        using const_iterator = typename std::vector<T>::const_iterator;

        SparseSet() = default;
        ~SparseSet() override = default;

        void reserve(size_t n)
        {
            dense_.reserve(n);
            sparse_.reserve(n);
        }

        // ==================== Type-specific методы ====================

        template <typename... Args>
        T &create(EntityId owner, Args &&...args)
        {
            assert(!contains(owner) && "Entity already has this component!");

            if (owner >= sparse_.size())
            {
                sparse_.resize(owner + 1, INVALID_INDEX);
            }

            size_t index = dense_.size();
            dense_.emplace_back(std::forward<Args>(args)...);
            dense_.back().owner = owner;
            sparse_[owner] = static_cast<uint32_t>(index);

            return dense_.back();
        }

        T *get(EntityId owner)
        {
            if (owner >= sparse_.size())
                return nullptr;

            size_t index = sparse_[owner];
            if (index == INVALID_INDEX)
                return nullptr;

            return &dense_[index];
        }

        const T *get(EntityId owner) const
        {
            if (owner >= sparse_.size())
                return nullptr;

            size_t index = sparse_[owner];
            if (index == INVALID_INDEX)
                return nullptr;

            return &dense_[index];
        }

        EntityId getOwner(size_t index) const
        {
            assert(index < dense_.size());
            return dense_[index].owner;
        }

        T &rawData(size_t index)
        {
            assert(index < dense_.size());
            return dense_[index];
        }

        const T &rawData(size_t index) const
        {
            assert(index < dense_.size());
            return dense_[index];
        }

        // Итерация
        iterator begin() { return dense_.begin(); }
        iterator end() { return dense_.end(); }
        const_iterator begin() const { return dense_.begin(); }
        const_iterator end() const { return dense_.end(); }

        // ==================== ISparseSet interface ====================

        void remove(EntityId owner) override
        {
            if (!contains(owner))
                return;

            size_t index = sparse_[owner];
            size_t last = dense_.size() - 1;

            // lastOwner читаем ДО move: после std::move(dense_[last]) для
            // тривиально-копируемых типов dense_[last] остаётся валидным, но
            // в moved-from состоянии; явно сохраняем значение, чтобы не зависеть
            // от семантики move-assignment пользовательского T.
            EntityId lastOwner = dense_[last].owner;

            dense_[index] = std::move(dense_[last]);
            // dense_[index].owner после move == lastOwner (move-assign для
            // aggregate просто копирует), но оставляем явный set для надёжности.
            dense_[index].owner = lastOwner;
            sparse_[lastOwner] = static_cast<uint32_t>(index);

            dense_.pop_back();
            sparse_[owner] = INVALID_INDEX;
        }

        bool contains(EntityId owner) const override
        {
            return owner < sparse_.size() && sparse_[owner] != INVALID_INDEX;
        }

        size_t size() const override
        {
            return dense_.size();
        }

        void clear() override
        {
            dense_.clear();
            sparse_.clear();
        }

        bool empty() const override
        {
            return dense_.empty();
        }

        static constexpr uint32_t INVALID_INDEX = std::numeric_limits<uint32_t>::max();
    };

} // namespace engine
