#!/bin/bash
# 性能测试参数设置
region=6                # 测试区域名
keylen=16                 # key长度
kvpair=100000             # kv对数量
version=16                # 版本号
data_path="$PWD/../data/"   # 数据目录
index_path="$PWD/../index"  # 索引目录

# 生成参数标识符，用于文件命名
param_suffix="blocking_region${region}_keylen${keylen}_kvpair${kvpair}_version${version}"
timestamp=$(date +%Y%m%d_%H%M%S)  # 添加时间戳

cd ../                    # 切换到项目根目录
./build_debug.sh --cxx g++   # 编译项目（debug模式）
cd exps/                  # 回到实验目录
mkdir -p results          # 创建结果目录

set -x
# 清理并创建数据和索引目录
rm -rf "$data_path"
mkdir -p "$data_path"
rm -rf "$index_path"
mkdir -p "$index_path"

# 使用 perf 采集 put_obs 程序的性能数据
sudo perf record -g -F 99 ../build_debug/bin/put_obs "$region" "$keylen" "$kvpair" "$version"

# 生成 perf 的中间文件（perf.folded）和火焰图
sudo perf script | ./FlameGraph/stackcollapse-perf.pl > perf.folded
./FlameGraph/flamegraph.pl perf.folded > "results/flamegraph_${param_suffix}_${timestamp}.svg"

# 生成性能报告
sudo perf report --stdio > "results/perf_report_${param_suffix}_${timestamp}.txt"

# 清理中间文件
rm -f perf.data perf.folded
echo "中间文件已清理完成"
echo "结果文件已生成："
echo "  - results/flamegraph_${param_suffix}_${timestamp}.svg"
echo "  - results/perf_report_${param_suffix}_${timestamp}.txt"

# python3 plot.py get_put_hashed_key