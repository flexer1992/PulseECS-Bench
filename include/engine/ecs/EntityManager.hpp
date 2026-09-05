#pragma once

#include "engine/ecs/Entity.hpp"
#include <vector>
#include <cassert>

namespace engine
{
    class EntityManager
    {
    private:
        EntityId nextId_{1};
        std::vector<uint8_t> alive_;
        std::vector<EntityId> recycled_; // LIFO-стек: только что освобождённый id ещё в кэше
        size_t aliveCount_{0};
        std::vector<uint32_t> generations_;

        uint32_t getGeneration(EntityId id) const
        {
            return (id < generations_.size()) ? generations_[id] : 0;
        }

    public:
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

            if (id >= alive_.size())
            {
                const size_t newCap = std::max(static_cast<size_t>(id + 1), alive_.size() * 2);
                alive_.resize(newCap, 0);
                generations_.resize(newCap, 0);
            }

            alive_[id] = 1;
            aliveCount_++;

            generations_[id]++;
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
                alive_[id] = 1;
                generations_[id]++;
                *out++ = id;
            }

            size_t remaining = count - fromRecycled;
            if (remaining > 0)
            {
                EntityId startId = nextId_;
                EntityId endId = nextId_ + static_cast<EntityId>(remaining);
                nextId_ = endId;

                if (endId > alive_.size())
                {
                    const size_t newCap = std::max(static_cast<size_t>(endId), alive_.size() * 2);
                    alive_.resize(newCap, 0);
                    generations_.resize(newCap, 0);
                }

                for (EntityId id = startId; id < endId; ++id)
                {
                    alive_[id] = 1;
                    generations_[id]++;
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
            if (id < alive_.size() && alive_[id])
            {
                alive_[id] = 0;
                aliveCount_--;
                recycled_.push_back(id);
            }
        }

        bool isAlive(EntityId id) const
        {
            return id < alive_.size() && alive_[id] != 0;
        }

        size_t count() const
        {
            return aliveCount_;
        }

        /// @brief Верхняя граница индекса id + 1 (размер массива слотов), для кэшей по EntityId.
        size_t slotCount() const { return alive_.size(); }

        void reset()
        {
            alive_.clear();
            recycled_.clear();
            aliveCount_ = 0;
            nextId_ = 1;
            generations_.clear();
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
    };

} // namespace engine
