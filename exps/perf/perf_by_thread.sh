#!/bin/bash
# 按线程生成火焰图的脚本（最终修正版）
# 用法: ./perf_by_thread.sh
# 环境变量可选: TOP_N - 只生成采样数前 N 的线程（默认 0 表示全部）

set -euo pipefail

# --- 参数配置 ---
region=64
keylen=16
kvpair=100000
version=16
param_suffix="newnewnew_region${region}_keylen${keylen}_kvpair${kvpair}_version${version}"
timestamp=$(date +%Y%m%d_%H%M%S)

# --- 准备工作 ---
cd ../../
./build_debug.sh --cxx g++
cd exps/perf/
mkdir -p results

# 清理并创建数据和索引目录
rm -rf "../../data/"
mkdir -p "../../data/"
rm -rf "../../index"
mkdir -p "../../index"

# set -x

# --- 性能采样 ---
# 1. 采样：以子进程方式运行被测程序（采集该进程的所有线程）
sudo perf record -g -F 99 ../../build_debug/bin/put_obs "$region" "$keylen" "$kvpair" "$version"

# 2. 导出 perf.script_full 用于提取 PID/TID token
sudo perf script -F pid,tid,comm > perf.script_full

# 3. 导出默认格式的 perf.script，包含完整的调用栈，用于生成火焰图
sudo perf script > perf.script

# --- 数据处理与火焰图生成 ---

# 4. 从 perf.script_full 中提取所有 pid/tid token，按出现频次排序
thread_counts=$(sudo grep -oE '[0-9]+/[0-9]+' perf.script_full | sort | uniq -c | sort -rn || true)

if [[ -z "$thread_counts" ]]; then
  echo "错误：无法从 perf.script_full 提取 pid/tid token，退出。" >&2
  echo "请检查 perf.script_full 的内容：" >&2
  sudo head -n 50 perf.script_full >&2 || true
  exit 1
fi

# 5. 准备运行目录和要处理的 token 列表
TOP_N=${TOP_N:-0}
run_dir="results/${param_suffix}_${timestamp}"
mkdir -p "$run_dir/threads"

if [[ "$TOP_N" -le 0 ]]; then
  tokens=$(echo "$thread_counts" | awk '{print $2}')
else
  tokens=$(echo "$thread_counts" | head -n "$TOP_N" | awk '{print $2}')
fi

# 6. 为每个线程生成火焰图
for token in $tokens; do
  pid=$(echo "$token" | cut -d'/' -f1)
  tid=$(echo "$token" | cut -d'/' -f2)
  echo "---"
  echo "正在处理线程: token=$token (PID=$pid, TID=$tid)"

  # 核心逻辑:
  # - 使用 sudo awk 读取 root 所有的 perf.script 文件，避免权限问题。
  # - 直接根据 TID (第二列) 筛选段落，无需猜测命令名。
  # - 将筛选出的、格式正确的段落传递给 stackcollapse-perf.pl。
  sudo awk -v tid="$tid" 'BEGIN{RS=""; ORS="\n\n"} $2 == tid' perf.script | ./FlameGraph/stackcollapse-perf.pl > "perf.tid.${tid}.folded"

  if [[ -s "perf.tid.${tid}.folded" ]]; then
    ./FlameGraph/flamegraph.pl "perf.tid.${tid}.folded" > "$run_dir/threads/${tid}.svg"
    echo "已生成: $run_dir/threads/${tid}.svg"
    sudo perf report --stdio -t $tid > "$run_dir/threads/${tid}.txt"
  else
    echo "线程 $tid 没有可用样本，跳过。"
  fi
  rm -f "perf.tid.${tid}.folded"
done
echo "---"

# 7. 生成汇总火焰图
echo "正在生成汇总火焰图..."
sudo cat perf.script | ./FlameGraph/stackcollapse-perf.pl > perf.folded
./FlameGraph/flamegraph.pl perf.folded > "$run_dir/flamegraph_${param_suffix}_${timestamp}.svg"
echo "已生成汇总火焰图: $run_dir/flamegraph_${param_suffix}_${timestamp}.svg"

# 8. 生成 perf 文本报告
echo "正在生成 perf report..."
sudo perf report --stdio > "$run_dir/perf_report_${param_suffix}_${timestamp}.txt"

# --- 清理工作 ---
echo "正在清理中间文件..."
rm -f perf.data perf.folded perf.script perf.script_full

echo "---"
echo "全部完成！"
echo "运行结果保存在: $run_dir"
echo "  - 按线程火焰图目录: $run_dir/threads/"
echo "  - 汇总火焰图: $run_dir/flamegraph_${param_suffix}_${timestamp}.svg"
echo "  - Perf 文本报告: $run_dir/perf_report_${param_suffix}_${timestamp}.txt"
