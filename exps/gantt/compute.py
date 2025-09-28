#!/usr/bin/env python3
"""
compute_metrics.py

读取与 gantt.py 相同的输入（使用 TSReader.ReadGanttFile），计算每个 commit 下：
- region 的最长用时（ms）和平均用时（ms）
- joiner 的最长用时（ms）和平均用时（ms）

输出 CSV，包含列：run_label, commit_id, region_max_ms, region_avg_ms, region_count, joiner_max_ms, joiner_avg_ms, joiner_count

用法示例：
    python compute_metrics.py --regions 6 --commits 16 --label baseline --out metrics_baseline.csv

"""
import argparse
import os
import numpy as np
import pandas as pd
from TSReader import ReadGanttFile


def compute(region_number: int, commit_number: int):
    # follow previous scripts which called ReadGanttFile(region_number+1, commit_number)
    data, start = ReadGanttFile(region_number + 1, commit_number)
    commit_ids = set()
    for _, v in data['Task'].items():
        commit_ids.update(v['Start'].keys())

    per_commit = {cid: {'region': [], 'joiner': []} for cid in commit_ids}

    for _, v in data['Task'].items():
        name = v.get('Name', '')
        is_joiner = 'joiner' in name.lower()
        role = 'joiner' if is_joiner else 'region'
        for cid, s in v['Start'].items():
            e = v['End'].get(cid, None)
            if e is None:
                continue
            duration = e - s
            per_commit[cid][role].append(duration)

    rows = []
    for cid in sorted(per_commit.keys()):
        region_list = per_commit[cid]['region']
        joiner_list = per_commit[cid]['joiner']
        row = {'commit_id': cid}
        if region_list:
            arr = np.array(region_list, dtype=float)
            row.update({
                'region_max_ms': float(np.max(arr)),
                'region_avg_ms': float(np.mean(arr)),
                'region_count': int(len(arr))
            })
        else:
            row.update({'region_max_ms': np.nan, 'region_avg_ms': np.nan, 'region_count': 0})
        # joiner usually has a single entry per commit; output single joiner_ms value
        if joiner_list:
            arr2 = np.array(joiner_list, dtype=float)
            # use max as representative if multiple (shouldn't normally happen)
            row.update({
                'joiner_ms': float(np.max(arr2)),
                'joiner_count': int(len(arr2))
            })
        else:
            row.update({'joiner_ms': np.nan, 'joiner_count': 0})
        rows.append(row)

    df = pd.DataFrame(rows)
    df.sort_values('commit_id', inplace=True)
    return df


def main():
    p = argparse.ArgumentParser(description='Compute per-commit region/joiner metrics from gantt input')
    p.add_argument('--regions', type=int, default=6, help='number of regions (default: 6)')
    p.add_argument('--commits', type=int, default=16, help='number of commits (default: 16)')
    p.add_argument('--label', type=str, default='run', help='label for this run (used in output CSV)')
    p.add_argument('--out', type=str, default='metrics.csv', help='output CSV path')
    args = p.parse_args()

    df = compute(args.regions, args.commits)
    # add run label
    df.insert(0, 'run_label', args.label)
    out_path = args.out
    df.to_csv(out_path, index=False)
    print(f'Wrote metrics to {os.path.abspath(out_path)}')


if __name__ == '__main__':
    main()
