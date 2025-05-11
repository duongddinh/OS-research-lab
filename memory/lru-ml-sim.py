import random
from collections import deque, defaultdict

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from sklearn.linear_model import LogisticRegression
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler

def generate_mixed_trace(total_len=1_000_000,
                         hot_set_size=100,
                         cold_scan_size=1_000,
                         scan_interval=5_000,
                         hot_prob=0.9,
                         seed=42):
    rng = random.Random(seed)
    trace, cold_base = [], hot_set_size + 1
    while len(trace) < total_len:
        for _ in range(scan_interval):
            if len(trace) >= total_len:
                break
            if rng.random() < hot_prob:
                trace.append(rng.randint(1, hot_set_size))
            else:
                trace.append(rng.randint(cold_base,
                                         cold_base + cold_scan_size - 1))
        for off in range(cold_scan_size):
            if len(trace) >= total_len:
                break
            trace.append(cold_base + off)
        cold_base += cold_scan_size
    return trace

def simulate_lru(seq, k):
    cache, present, hits = deque(), set(), 0
    for p in seq:
        if p in present:
            hits += 1
            cache.remove(p)
            cache.append(p)
        else:
            if len(cache) == k:
                present.remove(cache.popleft())
            cache.append(p)
            present.add(p)
    return hits

def simulate_fifo(seq, k):
    cache, present, hits = deque(), set(), 0
    for p in seq:
        if p in present:
            hits += 1
        else:
            if len(cache) == k:
                present.remove(cache.popleft())
            cache.append(p)
            present.add(p)
    return hits

def simulate_random(seq, k, seed=0):
    rng = random.Random(seed)
    cache, present, hits = [], set(), 0
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

def build_feature_table(seq, horizon=2_000, unseen=100_000):
    last_seen, cnts, rows = {}, defaultdict(int), []
    for i, page in enumerate(seq):
        rec  = i - last_seen[page] if page in last_seen else unseen
        freq = cnts[page]
        label = 1 if page in seq[i+1 : i+1+horizon] else 0
        rows.append((rec, freq, label))
        last_seen[page] = i
        cnts[page] += 1
    return pd.DataFrame(rows, columns=['recency', 'frequency', 'label'])

def train_logreg(df, train_frac=0.6):
    cut = int(len(df)*train_frac)
    X, y = df[['recency','frequency']][:cut], df['label'][:cut]
    model = make_pipeline(StandardScaler(),
                          LogisticRegression(max_iter=1000))
    model.fit(X, y)
    return model

def simulate_ml(seq, k, model, unseen=100_000):
    cache, present, hits = [], set(), 0
    last_seen, cnts = {}, defaultdict(int)

    for i, page in enumerate(seq):
        if page in present:
            hits += 1
        else:
            if len(cache) == k:
                feats = np.array([[i - last_seen.get(p, -unseen), cnts[p]]
                                  for p in cache])
                probs = model.predict_proba(feats)[:,1]
                evict = int(np.argmin(probs))
                present.remove(cache[evict])
                cache[evict] = page
            else:
                cache.append(page)
            present.add(page)

        last_seen[page] = i
        cnts[page] += 1
    return hits

def run_sweep():
    TOTAL = 1_000_000
    trace = generate_mixed_trace(total_len=TOTAL)

    ft   = build_feature_table(trace)
    ml   = train_logreg(ft)

    for CACHE in [64, 128, 256, 512]:
        hits = {
            'LRU'   : simulate_lru(trace, CACHE),
            'FIFO'  : simulate_fifo(trace, CACHE),
            'Random': simulate_random(trace, CACHE),
            'ML'    : simulate_ml(trace, CACHE, ml),
        }
        misses = {m: TOTAL - h for m, h in hits.items()}

        print(f"\n=== Cache = {CACHE} lines ===")
        for m in hits:
            hr = hits[m] / TOTAL * 100
            print(f"{m:<6}: hits {hits[m]:7,}  misses {misses[m]:7,} "
                  f"| hit‑rate {hr:5.2f}%")

        methods = list(hits.keys())
        x = np.arange(len(methods))
        width = 0.35

        fig, ax = plt.subplots(figsize=(8,4))
        ax.bar(x - width/2, [hits[m]   for m in methods], width, label="Hits")
        ax.bar(x + width/2, [misses[m] for m in methods], width, label="Misses")
        ax.set_xticks(x)
        ax.set_xticklabels(methods)
        ax.set_ylabel("Count")
        ax.set_title(f"Cache Hits vs Misses  (Cache = {CACHE}, Trace = {TOTAL:,})")
        ax.legend()
        for bars in ax.containers:
            ax.bar_label(bars, padding=3, fontsize=8)
        plt.tight_layout()
        plt.show()

if __name__ == "__main__":
    run_sweep()
