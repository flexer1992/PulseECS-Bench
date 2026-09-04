#pragma once

#include "engine/ecs/Entity.hpp"
#include <cassert>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

namespace engine
{
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
    public:
        /// Внутренний слот хранения: EntityId owner + данные компонента T.
        /// Пользовательский T остаётся 100% чистым POD (standard layout).
        /// owner упакован в ту же кэш-линию, что исключает второй поток по памяти
        /// при итерации и swap-and-pop.
        struct Slot
        {
            EntityId owner;
            T data;

            Slot() = default;
            Slot(EntityId o, const T &d) : owner(o), data(d) {}
            Slot(EntityId o, T &&d) : owner(o), data(std::move(d)) {}

            template <typename... CArgs>
            requires (sizeof...(CArgs) > 1 || (!std::is_same_v<std::decay_t<CArgs>, T> && ...))
            Slot(EntityId o, CArgs &&...cargs)
                : owner(o), data{std::forward<CArgs>(cargs)...} {}

            operator T &() noexcept { return data; }
            operator const T &() const noexcept { return data; }
            T *operator->() noexcept { return &data; }
            const T *operator->() const noexcept { return &data; }
        };

    private:
        std::vector<Slot> dense_;
        std::vector<uint32_t> sparse_;

    public:
        using iterator = typename std::vector<Slot>::iterator;
        using const_iterator = typename std::vector<Slot>::const_iterator;

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
            dense_.emplace_back(owner, std::forward<Args>(args)...);
            sparse_[owner] = static_cast<uint32_t>(index);

            return dense_.back().data;
        }

        T *get(EntityId owner)
        {
            if (owner >= sparse_.size())
                return nullptr;

            size_t index = sparse_[owner];
            if (index == INVALID_INDEX)
                return nullptr;

            return &dense_[index].data;
        }

        const T *get(EntityId owner) const
        {
            if (owner >= sparse_.size())
                return nullptr;

            size_t index = sparse_[owner];
            if (index == INVALID_INDEX)
                return nullptr;

            return &dense_[index].data;
        }

        EntityId getOwner(size_t index) const
        {
            assert(index < dense_.size());
            return dense_[index].owner;
        }

        T &rawData(size_t index)
        {
            assert(index < dense_.size());
            return dense_[index].data;
        }

        const T &rawData(size_t index) const
        {
            assert(index < dense_.size());
            return dense_[index].data;
        }

        Slot &rawSlot(size_t index)
        {
            assert(index < dense_.size());
            return dense_[index];
        }

        const Slot &rawSlot(size_t index) const
        {
            assert(index < dense_.size());
            return dense_[index];
        }

        // Итерация по слотам (Slot { EntityId owner, T data })
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

            EntityId lastOwner = dense_[last].owner;

            dense_[index] = std::move(dense_[last]);
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
