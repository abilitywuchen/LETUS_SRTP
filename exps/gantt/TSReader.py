def ReadGanttFile(worker_number, commit_number):
    # def calc_id(worker_id, commit_id):
    #     if "Region" in worker_id:
    #         id = int(worker_id.split()[1])
    #         return (commit_id-1) * commit_number + id
    #     else:
    #         id = worker_number
    #         return (commit_id-1) * commit_number + id
    start = 1e20
    result = dict()
    result['Task'] = {}
    # result['Task'] = {}
    # result['Task']['Start'] = {}
    # result['Task']['End'] = {}
    # result['Start'] = {}
    # result['End'] = {}
    result['Worker'] = {}
    with open("gantt.txt", "r") as f:
        for line in f:
            worker = line[line.index("[")+1:line.index("]")]
            commit_id = int(line[line.index("<")+1:line.index(">")])
            # commit = worker + ", Commit " + str(commit_id)
            # id = calc_id(worker, commit_id)
            if "Region" in worker:
                id = int(worker.split()[1])
            else:
                id = worker_number
            if id not in result['Task']:
                result['Task'][id] = {'Name':worker, 'Start': {}, 'End': {}}
            result['Worker'][id] = worker
            if "START" in line:
                start_time = int(line.split("|")[1].strip())/1000
                if start_time < start:
                    start = start_time
                result['Task'][id]['Start'][commit_id] = start_time
                # print(
                #     f"ID: {id}, Worker: {worker}, Commit ID: {commit_id}, Task: {commit}, Start: {start_time}")
            elif "DONE" in line:
                end_time = int(line.split("|")[1].strip())/1000
                result['Task'][id]['End'][commit_id] = end_time
                # print(
                #     f"ID: {id}, Worker: {worker}, Commit ID: {commit_id}, Task: {commit}, End: {end_time}")
    return result, start


if __name__ == "__main__":
    worker_number = 7
    commit_number = 9
    gantt_data = ReadGanttFile(worker_number, commit_number)
    print(gantt_data)
