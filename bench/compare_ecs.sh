#!/usr/bin/env bash
# bench/compare_ecs.sh — запускает сравнительные бенчмарки ECS и формирует таблицу.
#
# Использование:
#   ./bench/compare_ecs.sh                    # 200K entities, 5 iterations, Release
#   ./bench/compare_ecs.sh --entities 1000000
#   ./bench/compare_ecs.sh --keep-output      # не удалять временные файлы
#   ./bench/compare_ecs.sh --only ecs_bench,ecs_bench_gaia   # подмножество

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build/bench-release"

ENTITIES="${ENTITIES:-200000}"
ITERATIONS="${ITERATIONS:-5}"
KEEP_OUTPUT=0
ONLY=""

# Чтобы добавить новый ECS: допиши executable сюда + метку в LABELS (той же длины
# массива) + add_executable в CMakeLists.txt. Паттерны строк править не нужно —
# ВСЕ бенчи печатают одинаковые метки.
#
# Свой ECS представлен ecs_bench_mine (зеркало ecs_bench_entt.cpp), а НЕ ecs_bench.
# ecs_bench.cpp — детальный scaling-бенч: он игнорирует --entities, гоняет свою
# лестницу {64 … 1'000'000} и использует другие плотности компонентов, поэтому
# сравнивать его колонку с чужими нельзя. Запускай его отдельно.
BENCHES=(ecs_bench_mine ecs_bench_entt ecs_bench_flecs ecs_bench_entityx ecs_bench_gaia ecs_bench_pico)
LABELS=("PulseECS" "EnTT" "flecs" "EntityX" "gaia-ecs" "pico_ecs")

while [[ $# -gt 0 ]]; do
    case "$1" in
        --entities) ENTITIES="$2"; shift 2 ;;
        --iterations) ITERATIONS="$2"; shift 2 ;;
        --keep-output) KEEP_OUTPUT=1; shift ;;
        --only) ONLY="$2"; shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

# --only: фильтруем BENCHES/LABELS синхронно
if [[ -n "$ONLY" ]]; then
    IFS=',' read -r -a want <<< "$ONLY"
    fb=(); fl=()
    for i in "${!BENCHES[@]}"; do
        for w in "${want[@]}"; do
            if [[ "${BENCHES[$i]}" == "$w" ]]; then
                fb+=("${BENCHES[$i]}"); fl+=("${LABELS[$i]}")
            fi
        done
    done
    if [[ ${#fb[@]} -eq 0 ]]; then
        echo "--only: ни один бенч не совпал. Доступны: ${BENCHES[*]}"; exit 1
    fi
    BENCHES=("${fb[@]}"); LABELS=("${fl[@]}")
fi

echo "=== Конфигурация ==="
echo "Entities: $ENTITIES"
echo "Iterations: $ITERATIONS"
echo "Benchmarks: ${BENCHES[*]}"
echo "Build dir: $BUILD"
echo ""

# 1. CMake configure (один раз)
if [[ ! -f "$BUILD/CMakeCache.txt" ]]; then
    echo "=== CMake configure ==="
    cmake -S "$ROOT/bench" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -5
fi

# 2. Сборка всех бенчмарков
echo ""
echo "=== Сборка ==="
cmake --build "$BUILD" --target "${BENCHES[@]}" --parallel 2>&1 | tail -5

# 3. Запуск каждого бенча, вывод сохраняем во временные файлы
OUT_FILES=()
for bench in "${BENCHES[@]}"; do
    echo ""
    echo "=== Запуск $bench ==="
    tmp=$(mktemp)
    if ! "$BUILD/$bench" --entities "$ENTITIES" --iterations "$ITERATIONS" > "$tmp" 2>&1; then
        echo "FAILED: $bench — последние строки вывода:"
        tail -10 "$tmp"
        exit 1
    fi
    OUT_FILES+=("$tmp")
done

# 4. Сводная табличка. Метрика — ns/op.
echo ""

# Строки таблицы: "подпись|паттерн поиска". Паттерн один на все бенчи —
# они печатают одинаковые метки. Подстроки подобраны так, чтобы не пересекаться:
# "each<Pos,Vel>" не матчит "each<Pos,Vel,Tag>" из-за закрывающей скобки,
# "7 systems mixed" не матчит "frag 7sys mixed".
ROWS=(
    "add components (Pos+Vel+50%Tag)|add components"
    "each<Pos,Vel>|each<Pos,Vel> avg"
    "each<Pos,Vel,Tag>|each<Pos,Vel,Tag>"
    "query<4> without<1>|query<ReqA,B,C,D>"
    "destroyEntity|destroyEntity"
    "get<Pos>|get<Pos>"
    "7 systems mixed (full)|7 systems mixed"
    "frag 7sys mixed (alive)|frag 7sys mixed"
)

# Helper: ns/op из файла по паттерну строки
ns_per_op() {
    local file="$1"; local pattern="$2"
    grep -F "$pattern" "$file" | head -1 | sed -nE 's/.*\| ([0-9.]+) ns\/op.*/\1/p'
}

# Ширины: ячейка данных "| %9s ns" = 14 символов, шапка добивается до тех же 14.
COLW=9
LABELW=32
HEADER="$(printf "%-${LABELW}s" "Operation")"
for lbl in "${LABELS[@]}"; do
    HEADER+="$(printf "| %${COLW}s   " "$lbl")"
done
HEADER+="| $(printf '%-10s' 'fastest')"
WIDTH=${#HEADER}
LINE=$(printf '=%.0s' $(seq 1 "$WIDTH"))

echo "$LINE"
printf '%s\n' "        СРАВНЕНИЕ ECS (Release -O3 -march=native)"
printf '%s\n' "        entities=$ENTITIES, iterations=$ITERATIONS"
echo "$LINE"
echo "$HEADER"
echo "$LINE"

for row in "${ROWS[@]}"; do
    IFS='|' read -r label pat <<< "$row"
    vals=()
    for i in "${!BENCHES[@]}"; do
        ns=$(ns_per_op "${OUT_FILES[$i]}" "$pat")
        vals+=("${ns:-—}")
    done

    # Определяем fastest (минимальное значение)
    min_val=""; min_idx=-1
    for i in "${!vals[@]}"; do
        v="${vals[$i]}"
        if [[ "$v" == "—" ]]; then continue; fi
        if [[ -z "$min_val" ]] || awk -v a="$v" -v b="$min_val" 'BEGIN{exit !(a<b)}'; then
            min_val="$v"
            min_idx=$i
        fi
    done

    row_str="$(printf "%-${LABELW}s" "$label")"
    for v in "${vals[@]}"; do
        if [[ "$v" != "—" ]]; then
            row_str+="$(printf "| %${COLW}s ns" "$v")"
        else
            row_str+="$(printf "| %${COLW}s   " "—")"
        fi
    done
    if [[ $min_idx -ge 0 ]]; then
        fastest="${LABELS[$min_idx]}"
    else
        fastest="—"
    fi
    row_str+="| $(printf '%-10s' "$fastest")"
    echo "$row_str"
done

echo "$LINE"
echo ""
echo "fastest — у кого минимальное ns/op (меньше = быстрее)"
echo "«—» — сценарий не найден в выводе этого бенча"
echo ""

# 5. Контроль эквивалентности: sink должен совпадать у всех сторонних бенчей.
# Расходится → сценарии делают разный объём работы, цифры несравнимы.
echo "=== Проверка sink (объём выполненной работы) ==="
sinks=""
for i in "${!BENCHES[@]}"; do
    s=$(grep -E "^sink=" "${OUT_FILES[$i]}" | head -1 || true)
    if [[ -n "$s" ]]; then
        printf '  %-20s %s\n' "${BENCHES[$i]}" "$s"
        sinks+="${s}"$'\n'
    fi
done
uniq_count=$(printf '%s' "$sinks" | sort -u | grep -c . || true)
if [[ "$uniq_count" -le 1 ]]; then
    echo "  OK: все бенчи с sink дали одинаковое значение."
else
    echo "  ВНИМАНИЕ: sink различается ($uniq_count разных значений) —"
    echo "  бенчи выполнили разный объём работы, сравнение некорректно."
fi
echo ""

# 6. Пути к файлам (или чистка)
if [[ $KEEP_OUTPUT -eq 1 ]]; then
    echo "Полные результаты:"
    for i in "${!BENCHES[@]}"; do
        echo "  ${BENCHES[$i]}: ${OUT_FILES[$i]}"
    done
else
    echo "Полные результаты (уже удалены). Используй --keep-output чтобы сохранить."
    for f in "${OUT_FILES[@]}"; do
        rm -f "$f"
    done
fi
