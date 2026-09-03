#pragma once

#include <string>

namespace engine
{
    class World;

    class ISystem
    {
    public:
        virtual ~ISystem() = default;

        // ==================== Lifecycle ====================

        /// @brief Вызывается при инициализации мира/сцены
        virtual void onInit(World &world) { (void)world; }

        /// @brief Вызывается при уничтожении мира/сцены
        virtual void onDestroy(World &world) { (void)world; }

        // @brief Вызывается после update всех систем, для рендеринга
        virtual void onRender(World &world) { (void)world; }

        // ==================== Update ====================

        /// @brief Метод для обновления системы (вызывается каждый кадр)
        virtual void update(World &world, float dt) = 0;

        /// @brief Получение имени системы
        virtual const char *getName() const = 0;

        /// @brief Приоритет выполнения (больше = позже)
        /// @note Например: Input (100) → Movement (200) → Collision (300) → Render (400)
        virtual int getPriority() const { return 0; }

        /// @brief Включена ли система
        virtual bool isEnabled() const { return true; }
    };
}