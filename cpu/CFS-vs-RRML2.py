import random, numpy as np, matplotlib.pyplot as plt, pandas as pd
from collections import deque
from sklearn.linear_model import SGDClassifier, SGDRegressor

def generate_tasks(num_tasks=750, seed=0, profile="bursty"):
    rng = random.Random(seed)
    tasks, now = [], 0.0
    for tid in range(num_tasks):
        now += rng.expovariate(1 / 30)
        if profile == "bursty":
            burst = rng.expovariate(1 / 30) if rng.random() < 0.9 else rng.expovariate(1 / 300)
        else:  # heavy Pareto
            u = rng.random()
            burst = 5 / (u ** (1/1.5))
        tasks.append({"id": tid, "arrival": now, "burst": max(1, burst)})
    return sorted(tasks, key=lambda t: t["arrival"])

class Task:
    __slots__ = ("id","arrival","remaining","start","end","vruntime","weight")
    def __init__(self, tid, arr, burst, weight=1):
        self.id=tid; self.arrival=arr; self.remaining=burst
        self.start=None; self.end=None; self.vruntime=0.0; self.weight=weight

class Scheduler:
    def __init__(self,name,quantum=10):
        self.name=name; self.quantum=quantum
    def pick(self,ready,now): return ready.popleft()
    def slice(self,task,ready): return self.quantum
    def update(self,task,slice_len): pass
    def run(self,raw):
        tasks=[Task(t["id"],t["arrival"],t["burst"]) for t in raw]
        ready,finished,t=deque(),[],0.0
        idx=0; n=len(tasks)
        def admit(u):
            nonlocal idx
            while idx<n and tasks[idx].arrival<=u:
                ready.append(tasks[idx]); idx+=1
        while len(finished)<n:
            admit(t)
            if not ready:
                t=tasks[idx].arrival; admit(t)
            task=self.pick(ready,t)
            sl=min(self.slice(task,ready),task.remaining)
            if task.start is None: task.start=t
            t+=sl; task.remaining-=sl
            self.update(task,sl)
            admit(t)
            if task.remaining<=1e-9:
                task.end=t; finished.append(task)
            else:
                ready.append(task)
        waits=[ts.start-ts.arrival for ts in finished]
        turns=[ts.end-ts.arrival for ts in finished]
        return {"sched":self.name,"avg_turn":np.mean(turns),"p95_turn":np.percentile(turns,95)}

class CFS(Scheduler):
    def __init__(self,target=80,min_gran=5): super().__init__("CFS"); self.target=target; self.min_gran=min_gran
    def pick(self,ready,now):
        task=min(ready,key=lambda x:x.vruntime); ready.remove(task); return task
    def slice(self,task,ready):
        return max(self.min_gran, self.target/max(1,len(ready)+1))
    def update(self,task,slice_len): task.vruntime+=slice_len

class RoundRobin(Scheduler): pass

class RR_ML2(Scheduler):
    def __init__(self,max_q=20,min_gran=2):
        super().__init__("RR+2×ML")
        self.max_q=max_q; self.min_gran=min_gran
        self.clf=SGDClassifier(loss="log_loss"); self.clf_ready=False
        self.reg=SGDRegressor(max_iter=1,tol=None,learning_rate="constant",eta0=0.01)
        self.reg_ready=False
    def pick(self,ready,now):
        if not self.clf_ready or len(ready)==1: return ready.popleft()
        feats=np.array([[t.remaining] for t in ready])
        probs=self.clf.predict_proba(feats)[:,1]
        idx=int(np.argmax(probs)); task=ready[idx]; ready.remove(task); return task
    def slice(self,task,ready):
        if not self.reg_ready: return self.min_gran
        pred=max(self.min_gran,min(self.max_q, self.reg.predict([[task.remaining]])[0]))
        return pred
    def update(self,task,sl):
        X=np.array([[task.remaining+sl]]); y=np.array([int(task.remaining<=1e-9)])
        if self.clf_ready:
            self.clf.partial_fit(X,y)
        else:
            self.clf.partial_fit(X,y,classes=np.array([0,1])); self.clf_ready=True
        target=task.remaining  
        Xr=np.array([[task.remaining+sl]])
        yr=np.array([target])
        if self.reg_ready:
            self.reg.partial_fit(Xr,yr)
        else:
            self.reg.partial_fit(Xr,yr); self.reg_ready=True

def run(profile):
    trace=generate_tasks(profile=profile,seed=0)
    scheds=[CFS(),RoundRobin("RR",quantum=10),RR_ML2()]
    results=[s.run(trace) for s in scheds]
    return pd.DataFrame(results)

df_bursty=run("bursty"); df_heavy=run("heavy")
print("Bursty results:\n",df_bursty)
print("\nHeavy results:\n",df_heavy)

# plot
for df,prof in [(df_bursty,"bursty"),(df_heavy,"heavy")]:
    plt.figure(figsize=(5,3))
    plt.bar(df.sched, df.avg_turn)
    plt.title(f"{prof} workload – avg turnaround (lower better)")
    plt.ylabel("Avg turnaround"); plt.tight_layout(); plt.show()
