#!/bin/bash

# 线程性能测试脚本 - 精简注释版
# 功能：测试不同线程数下的执行时间，生成性能图表

# === 测试配置 ===
KEY_LEN=16           # Key长度
KVPAIRS=100000       # KV对数/提交
COMMITS=16           # 提交次数
BASELINE_TIME=95.03   # 基准线(秒)
SAMPLING_STEP=5      # 采样步长：1 表示所有点，>1 表示隔 N 个点采样（不会覆盖原始全量数据）

# === 线程范围 ===
MIN_THREADS=1       # 最小线程数 (对应MAX_REGION_NUM)
MAX_THREADS=64       # 最大线程数 (对应MAX_REGION_NUM)
STEP=5               # 采样间隔 (1=测所有点, 2=每隔1个测, 3=每隔2个测)
REPEAT_PER_THREAD=2  # 每线程重复次数

# === 调试选项 ===
VERBOSE=true         # 默认显示详细输出

# 获取脚本所在目录
SCRIPT_DIR="$(dirname "$0")"
SCRIPT_DIR="$(cd "$SCRIPT_DIR" && pwd)"  # 获取绝对路径

# === 输出文件 ===
FULL_RESULTS_FILE="$SCRIPT_DIR/thread_performance_results.csv"   # 原始全量文件
PLOT_OUTPUT="$SCRIPT_DIR/thread_performance_plot.png"

# 如果使用采样（SAMPLING_STEP>1），输出到独立的采样文件，保留全量文件不变
if [ "$SAMPLING_STEP" -gt 1 ]; then
    RESULTS_FILE="$SCRIPT_DIR/thread_performance_results_sampled_step${SAMPLING_STEP}.csv"
else
    RESULTS_FILE="$FULL_RESULTS_FILE"
fi

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

for thread_num in $(seq $MIN_THREADS $STEP $MAX_THREADS); do
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
    
    # 记录平均时间
    if [ $success_count -gt 0 ]; then
        avg_time=$(echo "scale=4; $sum_time / $success_count" | bc)
        echo "$thread_num,$avg_time" >> "$RESULTS_FILE"
        echo " -> 平均时间: $avg_time s (成功 $success_count/$REPEAT_PER_THREAD 次)"
    else
        echo " -> 测试失败"
    fi
done

# 生成图表
echo "生成性能图表..."
# 如果存在全量数据并且当前是采样文件，传入 --sampled 参数以在图上叠加采样点
# 采样文件现在也支持多组数据（空行分隔），方便在之前的测试结果上继续添加新测试
if [ "$SAMPLING_STEP" -gt 1 ] && [ -f "$FULL_RESULTS_FILE" ]; then
    python3 "$SCRIPT_DIR/draw_thread_picture.py" --input "$FULL_RESULTS_FILE" --sampled "$RESULTS_FILE" --output "$PLOT_OUTPUT" --baseline "$BASELINE_TIME" --title "Thread Performance Test (sample step ${SAMPLING_STEP})"
else
    python3 "$SCRIPT_DIR/draw_thread_picture.py" --input "$RESULTS_FILE" --output "$PLOT_OUTPUT" --baseline "$BASELINE_TIME" --title "Thread Performance Test"
fi

echo "测试完成! 结果: $RESULTS_FILE, 图表: $PLOT_OUTPUT"
