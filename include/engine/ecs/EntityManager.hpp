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
                alive_.resize(id + 1, 0);
                generations_.resize(id + 1, 0);
            }

            alive_[id] = 1;
            aliveCount_++;

            generations_[id]++;
            return id;
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
