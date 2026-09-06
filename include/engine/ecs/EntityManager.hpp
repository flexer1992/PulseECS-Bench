#pragma once

#include "engine/ecs/Entity.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <vector>

namespace engine
{
    /// @brief Жизненный цикл сущностей и метаданные их слотов.
    ///
    /// Все данные слота (поколение + маска компонентов) слиты в одну запись
    /// Record (16 байт). Это ключ к локальности: isAlive, чтение/очистка маски
    /// и смена поколения при create/destroy бьют по ОДНОЙ кэш-линии, тогда как
    /// прежняя схема (vector<uint8_t> alive_ + vector<uint32_t> generations_ +
    /// vector<uint64_t> compMask_ в World) требовала до трёх независимых
    /// промахов DRAM на каждую операцию над сущностью.
    class EntityManager
    {
    public:
        /// Слитая запись слота. gen: нечётное значение = слот жив;
        /// инкрементируется на каждом create и destroy (чёт <-> нечёт).
        struct Record
        {
            std::uint32_t gen = 0;
            std::uint64_t compMask = 0; ///< бит i — компонент с typeId == i
        };

        EntityManager() = default;

        EntityId create()
        {
            EntityId id;

            if (!recycled_.empty())
            {
                id = recycled_.back();
                recycled_.pop_back();
            }
            else
            {
                id = nextId_++;
            }

            if (id >= records_.size())
                grow(static_cast<size_t>(id) + 1);

            records_[id].gen += 1; // чёт (мёртв) -> нечёт (жив)
            aliveCount_++;
            return id;
        }

        /// @brief Пакетное создание N сущностей
        template <typename OutputIt>
        void create(size_t count, OutputIt out)
        {
            if (count == 0)
                return;

            size_t fromRecycled = std::min(count, recycled_.size());
            for (size_t i = 0; i < fromRecycled; ++i)
            {
                EntityId id = recycled_.back();
                recycled_.pop_back();
                records_[id].gen += 1;
                *out++ = id;
            }

            size_t remaining = count - fromRecycled;
            if (remaining > 0)
            {
                assert(remaining <= static_cast<size_t>(std::numeric_limits<EntityId>::max() - nextId_) &&
                       "EntityId overflow: слишком много созданий без переиспользования");

                EntityId startId = nextId_;
                nextId_ += static_cast<EntityId>(remaining);

                if (nextId_ > records_.size())
                    grow(nextId_);

                for (EntityId id = startId; id < nextId_; ++id)
                {
                    records_[id].gen += 1;
                    *out++ = id;
                }
            }

            aliveCount_ += count;
        }

        std::vector<EntityId> createBulk(size_t count)
        {
            std::vector<EntityId> ids;
            ids.reserve(count);
            create(count, std::back_inserter(ids));
            return ids;
        }

        void destroy(EntityId id)
        {
            if (id < records_.size() && (records_[id].gen & 1u))
            {
                records_[id].gen += 1; // нечёт (жив) -> чёт (мёртв)
                records_[id].compMask = 0;
                aliveCount_--;
                recycled_.push_back(id);
            }
        }

        bool isAlive(EntityId id) const
        {
            return id < records_.size() && (records_[id].gen & 1u) != 0;
        }

        /// @brief Маска компонентов слота. Вызывающий обязан проверить id < slotCount().
        std::uint64_t compMask(EntityId id) const { return records_[id].compMask; }

        /// @brief Изменяемый доступ к маске. Вызывающий обязан проверить id < slotCount().
        std::uint64_t &compMaskRef(EntityId id) { return records_[id].compMask; }

        size_t count() const
        {
            return aliveCount_;
        }

        /// @brief Верхняя граница индекса id + 1 (размер массива слотов), для кэшей по EntityId.
        size_t slotCount() const { return records_.size(); }

        /// @brief Предвыделение памяти под слоты сущностей.
        void reserve(size_t count)
        {
            if (count > records_.size())
                records_.resize(count);
        }

        void reset()
        {
            records_.clear();
            recycled_.clear();
            aliveCount_ = 0;
            nextId_ = 1;
        }

        struct EntityHandle
        {
            EntityId id;
            uint32_t generation;
        };

        EntityHandle createHandle(EntityId id) const
        {
            return {id, getGeneration(id)};
        }

        bool validateHandle(const EntityHandle &handle) const
        {
            if (!isAlive(handle.id))
                return false;
            return handle.generation == getGeneration(handle.id);
        }

        void assertValidHandle(const EntityHandle &handle, const char *context = nullptr) const
        {
            if (!isAlive(handle.id))
            {
                assert(false && "Entity is dead (use-after-destroy)");
            }
            if (handle.generation != getGeneration(handle.id))
            {
                assert(false && "Stale entity handle (generation mismatch - slot was recycled)");
            }
            (void)context;
        }

    private:
        std::uint32_t getGeneration(EntityId id) const
        {
            return (id < records_.size()) ? records_[id].gen : 0;
        }

        void grow(size_t minSlots)
        {
            const size_t newCap = std::max(minSlots, records_.size() * 2);
            records_.resize(newCap); // Record нулевая: gen = 0 (мёртв), маска пуста
        }

        EntityId nextId_{1};
        std::vector<Record> records_;   ///< индекс = EntityId
        std::vector<EntityId> recycled_; // LIFO-стек: только что освобождённый id ещё в кэше
        size_t aliveCount_{0};
    };

    /// @brief Публичный псевдоним для API World (handleOf / isAlive(handle) / ...).
    using EntityHandle = EntityManager::EntityHandle;

} // namespace engine