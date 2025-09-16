#!/usr/bin/env python3
"""
主曲线：不插值，支持多条（实线）
采样曲线：必插值，支持多条（虚线+×号）
"""
import matplotlib.pyplot as plt
import numpy as np
import argparse

# ---------- 工具函数 ----------
def read_results_file(file_path):
    t, tm = [], []
    with open(file_path, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            if ',' in line:
                a, b = line.split(',')
                t.append(int(a))
                tm.append(float(b))
    return t, tm


def read_results_multiple(file_path):
    groups, cur_t, cur_tm = [], [], []
    cur_group_name = None
    with open(file_path, 'r', encoding='utf-8') as f:
        for raw in f:
            line = raw.strip()
            if line == '':
                if cur_t:
                    groups.append((cur_t, cur_tm, cur_group_name))
                    cur_t, cur_tm = [], []
                    cur_group_name = None
                continue
            if line.startswith('#'):
                # 如果已经有数据但还没保存，先保存之前的组
                if cur_t:
                    groups.append((cur_t, cur_tm, cur_group_name))
                    cur_t, cur_tm = [], []
                # 设置新的组名（去掉开头的#和空格）
                cur_group_name = line[1:].strip()
                continue
            if ',' in line:
                a, b = line.split(',')
                cur_t.append(int(a))
                cur_tm.append(float(b))
    if cur_t:
        groups.append((cur_t, cur_tm, cur_group_name))
    return groups


# ---------- 绘图 ----------
def plot(thread_nums, exec_times, baseline, title, output, sampled_groups, main_labels=None, sampled_labels=None):
    plt.figure(figsize=(12, 8))
    colors = ['C0', 'C1', 'C2', 'C3', 'C4', 'C5', 'C6', 'C7', 'C8', 'C9']

    # 1. 主曲线 —— 不插值，原样绘制（支持多条）
    if isinstance(exec_times[0], (list, tuple)):          # 多条主曲线
        for idx, ser in enumerate(exec_times):
            if main_labels and idx < len(main_labels):
                lbl = main_labels[idx]
            else:
                lbl = f'Main {idx+1}'
            plt.plot(thread_nums, ser, 'o-', color=colors[idx % len(colors)],
                     linewidth=2, markersize=6, label=lbl)
    else:                                                 # 单条主曲线
        lbl = main_labels[0] if main_labels and len(main_labels) > 0 else 'Main'
        plt.plot(thread_nums, exec_times, 'o-', color='C0',
                 linewidth=2, markersize=6, label=lbl)

    # 2. 采样曲线 —— 必插值（支持多条）
    if sampled_groups:
        markers = ['x', '+', 's', '^', 'v', 'D', 'p', '*', 'h', 'H']
        for idx, group_data in enumerate(sampled_groups):
            # 解包数据（支持带名称和不带名称两种格式）
            if len(group_data) == 3:
                s_t, s_y, group_name = group_data
            else:
                s_t, s_y = group_data
                group_name = None
            
            if len(s_t) < 2:
                continue
            
            # 确定标签
            if sampled_labels and idx < len(sampled_labels):
                lbl = sampled_labels[idx]
            elif group_name:
                lbl = group_name
            else:
                lbl = f'Sampled {idx+1}'
            
            # 插值绘制虚线
            fine_t = np.arange(min(s_t), max(s_t) + 1)
            fine_y = np.interp(fine_t, s_t, s_y)
            plt.plot(fine_t, fine_y, '--', linewidth=2,
                     color=colors[(idx+2) % len(colors)], alpha=0.8,
                     label=lbl)
            # 原始标记点
            marker = markers[idx % len(markers)]
            plt.scatter(s_t, s_y, color=colors[(idx+2) % len(colors)],
                        marker=marker, s=80, zorder=10)

    # 3. 基准线
    plt.axhline(y=baseline, color='r', linestyle=':', linewidth=2,
                label=f'Baseline {baseline}s')

    # 4. 装饰
    plt.xlabel('Thread Number')
    plt.ylabel('Execution Time (s)')
    plt.title(title or 'Thread Performance (main + sampled interpolation)')
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()

    if output:
        plt.savefig(output, dpi=300, bbox_inches='tight')
    else:
        plt.show()
    plt.close()


# ---------- 命令行 ----------
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-i', '--input', required=True,
                        help='主曲线文件，可多空行分组')
    parser.add_argument('--sampled', required=True,
                        help='采样文件，可多空行分组')
    parser.add_argument('-o', '--output', required=True)
    parser.add_argument('-b', '--baseline', type=float, default=95.03)
    parser.add_argument('--title', default='')
    args = parser.parse_args()

    main_groups = read_results_multiple(args.input)
    if not main_groups:
        print('主曲线无数据'); return
    
    # 提取主曲线数据和标签
    main_labels = []
    main_data = []
    for group_data in main_groups:
        if len(group_data) == 3:
            threads, times, group_name = group_data
            main_data.append((threads, times))
            main_labels.append(group_name if group_name else f'Main {len(main_labels)+1}')
        else:
            threads, times = group_data
            main_data.append((threads, times))
            main_labels.append(f'Main {len(main_labels)+1}')
    
    # 统一用第一组的线程刻度当横轴
    thread_nums = main_data[0][0]
    exec_times = [g[1] for g in main_data] if len(main_data) > 1 else main_data[0][1]

    sampled_groups = read_results_multiple(args.sampled)

    plot(thread_nums, exec_times,
         baseline=args.baseline,
         title=args.title,
         output=args.output,
         sampled_groups=sampled_groups,
         main_labels=main_labels)


if __name__ == '__main__':
    main()