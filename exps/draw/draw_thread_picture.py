# 简化版线程性能测试绘图脚本
# 依赖：matplotlib, numpy
import matplotlib.pyplot as plt
import numpy as np
import argparse

# 读取结果文件，返回线程数和执行时间列表
def read_results_file(file_path):
    threads, times = [], []
    with open(file_path, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            if ',' in line:
                t, tm = line.split(',')
                threads.append(int(t))
                times.append(float(tm))
    return threads, times

# 检查数据是否连续（线程数间隔是否为1）
def check_continuous(thread_nums):
    if len(thread_nums) <= 1:
        return True
    for i in range(1, len(thread_nums)):
        if thread_nums[i] - thread_nums[i-1] != 1:
            return False
    return True

# 对采样数据进行插值
def interpolate_data(thread_nums, exec_times):
    if len(thread_nums) <= 1:
        return thread_nums, exec_times

    # 创建连续的线程数范围
    min_thread = min(thread_nums)
    max_thread = max(thread_nums)
    interp_threads = list(range(min_thread, max_thread + 1))

    # 使用线性插值
    interp_times = np.interp(interp_threads, thread_nums, exec_times)

    return interp_threads, interp_times

# 读取包含多组结果的文件。组与组之间以一行空行分隔。
def read_results_multiple(file_path):
    groups = []
    cur_threads, cur_times = [], []
    with open(file_path, 'r') as f:
        for raw in f:
            line = raw.strip()
            # 空行作为组分隔符
            if line == '':
                if cur_threads:
                    groups.append((cur_threads, cur_times))
                    cur_threads, cur_times = [], []
                continue
            if line.startswith('#'):
                continue
            if ',' in line:
                t, tm = line.split(',')
                cur_threads.append(int(t))
                cur_times.append(float(tm))
    if cur_threads:
        groups.append((cur_threads, cur_times))
    return groups

# 绘制性能图表，支持多条曲线
def plot(thread_nums, exec_times, baseline=63.32, title=None, output=None, labels=None, interpolate=False, sampled_groups=None):
    plt.figure(figsize=(12,8))

    # 预定义颜色表，用于尽量保持主曲线和采样点颜色一致
    colors = ['C0', 'C1', 'C2', 'C3', 'C4']

    # 如果传入的是多组数据，绘制多条曲线
    if isinstance(exec_times[0], (list, tuple)):
        series = exec_times
        threads_ref = thread_nums
        for idx, ser in enumerate(series):
            lbl = (labels[idx] if labels and idx < len(labels) else f'Series {idx+1}')

            # 检查数据是否连续
            is_continuous = check_continuous(threads_ref)
            if not is_continuous and interpolate:
                # 进行插值
                interp_threads, interp_times = interpolate_data(threads_ref, ser)
                # 绘制插值曲线
                plt.plot(interp_threads, interp_times, linestyle='-', alpha=0.7, color=colors[idx % len(colors)], linewidth=2)
                # 绘制原始数据点
                plt.plot(threads_ref, ser, marker='o', linestyle='', label=lbl, color=colors[idx % len(colors)], markersize=6)
            else:
                plt.plot(threads_ref, ser, marker='o', linestyle='-', label=lbl, color=colors[idx % len(colors)], markersize=6, linewidth=2)
    else:
        # 检查数据是否连续
        is_continuous = check_continuous(thread_nums)
        if not is_continuous and interpolate:
            # 进行插值
            interp_threads, interp_times = interpolate_data(thread_nums, exec_times)
            # 绘制插值曲线
            plt.plot(interp_threads, interp_times, linestyle='-', alpha=0.7, color='C0', linewidth=2)
            # 绘制原始数据点
            plt.plot(thread_nums, exec_times, marker='o', linestyle='', label='Execution Time', color='C0', markersize=6)
        else:
            plt.plot(thread_nums, exec_times, 'bo-', label='Execution Time', markersize=6, linewidth=2)

    plt.axhline(y=baseline, color='r', linestyle='--', linewidth=2, label=f'Baseline {baseline}s')

    # 如果提供了采样文件的数据，在主曲线之上叠加采样点
    if sampled_groups:
        try:
            # 多条曲线时，尝试按序列对应颜色叠加采样点
            if isinstance(exec_times[0], (list, tuple)):
                series_count = len(exec_times)
                if len(sampled_groups) == series_count:
                    for idx, (s_threads, s_times) in enumerate(sampled_groups):
                        plt.scatter(s_threads, s_times, color=colors[idx % len(colors)], marker='x', s=80, zorder=10, label=(f'Sampled {idx+1}' if idx==0 else None))
                elif len(sampled_groups) == 1:
                    s_threads, s_times = sampled_groups[0]
                    plt.scatter(s_threads, s_times, color='k', marker='x', s=80, zorder=10, label='Sampled')
                else:
                    for i, (s_threads, s_times) in enumerate(sampled_groups):
                        plt.scatter(s_threads, s_times, color='k', marker='x', s=80, zorder=10, label=("Sampled" if i==0 else None))
            else:
                # 单条曲线，直接绘制采样点
                for i, (s_threads, s_times) in enumerate(sampled_groups):
                    plt.scatter(s_threads, s_times, color='k', marker='x', s=80, zorder=10, label=("Sampled" if i==0 else None))
        except Exception:
            # 不要因为采样覆盖导致主绘图失败，静默处理
            pass

    # 设置坐标轴
    plt.xlabel('Thread Number', fontsize=12)
    plt.ylabel('Execution Time (s)', fontsize=12)
    plt.title(title or 'Thread Performance Analysis', fontsize=14, pad=20)

    # 横坐标：每个线程数对应一个刻度
    plt.xticks(thread_nums, fontsize=8)

    # 纵坐标：更细分的刻度
    y_min, y_max = min([min(s) if isinstance(s, (list, tuple)) else s for s in (exec_times if isinstance(exec_times[0], (list, tuple)) else [exec_times])]) , max([max(s) if isinstance(s, (list, tuple)) else s for s in (exec_times if isinstance(exec_times[0], (list, tuple)) else [exec_times])])
    y_range = y_max - y_min
    if y_range > 0:
        # 计算合适的刻度间隔
        if y_range <= 1:
            tick_interval = 0.1
        elif y_range <= 5:
            tick_interval = 0.5
        elif y_range <= 10:
            tick_interval = 1
        elif y_range <= 50:
            tick_interval = 5
        else:
            tick_interval = 10

        # 设置纵坐标范围和刻度
        y_min_tick = (int(y_min / tick_interval) - 1) * tick_interval
        y_max_tick = (int(y_max / tick_interval) + 2) * tick_interval
        yticks = np.arange(y_min_tick, y_max_tick, tick_interval)
        plt.yticks(yticks)

        # 设置纵坐标显示范围
        plt.ylim(y_min_tick, y_max_tick)

    # 添加网格和图例
    plt.grid(True, alpha=0.3, linestyle='-', linewidth=0.5)
    plt.legend(fontsize=10)

    # 调整布局
    plt.tight_layout()

    if output:
        plt.savefig(output, dpi=300, bbox_inches='tight')
    else:
        plt.show()
    plt.close()

def main():
    parser = argparse.ArgumentParser(description='线程性能测试图表')
    parser.add_argument('--input', '-i', type=str, help='输入结果文件路径')
    parser.add_argument('--output', '-o', type=str, help='输出图片文件路径')
    parser.add_argument('--baseline', '-b', type=float, default=63.32, help='基准线时间')
    parser.add_argument('--title', type=str, default='', help='图表标题')
    parser.add_argument('--sample', '-s', action='store_true', help='使用示例数据')
    parser.add_argument('--interpolate', action='store_true', help='对采样数据进行插值以显示完整曲线')
    parser.add_argument('--sampled', type=str, default=None, help='采样结果文件路径（可选），用于在主曲线之上叠加采样点）')
    args = parser.parse_args()

    # 示例数据或文件数据
    if args.sample:
        thread_nums = list(range(1, 65))
        exec_times = [100.0 if t==1 else 100.0*(0.3**((t-1)/7)) if t<=8 else 22.0+np.random.normal(0,1.5) for t in thread_nums]
        exec_times = [max(18, min(26, tm)) if t>8 else tm for t,tm in zip(thread_nums,exec_times)]
        # 生成第二组示例以演示双曲线绘制
        exec_times2 = [et * (0.95 + 0.05 * np.random.randn()) for et in exec_times]
        plot(thread_nums, [exec_times, exec_times2], baseline=args.baseline, title=args.title, output=args.output, labels=['Run A','Run B'], interpolate=args.interpolate)
    elif args.input:
        groups = read_results_multiple(args.input)
        if len(groups) == 0:
            print('No data found in input file.'); return
        # 如果只有一组，保持兼容行为
        if len(groups) == 1:
            thread_nums, exec_times = groups[0]
            sampled_groups = None
            if args.sampled:
                sampled_groups = read_results_multiple(args.sampled)
            plot(thread_nums, exec_times, baseline=args.baseline, title=args.title, output=args.output, interpolate=args.interpolate, sampled_groups=sampled_groups)
        else:
            # 假设每组使用相同的 thread_nums（通常成立），使用第一组的线程刻度
            thread_nums = groups[0][0]
            series = [g[1] for g in groups]
            labels = [f'Run {i+1}' for i in range(len(series))]
            sampled_groups = None
            if args.sampled:
                sampled_groups = read_results_multiple(args.sampled)
            plot(thread_nums, series, baseline=args.baseline, title=args.title, output=args.output, labels=labels, interpolate=args.interpolate, sampled_groups=sampled_groups)
    else:
        parser.print_help(); return

    # 如果没有提前退出
    if args.sample:
        return

if __name__ == '__main__':
    main()
