import random
import numpy as np
import matplotlib.pyplot as plt
import pandas as pd
from collections import deque
from sklearn.linear_model import SGDClassifier

def generate_tasks(num_tasks=750, seed=0, profile="bursty"):
    rng = random.Random(seed)
    tasks, now = [], 0.0

    for tid in range(num_tasks):
        now += rng.expovariate(1 / 30)  # inter-arrival ~30
        if profile == "bursty":
            if rng.random() < 0.9:  # 90 % “mice”
                burst = max(1, rng.expovariate(1 / 30))
            else:  # 10 % “elephants”
                burst = max(50, rng.expovariate(1 / 300))
        elif profile == "heavy":
            u = rng.random()
            burst = 5 / (u ** (1/1.5))  # Pareto α=1.5
        else:  # simple exp
            burst = max(1, rng.expovariate(1 / 60))

        tasks.append({"id": tid, "arrival": now, "burst": burst})
    return sorted(tasks, key=lambda t: t["arrival"])

class Task:
    __slots__ = ("id","arrival","remaining","start","end","vruntime","weight")
    def __init__(self, tid, arr, burst, weight=1):
        self.id = tid
        self.arrival = arr
        self.remaining = burst
        self.start = None
        self.end = None
        self.vruntime = 0.0
        self.weight = weight

class Scheduler:
    def __init__(self, name, quantum=10):
        self.name = name
        self.quantum = quantum

    def run(self, raw):
        tasks = [Task(t["id"], t["arrival"], t["burst"], t.get("weight",1))
                 for t in raw]
        ready, finished, t = deque(), [], 0.0
        idx, n = 0, len(tasks)

        def admit(up_to):
            nonlocal idx
            while idx < n and tasks[idx].arrival <= up_to:
                ready.append(tasks[idx])
                idx += 1

        while len(finished) < n:
            admit(t)
            if not ready:
                t = tasks[idx].arrival
                admit(t)

            task = self.pick(ready, t)
            slice_len = min(self.slice(task, ready), task.remaining)

            if task.start is None:
                task.start = t
            t += slice_len
            task.remaining -= slice_len
            self.update(task, slice_len)

            admit(t)
            if task.remaining <= 1e-9:
                task.end = t
                finished.append(task)
            else:
                ready.append(task)

        waits  = [ts.start - ts.arrival for ts in finished]
        turns  = [ts.end   - ts.arrival for ts in finished]
        return {
            "sched": self.name,
            "avg_turn": np.mean(turns),
            "p95_turn": np.percentile(turns, 95),
            "avg_wait": np.mean(waits),
        }

    # overridables
    def pick (self, ready, now):  return ready.popleft()
    def slice(self, task, ready): return self.quantum
    def update(self, task, sl):   pass

class RoundRobin(Scheduler):
    pass

class RoundRobinML(Scheduler):
    def __init__(self, quantum=10):
        super().__init__(f"RR+ML(q={quantum})", quantum)
        self.clf = SGDClassifier(loss="log_loss")
        self.ready = False

    def pick(self, ready, now):
        if not self.ready or len(ready) == 1:
            return ready.popleft()
        feats = np.array([[t.remaining] for t in ready])
        idx   = int(np.argmax(self.clf.predict_proba(feats)[:, 1]))
        task  = ready[idx]
        ready.remove(task)
        return task

    def update(self, task, sl):
        X = np.array([[task.remaining + sl]])
        y = np.array([int(task.remaining <= 1e-9)])
        if not self.ready:
            self.clf.partial_fit(X, y, classes=np.array([0,1]))
        else:
            self.clf.partial_fit(X, y)
        self.ready = True

class CFS(Scheduler):
    def __init__(self, target=80, min_gran=5):
        super().__init__("CFS")
        self.target = target
        self.min_gran = min_gran

    def pick(self, ready, _now):
        task = min(ready, key=lambda t: t.vruntime)
        ready.remove(task)
        return task

    def slice(self, task, ready):
        q = max(self.min_gran,
                self.target / max(1, len(ready)+1))  # +1 = current task
        return q

    def update(self, task, sl):
        task.vruntime += sl / task.weight

def run_suite(profiles=("bursty","heavy"), quanta=(5,10,20), seeds=range(3)):
    out = []
    for prof in profiles:
        for s in seeds:
            trace = generate_tasks(profile=prof, seed=s)
            scheds = [CFS()] + \
                     [RoundRobin(f"RR(q={q})",q)     for q in quanta] + \
                     [RoundRobinML(q)                for q in quanta]
            for sch in scheds:
                res = sch.run(trace)
                res.update({"profile":prof, "seed":s})
                out.append(res)
    return out

# run the suite
df = pd.DataFrame(run_suite())

# pivot table (for a combined plot later)
pivot = df.pivot_table(
    index="sched", columns="profile", values="avg_turn", aggfunc="mean"
).round(1)

print("\nAverage Turnaround Time (lower is better):")
print(pivot)

# — Separate per-profile bar charts —
for prof in df.profile.unique():
    subset = df[df.profile == prof]
    bar = subset.groupby("sched")["avg_turn"].mean()
    plt.figure(figsize=(6,3))
    bar.plot(kind="bar")
    plt.title(f"{prof} workload – lower is better")
    plt.ylabel("Avg turnaround")
    plt.xticks(rotation=45, ha="right")
    plt.tight_layout()
    plt.show()
