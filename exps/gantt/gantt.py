import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.patches import Patch
from pandas import Timestamp
from TSReader import ReadGanttFile

##### DATA #####
region_number = 6
commit_number = 16
kvpair_number = 100000
key_size = 16

print("Reading gantt file...")
data, start = ReadGanttFile(region_number+1, commit_number)
print(f"Found {len(data['Task'])} workers")
print(f"Start time: {start}")

c_dict = {1: '#1f3a61', 2: '#3b6a8c', 3: '#7a9cb3', 4: '#efc14d', 5: '#e15656',
          6: "#2e8a56", 7:'#50b99f', 8:'#a3d55d', 9:'#f3e77c', 10:'#f0a800',
          11: '#1f3a61', 12: '#3b6a8c', 13: '#7a9cb3', 14: '#efc14d', 15: '#e15656',
          16: "#2e8a56", 17:'#50b99f', 18:'#a3d55d', 19:'#f3e77c', 20:'#f0a800'}

# c_dict = {'Region 0': '#E64646', 'Region 1': '#E69646', 'Region 2': '#34D05C', 'Region 3': '#34D0C3', 'Region 4': '#3475D0', 'Region 5': '#D034D0', 'Joiner': '#D0A634'}
fig, ax = plt.subplots(1, figsize=(11,4))

for key, value in data['Task'].items():
    for commit_id in value['Start']:
        ax.barh(value['Name'], value['End'][commit_id] - value['Start'][commit_id], left=value['Start'][commit_id] - start, color=c_dict[commit_id], alpha=0.5)
    
plt.title(f"Gantt Chart of DMMTree Commit ({region_number} Regions, {commit_number} Commits, {kvpair_number} KVPairs/Commit of KeyLen {key_size}), Async Disabled")
plt.xlabel("Execution Time (ms)")
plt.ylabel("Worker Thread")
# ##### DATA PREP #####
# df = pd.DataFrame(data)

# # project start date
# proj_start = df.Task.Start.min()

# # number of days from project start to task start
# df['start_num'] = (df.Start - proj_start)

# # number of days from project start to end of tasks
# df['end_num'] = (df.End - proj_start)

# # days between start and end of each task
# df['days_start_to_end'] = df.end_num - df.start_num




# # create a column with the color for each department
# def color(row):
#     c_dict = {'Region 0': '#E64646', 'Region 1': '#E69646', 'Region 2': '#34D05C', 'Region 3': '#34D0C3', 'Region 4': '#3475D0', 'Region 5': '#D034D0', 'Joiner': '#D0A634'}
#     return c_dict[row['Worker']]


# df['color'] = df.apply(color, axis=1)


# from matplotlib.patches import Patch
# fig, ax = plt.subplots(1, figsize=(32,12))
# # bars
# ax.barh(df.Task, df.current_num, left=df.start_num, color=df.color)
# # ax.barh(df.Task, df.days_start_to_end, left=df.start_num, color=df.color, alpha=0.5)
# # # texts
# # for idx, row in df.iterrows():
# #     ax.text(row.end_num+0.1, idx,
# #             f"{row.Task}",
# #             va='center', alpha=0.8)
# ##### LEGENDS #####
# c_dict = {'Region 0': '#E64646', 'Region 1': '#E69646', 'Region 2': '#34D05C', 'Region 3': '#34D0C3', 'Region 4': '#3475D0', 'Region 5': '#D034D0', 'Joiner': '#D0A634'}
# legend_elements = [Patch(facecolor=c_dict[i], label=i)  for i in c_dict]
# plt.legend(handles=legend_elements)
##### TICKS #####
# xticks = np.arange(0, df.end_num.max()+1, 3)
# xticks_labels = pd.date_range(proj_start, end=df.End.max()).strftime("%m/%d")
# xticks_minor = np.arange(0, df.end_num.max()+1, 1)
# ax.set_xticks(xticks)
# ax.set_xticks(xticks_minor, minor=True)
# ax.set_xticklabels(xticks_labels[::3])

print("Saving gantt chart...")
plt.savefig('gantt_chart.png', dpi=300, bbox_inches='tight')
print("Gantt chart saved as gantt_chart.png")

# 如果环境支持，也尝试显示
try:
    plt.show()
except:
    print("Cannot display chart (no GUI), but saved to gantt_chart.png")