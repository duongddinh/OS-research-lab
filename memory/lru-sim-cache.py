import random
from collections import deque, defaultdict
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from sklearn.linear_model import LogisticRegression
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler


def generate_trace(total=300_000,
                   hot_set=100,
                   scan=1_000,
                   interval=5_000,
                   hot_p=0.9,
                   seed=42):
    rng, trace, base = random.Random(seed), [], hot_set + 1
    while len(trace) < total:
        # hot chunk
        for _ in range(interval):
            if len(trace) >= total:
                break
            if rng.random() < hot_p:
                trace.append(rng.randint(1, hot_set))
            else:
                trace.append(rng.randint(base, base + scan - 1))
        # full cold scan
        for off in range(scan):
            if len(trace) >= total:
                break
            trace.append(base + off)
        base += scan
    return trace


def lru(seq, k):
    cache, present, hits = deque(), set(), 0
    for p in seq:
        if p in present:
            hits += 1
            cache.remove(p); cache.append(p)
        else:
            if len(cache) == k:
                present.remove(cache.popleft())
            cache.append(p); present.add(p)
    return hits

def fifo(seq, k):
    cache, present, hits = deque(), set(), 0
    for p in seq:
        if p in present:
            hits += 1
        else:
            if len(cache) == k:
                present.remove(cache.popleft())
            cache.append(p); present.add(p)
    return hits

def rand(seq, k, seed=0):
    rng, cache, present, hits = random.Random(seed), [], set(), 0
    for p in seq:
        if p in present:
            hits += 1
        else:
            if len(cache) == k:
                victim = rng.randrange(k)
                present.remove(cache[victim])
                cache[victim] = p
            else:
                cache.append(p)
            present.add(p)
    return hits

def feature_table(seq, horizon=2_000, unseen=100_000):
    last, cnt, rows = {}, defaultdict(int), []
    for i, pg in enumerate(seq):
        rec  = i - last[pg] if pg in last else unseen
        freq = cnt[pg]
        label = 1 if pg in seq[i+1:i+1+horizon] else 0
        rows.append((rec, freq, label))
        last[pg] = i; cnt[pg] += 1
    return pd.DataFrame(rows, columns=['recency', 'frequency', 'label'])

def train_logreg(df, cut=0.6):
    split = int(len(df)*cut)
    X, y = df[['recency','frequency']][:split], df['label'][:split]
    pipe = make_pipeline(StandardScaler(),
                         LogisticRegression(max_iter=1000))
    pipe.fit(X, y)
    return pipe

def ml_eviction(seq, k, model, unseen=100_000):
    cache, present, hits = [], set(), 0
    last, cnt = {}, defaultdict(int)
    for i, pg in enumerate(seq):
        if pg in present:
            hits += 1
        else:
            if len(cache) == k:
                feats = np.array([[i - last.get(p, -unseen), cnt[p]] for p in cache])
                evict = int(np.argmin(model.predict_proba(feats)[:,1]))
                present.remove(cache[evict]); cache[evict] = pg
            else:
                cache.append(pg)
            present.add(pg)
        last[pg] = i; cnt[pg] += 1
    return hits


TOTAL  = 300_000
TRACE  = generate_trace(total=TOTAL)
MODEL  = train_logreg(feature_table(TRACE))

sizes  = [64, 128, 256, 512]
pols   = ['LRU', 'FIFO', 'Random', 'ML']
hits   = {p: [] for p in pols}

for k in sizes:
    hits['LRU'].append(lru(TRACE, k)          / TOTAL * 100)
    hits['FIFO'].append(fifo(TRACE, k)        / TOTAL * 100)
    hits['Random'].append(rand(TRACE, k)      / TOTAL * 100)
    hits['ML'].append(ml_eviction(TRACE, k, MODEL) / TOTAL * 100)

fig, ax = plt.subplots(figsize=(8, 5))
for p in pols:
    ax.plot(sizes, hits[p], marker='o', label=p)
ax.set_xlabel("Cache size (lines)")
ax.set_ylabel("Hit‑rate (%)")
ax.set_title("Hit‑rate vs. Cache Size for Four Eviction Policies")
ax.set_xticks(sizes)
ax.set_ylim(0, 100)
ax.legend()
plt.tight_layout()
plt.show()
