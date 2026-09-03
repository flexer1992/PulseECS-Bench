cmake_minimum_required(VERSION 3.20)
project(ecs_compare_bench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native")
endif()

# Сторонние ECS подтягиваем через FetchContent, чтобы репозиторий был self-contained:
# единственная зависимость — компилятор C++20 и CMake ≥ 3.20.
include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

# ---- EnTT (header-only) ----
FetchContent_Declare(
    entt
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v3.13.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(entt)

# ---- flecs (header + .cpp) ----
set(FLECS_STATIC ON CACHE BOOL "" FORCE)
set(FLECS_SHARED OFF CACHE BOOL "" FORCE)
set(FLECS_DEVELOPER_MODE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    flecs
    GIT_REPOSITORY https://github.com/SanderMertens/flecs.git
    GIT_TAG v4.1.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(flecs)

# ---- gaia-ecs (header-only, single_include/gaia.h) ----
# Пин v0.8.10 сознательный: в v0.9 вариативный Query::all<...> стал одноаргументным.
set(GAIA_BUILD_UNITTEST OFF CACHE BOOL "" FORCE)
set(GAIA_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(GAIA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GAIA_GENERATE_CC OFF CACHE BOOL "" FORCE)
set(GAIA_MAKE_SINGLE_HEADER OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    gaia_ecs
    GIT_REPOSITORY https://github.com/richardbiely/gaia-ecs.git
    GIT_TAG v0.8.10
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(gaia_ecs)

# ---- EntityX (CMakeLists требует cmake_minimum 3.1, но CMake 4.x ругается) ----
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
# Тесты и бенчи EntityX используют catch.hpp, который на новых clang ругается
# на deprecated std::iterator. Нам нужен только сам фреймворк — отключаем их.
set(ENTITYX_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ENTITYX_RUN_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(ENTITYX_BUILD_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    entityx
    GIT_REPOSITORY https://github.com/alecthomas/entityx.git
    GIT_TAG 1.3.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(entityx)

# ---- pico_ecs (header-only, C API) ----
FetchContent_Declare(
    pico_ecs
    GIT_REPOSITORY https://github.com/empyreanx/pico_headers.git
    GIT_TAG main
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(pico_ecs)

# ============================================================
# Свой ECS — путь к include/ в корне репозитория.
# Никакого spdlog: Logger.hpp заменён на минимальный stdio-стаб.
# ============================================================
set(ECS_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../include")

# ============================================================
# Свой ECS на тех же сценариях, что и сторонние (зеркало ecs_bench_entt.cpp).
# Это колонка «my ECS» в таблице compare_ecs.sh.
# ============================================================
add_executable(ecs_bench_mine ecs_bench_mine.cpp)
target_include_directories(ecs_bench_mine PRIVATE ${ECS_INCLUDE_DIR})
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ecs_bench_mine PRIVATE -Wall -Wextra)
endif()

# ============================================================
# EnTT — header-only, ALIAS-таргет EnTT::EnTT.
# ============================================================
add_executable(ecs_bench_entt ecs_bench_entt.cpp)
target_include_directories(ecs_bench_entt PRIVATE ${ECS_INCLUDE_DIR})
target_link_libraries(ecs_bench_entt PRIVATE EnTT::EnTT)

# ============================================================
# flecs — ищем таргет по любому из его имён.
# ============================================================
add_executable(ecs_bench_flecs ecs_bench_flecs.cpp)
target_include_directories(ecs_bench_flecs PRIVATE
    ${ECS_INCLUDE_DIR}
    ${flecs_SOURCE_DIR}/include
)
if(TARGET flecs::flecs_static)
    target_link_libraries(ecs_bench_flecs PRIVATE flecs::flecs_static)
elseif(TARGET flecs::flecs)
    target_link_libraries(ecs_bench_flecs PRIVATE flecs::flecs)
elseif(TARGET flecs)
    target_link_libraries(ecs_bench_flecs PRIVATE flecs)
endif()

# ============================================================
# gaia-ecs — single_include/gaia.h.
# ============================================================
add_executable(ecs_bench_gaia ecs_bench_gaia.cpp)
target_include_directories(ecs_bench_gaia PRIVATE
    ${ECS_INCLUDE_DIR}
    ${gaia_ecs_SOURCE_DIR}/single_include
)

# ============================================================
# EntityX — свой CMakeLists, target обычно называется entityx.
# ============================================================
add_executable(ecs_bench_entityx ecs_bench_entityx.cpp)
target_include_directories(ecs_bench_entityx PRIVATE
    ${ECS_INCLUDE_DIR}
    ${entityx_SOURCE_DIR}
)
if(TARGET entityx)
    target_link_libraries(ecs_bench_entityx PRIVATE entityx)
    target_compile_options(entityx PRIVATE -Wno-error=deprecated-declarations)
endif()

# ============================================================
# pico_ecs — header-only.
# ============================================================
add_executable(ecs_bench_pico ecs_bench_pico.cpp)
target_include_directories(ecs_bench_pico PRIVATE
    ${ECS_INCLUDE_DIR}
    ${pico_ecs_SOURCE_DIR}
)