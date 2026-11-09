# 🍽️ Restaurant Reservation & Kitchen Workflow Optimizer

A single-file, dependency-free **C++17** system that handles both sides of a busy restaurant: the **front of house** (table reservations, party combining, VIP waitlist) and the **back of house** (scheduling dishes across kitchen stations and cooks so food leaves the pass together and fast).

The kitchen scheduler compares three strategies, including a **simulated annealing** optimizer, and ships with a benchmark so every claim is measurable.

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue) ![Dependencies](https://img.shields.io/badge/dependencies-none-green) ![Tests](https://img.shields.io/badge/self--tests-12%2F12%20passing-brightgreen)

---

## ✨ Features

### Floor management
- **Best-fit table assignment**: a party always gets the smallest table that fits, keeping large tables free for large parties.
- **Adjacent-table combining**: parties too big for any single table are seated on two tables from the same zone (e.g. a party of 10 → T5 + T6, 12 seats).
- **Overlap detection**: time-window checks allow back-to-back bookings on the same table without false conflicts.
- **VIP-first waitlist**: when no table is free the party is waitlisted; on cancellation, VIPs are promoted first, then everyone else in arrival order.
- **ASCII floor views**: reservation list, per-table timeline (1 character = 15 minutes) and live "who is at which table at HH:MM" status.

### Kitchen optimization
- **Multi-station, multi-cook model**: Tandoor, Grill, Fry, Salad, Curry and Dessert stations, some with more than one cook.
- **Three scheduling strategies**
  | Strategy | How it works |
  |---|---|
  | **FIFO** | Cook dishes in the order they arrive (naive baseline) |
  | **Greedy** | Per order, longest dish first, assigned to the earliest-free cook |
  | **Anneal** | Simulated annealing over the global dispatch order, minimizing a VIP-weighted wait + hold-time cost |
- **Cost function**: penalizes order wait (with a quadratic term for very late orders), plus *hold time*, the minutes finished food sits waiting for the rest of its order. VIP orders count **3×**.
- **Output**: per-order tickets, an ASCII **Gantt chart**, per-station utilization and a side-by-side strategy comparison.

### Engineering extras
- **Benchmark mode**: random dinner-rush simulator for both the kitchen and the reservation system.
- **Persistence**: plain-text, `|`-separated save / load of the full state.
- **Built-in self-tests**: 12 checks covering best-fit, combining, waitlist promotion, kitchen scheduling, annealing and save/load round-trips.

---

## 🚀 Getting Started

### Build
```bash
g++ -std=c++17 -O2 -o restaurant restaurant_optimizer.cpp
```

### Run
```bash
./restaurant            # interactive menu
./restaurant --demo     # scripted end-to-end demo
./restaurant --bench 50 # benchmark on 50 random dinner rushes
./restaurant --test     # run self-tests
```

### Interactive menu
```
 1. Load demo data          8. Place order
 2. Make reservation        9. Kitchen optimization report
 3. Cancel reservation     10. Benchmark (random rushes)
 4. List reservations      11. Save state
 5. Table timeline         12. Load state
 6. Floor status at time   13. Run self-tests
 7. Show menu               0. Exit
```
Times use 24-hour `HH:MM` format (e.g. `19:30`).

---

## 🏢 Default Setup

**Tables** (id · seats · zone): T1 (2) and T2 (2) in zone 1; T3 (4) and T4 (4) in zone 2; T5 (6) and T6 (6) in zone 3; T7 (8) in zone 4. Only tables in the same zone can be combined.

**Kitchen stations**: Tandoor (1 cook), Grill (2), Fry (1), Salad (1), Curry (2), Dessert (1).

**Menu**: Paneer Tikka, Butter Naan, Grilled Chicken, Veg Burger, French Fries, Spring Rolls, Caesar Salad, Dal Makhani, Butter Chicken, Gulab Jamun (4 to 20 minutes prep each).

---

## 📊 Results

Output from `./restaurant --demo` (8 orders, 37 dishes, one VIP table):

| Strategy | Avg Wait | Max Wait | Avg Hold | VIP Wait | Weighted Cost |
|---|---|---|---|---|---|
| FIFO | 38.0 | 68.0 | 16.2 | 48.0 | 2367.0 |
| Greedy | 37.4 | 63.0 | 15.1 | 48.0 | 2288.6 |
| **Anneal** | **36.0** | **63.0** | **11.7** | **23.0** | **1723.1** |

**Anneal vs FIFO:** wait −5.3%, max wait −7.4%, hold time −28.0%, weighted cost −27.2%. VIP wait drops by more than half.

Output from `./restaurant --bench 50` (50 random rushes, 20 orders over 2.5 h, 15% VIP):

| Strategy | Avg Wait | Max Wait | Avg Hold | Weighted Cost |
|---|---|---|---|---|
| FIFO | 29.9 | 57.3 | 12.5 | 3722.8 |
| Greedy | 29.8 | 57.2 | 12.3 | 3682.9 |
| **Anneal** | **27.8** | 64.3 | **7.7** | **2983.9** |

**Anneal vs FIFO:** avg wait −7.0%, hold time −38.3%, weighted cost −19.8%.

> **Honest trade-off:** because annealing optimizes the *weighted* cost (VIPs and food-hold time), average wait and hold time improve substantially, but the single worst-case order can wait longer (+12.1% max wait in the benchmark). Adjust the cost function in `Kitchen::evaluate` if worst-case fairness matters more than total cost.

**Table combining:** in the reservation benchmark (200 random evenings, 45 requests each), combining raises the seating rate for **parties of 7+ from 16.6% to 24.0%**. Overall seating dips slightly (38.7% → 37.8%) because combining two tables can block several smaller parties.

> Benchmarks use fixed random seeds, so results are reproducible.

---

## 🧠 How It Works

**Table assignment** scans for the smallest free single table that fits the party. If none exists and combining is enabled, it picks the pair of same-zone tables with the smallest total seats that still fits and are both free for the full booking window (default 90 minutes).

**Kitchen scheduling** treats each dish as a task that needs a cook at a specific station. A *dispatch order* (a permutation of all tasks) is simulated: each task goes to the earliest-free cook at its station, starting no earlier than the order's placed time. The cost of that permutation is:

```
cost = Σ orders  w · (wait + 0.08 · wait²)   +   Σ dishes  0.5 · w · hold
       (w = 3 for VIP orders, 1 otherwise)
```

Simulated annealing starts from the greedy schedule and explores swaps and moves of tasks under a cooling temperature (15.0 → 0.02), keeping the best schedule seen, so the result is **never worse than greedy**.

---

## 📁 Project Structure

```
.
├── main.cpp   # entire application (Floor, Kitchen, benchmark, tests, CLI)
└── README.md
```

State is saved to `main.txt` in the working directory.

---

## 🔮 Possible Improvements

- Per-dish dependencies (e.g. naan must finish with the curry)
- Configurable tables, stations and menu from a file
- Dynamic re-optimization as new orders arrive mid-service
- Simple web or GUI front end

---

## 👥 Authors

- **Saumya Koranga**

## 📄 License

Add a license of your choice (e.g. MIT) to the repository.
