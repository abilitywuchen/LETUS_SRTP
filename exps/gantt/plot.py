#!/usr/bin/env python3
"""
plot_compare.py

读取一个或多个 metrics CSV（由 compute_metrics.py 生成）或一个包含多次运行输出的拼接 CSV，
绘制折线对比图。

用法示例：
    python plot_compare.py metrics_baseline.csv metrics_opt.csv --out compare.png

如果传入多个文件，会把它们按 run_label 区分并绘制多条线。
也支持传入一个已拼接好的 CSV（含 run_label 列）。

输出：compare_<timestamp>.png 或用户指定的 --out
"""
import argparse
import os
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from datetime import datetime


def load_metrics(files):
    # read multiple files and concatenate
    dfs = []
    for f in files:
        df = pd.read_csv(f)
        # if run_label not in columns, try to infer from filename
        if 'run_label' not in df.columns:
            label = os.path.splitext(os.path.basename(f))[0]
            df.insert(0, 'run_label', label)
        dfs.append(df)
    all_df = pd.concat(dfs, ignore_index=True, sort=False)
    return all_df


def plot(all_df, out_path=None):
    # metrics to plot
    # use simplified metrics: region_max, region_avg, and single joiner_ms
    metrics = [
        ('region_max_ms', 'Region max (ms)'),
        ('region_avg_ms', 'Region avg (ms)'),
        ('joiner_ms', 'Joiner (ms)')
    ]

    runs = all_df.run_label.unique()
    commits = sorted(all_df.commit_id.unique())

    # dynamic subplot layout to avoid empty axes
    n_metrics = len(metrics)
    ncols = n_metrics if n_metrics <= 3 else 3
    nrows = (n_metrics + ncols - 1) // ncols
    fig, axes = plt.subplots(nrows, ncols, figsize=(4 * ncols, 4 * nrows), constrained_layout=True)
    # flatten axes to 1D list for easy iteration
    if isinstance(axes, np.ndarray):
        axes = axes.flatten()
    else:
        axes = [axes]

    for ax, (col, title) in zip(axes, metrics):
        for run in runs:
            sub = all_df[all_df.run_label == run].sort_values('commit_id')
            # ensure we have values for all commits (fill with NaN if missing)
            vals = []
            for cid in commits:
                row = sub[sub.commit_id == cid]
                if not row.empty:
                        # Be tolerant: older files might have joiner_max_ms/joiner_avg_ms
                        if col not in row.columns:
                            if col == 'joiner_ms':
                                # prefer joiner_ms, otherwise fallback to joiner_max_ms or joiner_avg_ms
                                val = None
                                for fallback in ('joiner_ms', 'joiner_max_ms', 'joiner_avg_ms'):
                                    if fallback in row.columns and not pd.isna(row.iloc[0][fallback]):
                                        val = float(row.iloc[0][fallback])
                                        break
                                vals.append(val if val is not None else np.nan)
                            else:
                                vals.append(np.nan)
                        else:
                            vals.append(float(row.iloc[0][col]) if not pd.isna(row.iloc[0][col]) else np.nan)
                else:
                    vals.append(np.nan)
            ax.plot(commits, vals, marker='o', label=run)
        ax.set_title(title)
        ax.set_xlabel('commit id')
        ax.set_xticks(commits)
        ax.grid(alpha=0.2)
        ax.legend()

    # hide any unused axes (when metrics < grid cells)
    for i in range(len(metrics), len(axes)):
        try:
            axes[i].axis('off')
        except Exception:
            pass

    suptitle = 'Compare runs: ' + ', '.join(runs)
    fig.suptitle(suptitle)

    if out_path is None:
        out_path = f"compare_{datetime.now().strftime('%Y%m%d_%H%M%S')}.png"
    fig.savefig(out_path, dpi=200)
    print(f"Saved comparison plot to {os.path.abspath(out_path)}")


def main():
    p = argparse.ArgumentParser(description='Plot comparison of metrics CSVs')
    p.add_argument('files', nargs='+', help='metrics CSV files or concatenated CSV')
    p.add_argument('--out', help='output PNG path')
    args = p.parse_args()

    all_df = load_metrics(args.files)
    plot(all_df, args.out)


if __name__ == '__main__':
    main()
