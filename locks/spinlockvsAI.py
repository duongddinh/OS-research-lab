import numpy as np
import matplotlib.pyplot as plt

N_train = 2000   
N_test = 10000   
alpha = 0.1      
P = 5.0         

train_times = np.random.exponential(scale=1.0, size=N_train)

pred_initial = 1.0
for h in train_times:
    pred_initial = alpha * h + (1 - alpha) * pred_initial

short_times = np.random.exponential(scale=1.0, size=int(N_test * 0.8))
long_times = np.random.exponential(scale=5.0, size=int(N_test * 0.2))
test_times = np.hstack((short_times, long_times))
np.random.shuffle(test_times)

spin_costs = test_times.copy()
avg_spin_cost = np.mean(spin_costs)

T_heuristic = np.percentile(test_times, 90)
pred = pred_initial
costs_heuristic = []
for h in test_times:
    if pred <= T_heuristic:
        cost = h if h <= T_heuristic else (T_heuristic + P)
    else:
        cost = P
    costs_heuristic.append(cost)
    pred = alpha * h + (1 - alpha) * pred
avg_heuristic_cost = np.mean(costs_heuristic)

T_values = np.linspace(0.1, 10.0, 100)
avg_costs = []
for T in T_values:
    pred = pred_initial
    costs = []
    for h in test_times:
        if pred <= T:
            costs.append(h if h <= T else (T + P))
        else:
            costs.append(P)
        pred = alpha * h + (1 - alpha) * pred
    avg_costs.append(np.mean(costs))

avg_costs = np.array(avg_costs)
best_idx = np.argmin(avg_costs)
T_best = T_values[best_idx]
avg_tuned_cost = avg_costs[best_idx]

labels = ["Normal Spinlock", f"Heuristic AI (T={T_heuristic:.2f})", f"Tuned AI (T={T_best:.2f})"]
values = [avg_spin_cost, avg_heuristic_cost, avg_tuned_cost]

plt.figure(figsize=(8, 5))
bars = plt.bar(labels, values, color=['#1f77b4', '#ff7f0e', '#2ca02c'])
plt.ylabel("Average Wait Cost (time units)")
plt.title("Spinlock vs AI Spinlock (Heuristic vs Tuned)")

for bar, val in zip(bars, values):
    plt.text(bar.get_x() + bar.get_width() / 2, val + 0.05, f"{val:.2f}", ha='center')

plt.tight_layout()
plt.show()
