#!/bin/bash

# 线程性能测试脚本 - 精简注释版
# 功能：测试不同线程数下的执行时间，生成性能图表

# === 测试配置 ===
KEY_LEN=16           # Key长度
KVPAIRS=100000       # KV对数/提交
COMMITS=16           # 提交次数
BASELINE_TIME=95.03   # 基准线(秒)

# === 线程范围 ===
# 优先方式：直接用逗号分隔的线程数组，例如: THREAD_LIST="16,24,32,48,64"
# 兼容方式：如果 THREAD_LIST 为空，则使用下面的 MIN/MAX/STEP 生成序列
TMP="1,2,3,4,5,6,7,8,9,10,15,20,25,30,35,40,45,50,55,60,64"
THREAD_LIST=$TMP      # 例如: "16,24,32,48,64"，留空则使用 MIN/MAX/STEP
MIN_THREADS=1        # 最小线程数 (当 THREAD_LIST 为空时生效)
MAX_THREADS=64       # 最大线程数 (当 THREAD_LIST 为空时生效)
STEP=1               # 步长 (当 THREAD_LIST 为空时生效)
REPEAT_PER_THREAD=2  # 每线程重复次数

# === 调试选项 ===
VERBOSE=true         # 默认显示详细输出

# 获取脚本所在目录
SCRIPT_DIR="$(dirname "$0")"
SCRIPT_DIR="$(cd "$SCRIPT_DIR" && pwd)"  # 获取绝对路径

# === 输出文件 ===
FULL_RESULTS_FILE="$SCRIPT_DIR/thread_performance_results.csv"   # 原始全量文件
SAMPLED_FILE="$SCRIPT_DIR/thread_performance_results_sampled.csv" # 采样/追加文件（多组）
PLOT_OUTPUT="$SCRIPT_DIR/thread_performance_plot.png"

RESULTS_FILE="$FULL_RESULTS_FILE"

# ----------------------
# 函数定义
# ----------------------
print_usage() {
    cat <<'USAGE'
Usage: test_thread.sh [options]

Options:
  --draw-only                仅绘图，跳过测试
  --main FILE                主曲线 CSV（默认: thread_performance_results.csv）
  --sampled FILE             采样/追加 CSV（默认: thread_performance_results_sampled.csv）
  --output FILE              输出图像路径（默认: thread_performance_plot.png）
  --threads "a,b,c"         指定线程数组（逗号分隔），优先于脚本内 THREAD_LIST
  -h, --help                 显示此帮助
USAGE
}

generate_plot() {
    local mainf="$1"; shift
    local sampledf="$1"; shift
    local out="$1"; shift
    local base="$1"; shift
    local title="$1"; shift

    if [ -n "$sampledf" ] && [ -f "$sampledf" ] && [ -s "$sampledf" ]; then
        python3 "$SCRIPT_DIR/draw_thread_picture.py" --input "$mainf" --sampled "$sampledf" --output "$out" --baseline "$base" --title "$title"
    else
        python3 "$SCRIPT_DIR/draw_thread_picture.py" --input "$mainf" --output "$out" --baseline "$base" --title "$title"
    fi
}

prepare_sampled_group() {
    # 在 sampled 文件末尾添加空行作为新组分隔，如果文件不存在则创建并添加注释头
    if [ ! -f "$SAMPLED_FILE" ]; then
        echo "# sampled" > "$SAMPLED_FILE"
    else
        echo "" >> "$SAMPLED_FILE"
    fi
}

run_tests() {
    # 构造线程数组：优先使用 THREAD_LIST 或外部覆盖 THREAD_ARR_OVERRIDE
    if [ -n "$THREAD_ARR_OVERRIDE" ]; then
        IFS=',' read -r -a THREAD_ARR <<< "$THREAD_ARR_OVERRIDE"
    elif [ -n "$THREAD_LIST" ]; then
        IFS=',' read -r -a THREAD_ARR <<< "$THREAD_LIST"
    else
        mapfile -t THREAD_ARR < <(seq $MIN_THREADS $STEP $MAX_THREADS)
    fi

    # 在开始测试前准备 sampled 分组
    prepare_sampled_group

    for thread_num in "${THREAD_ARR[@]}"; do
        sum_time=0
        success_count=0
        
        echo -n "测试线程数 $thread_num (MAX_REGION_NUM=$thread_num): "
        
        # 重复测试取平均值
        for ((i=1; i<=REPEAT_PER_THREAD; i++)); do
            echo -n "[$i]"
            
            if [ "$VERBOSE" = true ]; then
                echo ""
                echo "  执行: ./put_obs $thread_num $KEY_LEN $KVPAIRS $COMMITS"
                output=$(cd build_release/bin && ./put_obs $thread_num $KEY_LEN $KVPAIRS $COMMITS 2>&1)
                echo "  输出: $output"
            else
                output=$(cd build_release/bin && ./put_obs $thread_num $KEY_LEN $KVPAIRS $COMMITS 2>&1)
            fi
            
            if [ $? -eq 0 ]; then
                # 提取执行时间 (格式: "TOTAL: X.XX s")
                time_str=$(echo "$output" | grep -Eo "TOTAL: [0-9.]+ s" | grep -Eo "[0-9.]+" | head -n1)
                if [ -n "$time_str" ]; then
                    sum_time=$(echo "$sum_time + $time_str" | bc)
                    ((success_count++))
                    if [ "$VERBOSE" = true ]; then
                        echo "  时间: $time_str s"
                    fi
                else
                    echo -n "[无时间]"
                fi
            else
                echo -n "[失败]"
            fi
        done
        
        # 记录平均时间（直接追加到 sampled 文件的当前组）
        if [ $success_count -gt 0 ]; then
            avg_time=$(echo "scale=4; $sum_time / $success_count" | bc)
            echo "$thread_num,$avg_time" >> "$SAMPLED_FILE"
            echo " -> 平均时间: $avg_time s (成功 $success_count/$REPEAT_PER_THREAD 次)"
        else
            echo " -> 测试失败"
        fi
    done
}

# ----------------------
# 参数解析
# ----------------------
DRAW_ONLY=false
THREAD_ARR_OVERRIDE=""
CLI_MAIN=""
CLI_SAMPLED=""
CLI_OUTPUT=""

while [ "$#" -gt 0 ]; do
    case "$1" in
        --draw-only)
            DRAW_ONLY=true; shift;;
        --main)
            CLI_MAIN="$2"; shift 2;;
        --sampled)
            CLI_SAMPLED="$2"; shift 2;;
        --output)
            CLI_OUTPUT="$2"; shift 2;;
        --threads)
            THREAD_ARR_OVERRIDE="$2"; shift 2;;
        -h|--help)
            print_usage; exit 0;;
        *)
            echo "Unknown arg: $1"; print_usage; exit 1;;
    esac
done

# 允许通过命令行覆盖默认文件路径
if [ -n "$CLI_MAIN" ]; then
    RESULTS_FILE="$CLI_MAIN"
fi
if [ -n "$CLI_SAMPLED" ]; then
    SAMPLED_FILE="$CLI_SAMPLED"
fi
if [ -n "$CLI_OUTPUT" ]; then
    PLOT_OUTPUT="$CLI_OUTPUT"
fi

if [ "$DRAW_ONLY" = true ]; then
    echo "仅绘图模式: 主文件=$RESULTS_FILE, 采样文件=$SAMPLED_FILE, 输出=$PLOT_OUTPUT"
    generate_plot "$RESULTS_FILE" "$SAMPLED_FILE" "$PLOT_OUTPUT" "$BASELINE_TIME" "Thread Performance Test"
    echo "完成绘图，退出。"
    exit 0
fi

# ----------------------
# 继续原有流程（测试 + 绘图）
# ----------------------

# 切换到项目根目录
cd "$SCRIPT_DIR/../.."

# 编译项目
echo "编译项目..."
if [ ! -d "build_release" ]; then
    ./build.sh > /dev/null 2>&1
else
    cd build_release && make -j > /dev/null 2>&1 && cd ..
fi

# 初始化结果文件
# echo "# 线程数,执行时间(秒)" > "$RESULTS_FILE"

# 主测试循环
echo "开始性能测试 (线程范围: $MIN_THREADS-$MAX_THREADS, 间隔: $STEP)..."
echo "测试参数: KEY_LEN=$KEY_LEN, KVPAIRS=$KVPAIRS, COMMITS=$COMMITS"
echo "每个线程数重复测试 $REPEAT_PER_THREAD 次，取平均值"
echo "注意: 线程数 = MAX_REGION_NUM (区域数)"
echo "----------------------------------------"

# 构造线程数组：优先使用 THREAD_LIST，其次使用 MIN..MAX
if [ -n "$THREAD_LIST" ]; then
    IFS=',' read -r -a THREAD_ARR <<< "$THREAD_LIST"
else
    mapfile -t THREAD_ARR < <(seq $MIN_THREADS $STEP $MAX_THREADS)
fi

for thread_num in "${THREAD_ARR[@]}"; do
    sum_time=0
    success_count=0
    
    echo -n "测试线程数 $thread_num (MAX_REGION_NUM=$thread_num): "
    
    # 重复测试取平均值
    for ((i=1; i<=REPEAT_PER_THREAD; i++)); do
        echo -n "[$i]"
        
        if [ "$VERBOSE" = true ]; then
            echo ""
            echo "  执行: ./put_obs $thread_num $KEY_LEN $KVPAIRS $COMMITS"
            output=$(cd build_release/bin && ./put_obs $thread_num $KEY_LEN $KVPAIRS $COMMITS 2>&1)
            echo "  输出: $output"
        else
            output=$(cd build_release/bin && ./put_obs $thread_num $KEY_LEN $KVPAIRS $COMMITS 2>&1)
        fi
        
        if [ $? -eq 0 ]; then
            # 提取执行时间 (格式: "TOTAL: X.XX s")
            time_str=$(echo "$output" | grep -Eo "TOTAL: [0-9.]+ s" | grep -Eo "[0-9.]+" | head -n1)
            if [ -n "$time_str" ]; then
                sum_time=$(echo "$sum_time + $time_str" | bc)
                ((success_count++))
                if [ "$VERBOSE" = true ]; then
                    echo "  时间: $time_str s"
                fi
            else
                echo -n "[无时间]"
            fi
        else
            echo -n "[失败]"
        fi
    done
    
    # 记录平均时间（直接追加到 sampled 文件的当前组）
    if [ $success_count -gt 0 ]; then
        avg_time=$(echo "scale=4; $sum_time / $success_count" | bc)
        echo "$thread_num,$avg_time" >> "$SAMPLED_FILE"
        echo " -> 平均时间: $avg_time s (成功 $success_count/$REPEAT_PER_THREAD 次)"
    else
        echo " -> 测试失败"
    fi
done

# 在测试开始前，确保 sampled 文件存在并在末尾添加空行作为新组分隔
if [ ! -f "$SAMPLED_FILE" ]; then
    echo "# sampled" > "$SAMPLED_FILE"
else
    echo "" >> "$SAMPLED_FILE"
fi

echo "生成性能图表 (主: $RESULTS_FILE, 采样: $SAMPLED_FILE) ..."
if [ -f "$SAMPLED_FILE" ] && [ -s "$SAMPLED_FILE" ]; then
    python3 "$SCRIPT_DIR/draw_thread_picture.py" --input "$RESULTS_FILE" --sampled "$SAMPLED_FILE" --output "$PLOT_OUTPUT" --baseline "$BASELINE_TIME" --title "Thread Performance Test"
else
    python3 "$SCRIPT_DIR/draw_thread_picture.py" --input "$RESULTS_FILE" --output "$PLOT_OUTPUT" --baseline "$BASELINE_TIME" --title "Thread Performance Test"
fi

echo "测试完成! 结果: $RESULTS_FILE, 图表: $PLOT_OUTPUT"
