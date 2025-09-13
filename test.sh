#! /bin/bash
N=3                 # 测试次数
ARGS=(64 16 100000 16)   # 参数
EXE=./build_debug/bin/put_obs # 路径

# 先确保可执行文件存在
./build_debug.sh
[[ -x $EXE ]] || { echo "$EXE 不存在或不可执行"; exit 1; }

# 创建结果文件
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
RESULT_FILE="test_results_${TIMESTAMP}.txt"

exec > >(tee "$RESULT_FILE") 2>&
echo "开始性能测试..."
echo "测试参数: ${ARGS[*]}"
echo "每次测试 $N 次"

total_time=0
for ((i=1;i<=N;i++)); do
    echo "[run $i/$N] $EXE ${ARGS[*]} ..."
    # 用 grep 过滤出 TOTAL: 行，再 awk 取第 2 列
    t=$($EXE "${ARGS[@]}" | awk '/TOTAL:/{print $2}')
    # 如果这次没抓到时间，直接失败
    [[ -z $t ]] && { echo "第 $i 次运行未抓到时间"; exit 1; }
    echo "  -> $t s"
    total_time=$(echo "$total_time + $t" | bc -l)
done

avg=$(echo "scale=3; $total_time / $N" | bc -l)
echo "--------------------------------"
echo "测试次数: $N"
echo "平均时间: $avg s"
echo "========================================"
echo "所有测试完成！"
echo "测试结束时间: $(date)"
echo "结果已保存到: $RESULT_FILE"