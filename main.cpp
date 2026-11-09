// =====================================================================
//  Restaurant Reservation & Kitchen Workflow Optimizer  (pure C++17)
//
//  Build : g++ -std=c++17 -O2 -o restaurant restaurant_pro.cpp
//  Run   : ./restaurant            interactive menu
//          ./restaurant --demo     scripted end-to-end demo
//          ./restaurant --bench 50 benchmark on 50 random dinner rushes
//          ./restaurant --test     self-tests
//
//  FLOOR   : best-fit table assignment, adjacent-table combining for big
//            parties, overlap detection, VIP-first waitlist promotion,
//            ASCII floor plan + table timeline.
//  KITCHEN : multi-station / multi-cook scheduling. Three strategies:
//              FIFO    - naive: cook in the order items arrive
//              Greedy  - per order, longest dish first, earliest-free cook
//              Anneal  - simulated annealing over the global dispatch
//                        order, minimizing VIP-weighted wait + hold time
//            Output: tickets, ASCII Gantt chart, utilization, comparison.
//  BENCH   : random rush simulator -> measurable % improvement.
//  SAVE    : plain-text state file (save / load).
// =====================================================================
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>
using namespace std;

// ------------------------------ helpers ------------------------------
static int parseTime(const string &s) {
    int h, m;
    if (sscanf(s.c_str(), "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) return -1;
    return h * 60 + m;
}
static string fmtTime(int t) {
    char b[8];
    snprintf(b, sizeof b, "%02d:%02d", (t / 60) % 24, t % 60);
    return b;
}
static vector<string> split(const string &s, char d) {
    vector<string> v; string cur; stringstream ss(s);
    while (getline(ss, cur, d)) v.push_back(cur);
    return v;
}
static string join(const vector<int> &v, char d) {
    string r;
    for (size_t i = 0; i < v.size(); ++i) { if (i) r += d; r += to_string(v[i]); }
    return r;
}
static int readInt(const string &p) {
    string l;
    while (true) {
        cout << p;
        if (!getline(cin, l)) exit(0);
        try { return stoi(l); } catch (...) { cout << "  Enter a number.\n"; }
    }
}
static string readLine(const string &p) {
    string l; cout << p;
    if (!getline(cin, l)) exit(0);
    return l;
}
static int readTime(const string &p) {
    while (true) {
        int t = parseTime(readLine(p));
        if (t >= 0) return t;
        cout << "  Use HH:MM (24h), e.g. 19:30\n";
    }
}
static const char *SYM = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

// ------------------------------ data ---------------------------------
struct Table { int id, cap, zone; };
struct Reservation { int id; string name; int party, start, dur; bool vip; vector<int> tables; };
struct MenuItem { string name; int prep; string station; };
struct Order { int id, resId, placed; bool vip; vector<int> items; };

// ===================================================================
//                               FLOOR
// ===================================================================
class Floor {
public:
    vector<Table> tables;
    vector<Reservation> booked, waitlist;
    int nextId = 1;
    bool combine = true, verbose = true;

    static bool overlap(int s1, int d1, int s2, int d2) { return s1 < s2 + d2 && s2 < s1 + d1; }
    static string tstr(const vector<int> &t) {
        string r;
        for (size_t i = 0; i < t.size(); ++i) { if (i) r += "+"; r += to_string(t[i]); }
        return r;
    }
    const Table *table(int id) const { for (auto &t : tables) if (t.id == id) return &t; return nullptr; }

    bool tableFree(int tid, int s, int d) const {
        for (auto &r : booked)
            if (find(r.tables.begin(), r.tables.end(), tid) != r.tables.end() && overlap(s, d, r.start, r.dur))
                return false;
        return true;
    }
    // best fit: smallest single table; else cheapest pair of tables in the same zone
    vector<int> findTables(int party, int s, int d) const {
        int best = -1;
        for (size_t i = 0; i < tables.size(); ++i) {
            if (tables[i].cap < party || !tableFree(tables[i].id, s, d)) continue;
            if (best < 0 || tables[i].cap < tables[best].cap) best = (int)i;
        }
        if (best >= 0) return {tables[best].id};
        if (!combine) return {};
        int bi = -1, bj = -1, bc = INT_MAX;
        for (size_t i = 0; i < tables.size(); ++i)
            for (size_t j = i + 1; j < tables.size(); ++j) {
                if (tables[i].zone != tables[j].zone) continue;
                int sum = tables[i].cap + tables[j].cap;
                if (sum < party || sum >= bc) continue;
                if (!tableFree(tables[i].id, s, d) || !tableFree(tables[j].id, s, d)) continue;
                bi = (int)i; bj = (int)j; bc = sum;
            }
        if (bi >= 0) return {tables[bi].id, tables[bj].id};
        return {};
    }

    int reserve(const string &name, int party, int start, bool vip = false, bool *ok = nullptr, int dur = 90) {
        Reservation r{nextId++, name, party, start, dur, vip, {}};
        auto t = findTables(party, start, dur);
        if (ok) *ok = !t.empty();
        if (t.empty()) {
            waitlist.push_back(r);
            if (verbose) cout << "  No table free -> #" << r.id << " " << name << (vip ? " (VIP)" : "") << " on WAITLIST\n";
            return r.id;
        }
        r.tables = t;
        booked.push_back(r);
        if (verbose)
            cout << "  Reservation #" << r.id << " confirmed: " << name << (vip ? " (VIP)" : "") << ", party " << party
                 << ", table " << tstr(t) << (t.size() > 1 ? " (combined)" : "") << ", " << fmtTime(start) << "-"
                 << fmtTime(start + dur) << "\n";
        return r.id;
    }

    void promote() {                           // VIPs first, then everyone else, in arrival order
        for (int pass = 0; pass < 2; ++pass)
            for (auto w = waitlist.begin(); w != waitlist.end();) {
                if ((pass == 0) != w->vip) { ++w; continue; }
                auto t = findTables(w->party, w->start, w->dur);
                if (t.empty()) { ++w; continue; }
                w->tables = t;
                booked.push_back(*w);
                if (verbose) cout << "  Waitlist promotion: #" << w->id << " " << w->name << (w->vip ? " (VIP)" : "")
                                  << " -> table " << tstr(t) << "\n";
                w = waitlist.erase(w);
            }
    }
    bool cancel(int id) {
        auto it = find_if(booked.begin(), booked.end(), [&](const Reservation &r) { return r.id == id; });
        if (it != booked.end()) { booked.erase(it); promote(); return true; }
        auto w = find_if(waitlist.begin(), waitlist.end(), [&](const Reservation &r) { return r.id == id; });
        if (w != waitlist.end()) { waitlist.erase(w); return true; }
        return false;
    }
    const Reservation *findRes(int id) const {
        for (auto &r : booked) if (r.id == id) return &r;
        return nullptr;
    }

    void list() const {
        if (booked.empty() && waitlist.empty()) { cout << "  (nothing booked)\n"; return; }
        vector<Reservation> v = booked;
        sort(v.begin(), v.end(), [](auto &a, auto &b) { return a.start < b.start; });
        for (auto &r : v)
            cout << "  #" << setw(2) << r.id << "  " << fmtTime(r.start) << "-" << fmtTime(r.start + r.dur) << "  table "
                 << left << setw(5) << tstr(r.tables) << right << " party " << setw(2) << r.party << "  " << r.name
                 << (r.vip ? "  [VIP]" : "") << "\n";
        for (auto &r : waitlist)
            cout << "  #" << setw(2) << r.id << "  " << fmtTime(r.start) << "      WAITLIST     party " << setw(2)
                 << r.party << "  " << r.name << (r.vip ? "  [VIP]" : "") << "\n";
    }

    // timeline: one row per table, 15-min columns, digit/letter = reservation id
    void timeline() const {
        if (booked.empty()) { cout << "  (nothing booked)\n"; return; }
        int lo = INT_MAX, hi = 0;
        for (auto &r : booked) { lo = min(lo, r.start); hi = max(hi, r.start + r.dur); }
        lo = lo / 30 * 30; hi = (hi + 29) / 30 * 30;
        int cols = (hi - lo) / 15;
        string hdr(cols, ' ');
        for (int c = 0; c < cols; ++c)
            if ((lo + c * 15) % 60 == 0) { string h = to_string(((lo + c * 15) / 60) % 24); for (size_t i = 0; i < h.size() && c + i < (size_t)cols; ++i) hdr[c + i] = h[i]; }
        cout << "  " << setw(12) << "" << hdr << "   (hour marks; 1 char = 15 min)\n";
        for (auto &tb : tables) {
            string row(cols, '.');
            for (auto &r : booked) {
                if (find(r.tables.begin(), r.tables.end(), tb.id) == r.tables.end()) continue;
                for (int c = 0; c < cols; ++c) {
                    int tm = lo + c * 15;
                    if (tm >= r.start && tm < r.start + r.dur) row[c] = SYM[r.id % 36];
                }
            }
            cout << "  T" << left << setw(2) << tb.id << " (" << setw(2) << tb.cap << ")  " << right << "  " << row << "\n";
        }
    }
};

// ===================================================================
//                              KITCHEN
// ===================================================================
struct Task { int order, item, st, prep, placed; double w; };
struct Entry { int task, cook, s, f; };
struct Plan {
    vector<Entry> entries; vector<int> ready; vector<double> util;
    double cost = 0, avgWait = 0, maxWait = 0, avgHold = 0, vipWait = -1;
    int makespan = 0;
};

class Kitchen {
public:
    vector<MenuItem> menu;
    vector<pair<string, int>> stations;
    vector<Order> orders;
    int nextOrder = 1;

    int stationIdx(const string &n) const {
        for (size_t i = 0; i < stations.size(); ++i) if (stations[i].first == n) return (int)i;
        return -1;
    }
    void addStation(const string &n, int cooks) { stations.push_back({n, cooks}); }
    void addItem(const string &n, int prep, const string &st) {
        if (stationIdx(st) < 0) addStation(st, 1);
        menu.push_back({n, prep, st});
    }
    int addOrder(int resId, int placed, bool vip, const vector<int> &items) {
        orders.push_back({nextOrder++, resId, placed, vip, items});
        return orders.back().id;
    }
    void listMenu() const {
        for (size_t i = 0; i < menu.size(); ++i)
            cout << "  " << setw(2) << i + 1 << ". " << left << setw(18) << menu[i].name << right << setw(3)
                 << menu[i].prep << " min  [" << menu[i].station << "]\n";
    }

    vector<Task> tasks() const {
        vector<Task> T;
        for (size_t o = 0; o < orders.size(); ++o)
            for (int it : orders[o].items) {
                const MenuItem &m = menu[it];
                T.push_back({(int)o, it, stationIdx(m.station), m.prep, orders[o].placed, orders[o].vip ? 3.0 : 1.0});
            }
        return T;
    }

    // dispatch tasks in 'perm' order, each to the earliest-free cook of its station
    Plan evaluate(const vector<Task> &T, const vector<int> &perm, bool detail) const {
        Plan p;
        p.ready.assign(orders.size(), 0);
        vector<vector<int>> freeAt;
        for (auto &s : stations) freeAt.emplace_back(s.second, 0);
        vector<int> busy(stations.size(), 0), fin(T.size(), 0);
        for (int ti : perm) {
            const Task &t = T[ti];
            auto &c = freeAt[t.st];
            int k = (int)(min_element(c.begin(), c.end()) - c.begin());
            int s = max(t.placed, c[k]), f = s + t.prep;
            c[k] = f; busy[t.st] += t.prep; fin[ti] = f;
            p.ready[t.order] = max(p.ready[t.order], f);
            if (detail) p.entries.push_back({ti, k, s, f});
        }
        double waitSum = 0, vipSum = 0, holdSum = 0;
        int vipN = 0, minP = INT_MAX, end = 0;
        for (size_t o = 0; o < orders.size(); ++o) {
            double w = orders[o].vip ? 3.0 : 1.0, wait = p.ready[o] - orders[o].placed;
            p.cost += w * (wait + 0.08 * wait * wait); waitSum += wait; p.maxWait = max(p.maxWait, wait);
            if (orders[o].vip) { vipSum += wait; ++vipN; }
            minP = min(minP, orders[o].placed); end = max(end, p.ready[o]);
        }
        for (size_t i = 0; i < T.size(); ++i) {
            double h = p.ready[T[i].order] - fin[i];
            holdSum += h; p.cost += 0.5 * T[i].w * h;
        }
        if (!orders.empty()) p.avgWait = waitSum / orders.size();
        if (!T.empty()) p.avgHold = holdSum / T.size();
        if (vipN) p.vipWait = vipSum / vipN;
        p.makespan = end - (minP == INT_MAX ? 0 : minP);
        for (size_t s = 0; s < stations.size(); ++s)
            p.util.push_back(p.makespan > 0 ? 100.0 * busy[s] / (stations[s].second * p.makespan) : 0);
        return p;
    }

    vector<int> makePerm(const vector<Task> &T, int mode) const {   // 0 = FIFO, 1 = greedy
        vector<int> p(T.size());
        iota(p.begin(), p.end(), 0);
        if (mode == 0)
            stable_sort(p.begin(), p.end(), [&](int a, int b) { return T[a].placed < T[b].placed; });
        else
            stable_sort(p.begin(), p.end(), [&](int a, int b) {
                if (T[a].placed != T[b].placed) return T[a].placed < T[b].placed;
                if (T[a].order != T[b].order) return T[a].order < T[b].order;
                return T[a].prep > T[b].prep;
            });
        return p;
    }

    // simulated annealing over the dispatch order (never worse than the start)
    vector<int> anneal(const vector<Task> &T, vector<int> cur, int iters, unsigned seed) const {
        if (cur.size() < 2) return cur;
        mt19937 rng(seed);
        uniform_real_distribution<double> U(0, 1);
        double cc = evaluate(T, cur, false).cost, bc = cc;
        vector<int> best = cur;
        const double T0 = 15.0, T1 = 0.02;
        for (int i = 0; i < iters; ++i) {
            double temp = T0 * pow(T1 / T0, (double)i / iters);
            int a = rng() % cur.size(), b = rng() % cur.size();
            if (a == b) continue;
            vector<int> nx = cur;
            if (rng() & 1) swap(nx[a], nx[b]);
            else { int x = nx[a]; nx.erase(nx.begin() + a); nx.insert(nx.begin() + b, x); }
            double nc = evaluate(T, nx, false).cost, d = nc - cc;
            if (d <= 0 || U(rng) < exp(-d / temp)) {
                cur.swap(nx); cc = nc;
                if (cc < bc) { bc = cc; best = cur; }
            }
        }
        return best;
    }

    static string chg(double base, double now) {
        if (base <= 0) return "n/a";
        double d = 100.0 * (now - base) / base;
        char b[24]; snprintf(b, sizeof b, "%s%.1f%%", d <= 0 ? "-" : "+", fabs(d));
        return b;
    }
    static char letter(int o) { return o < 26 ? 'A' + o : 'a' + (o - 26) % 26; }

    void gantt(const Plan &p, const vector<Task> &T) const {
        if (p.entries.empty()) return;
        int t0 = INT_MAX, t1 = 0;
        for (auto &e : p.entries) { t0 = min(t0, e.s); t1 = max(t1, e.f); }
        int step = max(1, (t1 - t0 + 69) / 70), cols = (t1 - t0 + step - 1) / step;
        cout << "  Gantt (1 char = " << step << " min, letters = orders, '.' = idle)\n";
        string hdr(cols, ' ');
        for (int c = 0; c < cols; c += 10) {
            string lab = fmtTime(t0 + c * step);
            for (size_t i = 0; i < lab.size() && c + i < (size_t)cols; ++i) hdr[c + i] = lab[i];
        }
        cout << "  " << setw(12) << "" << hdr << "\n";
        for (size_t s = 0; s < stations.size(); ++s)
            for (int k = 0; k < stations[s].second; ++k) {
                string row(cols, '.');
                for (auto &e : p.entries) {
                    if (T[e.task].st != (int)s || e.cook != k) continue;
                    for (int c = (e.s - t0) / step; c <= (e.f - 1 - t0) / step && c < cols; ++c)
                        row[c] = letter(T[e.task].order);
                }
                cout << "  " << left << setw(12) << (stations[s].first + " #" + to_string(k + 1)) << right << row << "\n";
            }
        cout << "  Legend: ";
        for (size_t o = 0; o < orders.size(); ++o)
            cout << letter((int)o) << "=#" << orders[o].id << (orders[o].vip ? "*" : "") << " ";
        cout << "  (* = VIP)\n";
    }

    void tickets(const Plan &p, const vector<Task> &T) const {
        for (size_t o = 0; o < orders.size(); ++o) {
            cout << "  Order #" << orders[o].id << (orders[o].resId ? " (res #" + to_string(orders[o].resId) + ")" : " (walk-in)")
                 << (orders[o].vip ? " [VIP]" : "") << " placed " << fmtTime(orders[o].placed) << " -> READY "
                 << fmtTime(p.ready[o]) << "  (wait " << p.ready[o] - orders[o].placed << " min)\n";
            for (auto &e : p.entries)
                if (T[e.task].order == (int)o)
                    cout << "      " << left << setw(16) << menu[T[e.task].item].name << right << setw(8)
                         << stations[T[e.task].st].first << " #" << e.cook + 1 << "  " << fmtTime(e.s) << "-"
                         << fmtTime(e.f) << "\n";
        }
    }

    void report(int iters = 20000) const {
        if (orders.empty()) { cout << "  No orders placed yet.\n"; return; }
        auto T = tasks();
        Plan fifo = evaluate(T, makePerm(T, 0), true);
        Plan greedy = evaluate(T, makePerm(T, 1), true);
        Plan sa = evaluate(T, anneal(T, makePerm(T, 1), iters, 12345), true);
        cout << "\n=== STRATEGY COMPARISON (" << orders.size() << " orders, " << T.size() << " dishes) ===\n";
        cout << "  " << left << setw(10) << "Strategy" << right << setw(9) << "AvgWait" << setw(9) << "MaxWait" << setw(9)
             << "AvgHold" << setw(9) << "VIPWait" << setw(9) << "Window" << setw(10) << "Cost" << "\n" << fixed << setprecision(1);
        auto row = [&](const char *n, const Plan &p) {
            cout << "  " << left << setw(10) << n << right << setw(9) << p.avgWait << setw(9) << p.maxWait << setw(9)
                 << p.avgHold;
            if (p.vipWait < 0) cout << setw(9) << "-"; else cout << setw(9) << p.vipWait;
            cout << setw(9) << p.makespan << setw(10) << p.cost << "\n";
        };
        row("FIFO", fifo); row("Greedy", greedy); row("Anneal", sa);
        cout << "  Anneal vs FIFO: wait " << chg(fifo.avgWait, sa.avgWait) << ", max wait " << chg(fifo.maxWait, sa.maxWait)
             << ", hold " << chg(fifo.avgHold, sa.avgHold) << ", weighted cost " << chg(fifo.cost, sa.cost) << "\n";
        cout << "  (hold = minutes finished food waits for the rest of its order; VIP orders count 3x)\n";
        cout << "\n=== OPTIMIZED TICKETS ===\n";
        tickets(sa, T);
        cout << "\n=== OPTIMIZED GANTT ===\n";
        gantt(sa, T);
        cout << "\n  Station utilization: ";
        for (size_t s = 0; s < stations.size(); ++s) cout << stations[s].first << " " << sa.util[s] << "%  ";
        cout << "\n";
    }
};

// ===================================================================
//                   persistence (plain text, '|' separated)
// ===================================================================
static string clean(string s) { replace(s.begin(), s.end(), '|', ' '); return s; }

static bool saveState(const Floor &f, const Kitchen &k, const string &path) {
    ofstream o(path);
    if (!o) return false;
    for (auto &t : f.tables) o << "T|" << t.id << "|" << t.cap << "|" << t.zone << "\n";
    for (auto &s : k.stations) o << "S|" << clean(s.first) << "|" << s.second << "\n";
    for (auto &m : k.menu) o << "M|" << clean(m.name) << "|" << m.prep << "|" << clean(m.station) << "\n";
    auto wr = [&](const Reservation &r, char st) {
        o << "R|" << r.id << "|" << clean(r.name) << "|" << r.party << "|" << r.start << "|" << r.dur << "|" << r.vip
          << "|" << st << "|" << join(r.tables, ',') << "\n";
    };
    for (auto &r : f.booked) wr(r, 'B');
    for (auto &r : f.waitlist) wr(r, 'W');
    for (auto &d : k.orders)
        o << "O|" << d.id << "|" << d.resId << "|" << d.placed << "|" << d.vip << "|" << join(d.items, ',') << "\n";
    return true;
}

static bool loadState(Floor &f, Kitchen &k, const string &path) {
    ifstream in(path);
    if (!in) return false;
    Floor nf; Kitchen nk;
    nf.verbose = f.verbose; nf.combine = f.combine;
    string line;
    try {
        while (getline(in, line)) {
            auto p = split(line, '|');
            if (p.empty()) continue;
            if (p[0] == "T" && p.size() >= 4) nf.tables.push_back({stoi(p[1]), stoi(p[2]), stoi(p[3])});
            else if (p[0] == "S" && p.size() >= 3) nk.addStation(p[1], stoi(p[2]));
            else if (p[0] == "M" && p.size() >= 4) nk.addItem(p[1], stoi(p[2]), p[3]);
            else if (p[0] == "R" && p.size() >= 8) {
                Reservation r{stoi(p[1]), p[2], stoi(p[3]), stoi(p[4]), stoi(p[5]), p[6] == "1", {}};
                if (p.size() >= 9) for (auto &x : split(p[8], ',')) if (!x.empty()) r.tables.push_back(stoi(x));
                (p[7] == "W" ? nf.waitlist : nf.booked).push_back(r);
                nf.nextId = max(nf.nextId, r.id + 1);
            } else if (p[0] == "O" && p.size() >= 6) {
                Order d{stoi(p[1]), stoi(p[2]), stoi(p[3]), p[4] == "1", {}};
                for (auto &x : split(p[5], ',')) if (!x.empty()) d.items.push_back(stoi(x));
                nk.orders.push_back(d);
                nk.nextOrder = max(nk.nextOrder, d.id + 1);
            }
        }
    } catch (...) { return false; }
    f = nf; k = nk;
    return true;
}

// ===================================================================
//                         setup data + demo
// ===================================================================
static void setupFloor(Floor &f) {
    f.tables.clear();
    // id, seats, zone  (tables in the same zone are adjacent and can be combined)
    for (auto t : vector<Table>{{1, 2, 1}, {2, 2, 1}, {3, 4, 2}, {4, 4, 2}, {5, 6, 3}, {6, 6, 3}, {7, 8, 4}})
        f.tables.push_back(t);
}
static void setupKitchen(Kitchen &k) {
    k.stations.clear(); k.menu.clear();
    k.addStation("Tandoor", 1); k.addStation("Grill", 2); k.addStation("Fry", 1);
    k.addStation("Salad", 1); k.addStation("Curry", 2); k.addStation("Dessert", 1);
    k.addItem("Paneer Tikka", 12, "Tandoor");   // 0
    k.addItem("Butter Naan", 6, "Tandoor");     // 1
    k.addItem("Grilled Chicken", 18, "Grill");  // 2
    k.addItem("Veg Burger", 10, "Grill");       // 3
    k.addItem("French Fries", 6, "Fry");        // 4
    k.addItem("Spring Rolls", 8, "Fry");        // 5
    k.addItem("Caesar Salad", 5, "Salad");      // 6
    k.addItem("Dal Makhani", 15, "Curry");      // 7
    k.addItem("Butter Chicken", 20, "Curry");   // 8
    k.addItem("Gulab Jamun", 4, "Dessert");     // 9
}

static void loadDemo(Floor &f, Kitchen &k, bool quiet = false) {
    f = Floor(); k = Kitchen();
    f.verbose = !quiet;
    setupFloor(f); setupKitchen(k);
    int a = f.reserve("Sharma", 2, parseTime("19:00"));
    int b = f.reserve("Verma", 4, parseTime("19:00"));
    int c = f.reserve("Khan", 6, parseTime("19:15"), true);
    int d = f.reserve("Gupta", 8, parseTime("19:30"));
    f.reserve("Singh", 8, parseTime("20:00"), true);      // no table -> VIP waitlist
    f.reserve("Mehta", 10, parseTime("21:00"));           // needs two combined tables
    k.addOrder(a, parseTime("19:05"), false, {0, 1, 2, 6});
    k.addOrder(b, parseTime("19:08"), false, {8, 7, 5, 4, 9});
    k.addOrder(0, parseTime("19:10"), false, {8, 8, 7});
    k.addOrder(0, parseTime("19:12"), false, {2, 2, 3, 4});
    k.addOrder(c, parseTime("19:20"), true, {2, 8, 0, 1, 4, 6});
    k.addOrder(0, parseTime("19:25"), false, {8, 7, 0, 1});
    k.addOrder(d, parseTime("19:35"), false, {7, 8, 3, 2, 9, 1, 4});
    k.addOrder(0, parseTime("19:40"), false, {5, 4, 6, 9});
    f.verbose = true;
    if (!quiet) cout << "  Demo data loaded: 7 tables, 6 stations, 10 menu items, 8 orders.\n";
}

// ===================================================================
//                              BENCHMARK
// ===================================================================
static void benchmark(int runs) {
    cout << "\n=== KITCHEN BENCHMARK: " << runs << " random dinner rushes (20 orders / 2.5 h, 15% VIP) ===\n";
    double sw[3] = {0}, sm[3] = {0}, sh[3] = {0}, sc[3] = {0};
    for (int r = 0; r < runs; ++r) {
        mt19937 rng(1000 + r);
        Kitchen k; setupKitchen(k);
        for (int o = 0; o < 20; ++o) {
            int n = 2 + rng() % 4;
            vector<int> it;
            for (int i = 0; i < n; ++i) it.push_back(rng() % k.menu.size());
            k.addOrder(0, 1140 + rng() % 150, rng() % 100 < 15, it);
        }
        auto T = k.tasks();
        auto p0 = k.makePerm(T, 0), p1 = k.makePerm(T, 1);
        Plan pl[3] = {k.evaluate(T, p0, false), k.evaluate(T, p1, false),
                      k.evaluate(T, k.anneal(T, p1, 8000, 7 + r), false)};
        for (int m = 0; m < 3; ++m) { sw[m] += pl[m].avgWait; sm[m] += pl[m].maxWait; sh[m] += pl[m].avgHold; sc[m] += pl[m].cost; }
    }
    const char *nm[3] = {"FIFO", "Greedy", "Anneal"};
    cout << fixed << setprecision(1) << "  " << left << setw(10) << "Strategy" << right << setw(10) << "AvgWait"
         << setw(10) << "MaxWait" << setw(10) << "AvgHold" << setw(12) << "WtdCost" << "\n";
    for (int m = 0; m < 3; ++m)
        cout << "  " << left << setw(10) << nm[m] << right << setw(10) << sw[m] / runs << setw(10) << sm[m] / runs
             << setw(10) << sh[m] / runs << setw(12) << sc[m] / runs << "\n";
    cout << "  Anneal vs FIFO  : wait " << Kitchen::chg(sw[0], sw[2]) << "  max wait " << Kitchen::chg(sm[0], sm[2])
         << "  hold " << Kitchen::chg(sh[0], sh[2]) << "  cost " << Kitchen::chg(sc[0], sc[2])
         << "\n  Anneal vs Greedy: wait " << Kitchen::chg(sw[1], sw[2]) << "  max wait " << Kitchen::chg(sm[1], sm[2])
         << "  hold " << Kitchen::chg(sh[1], sh[2]) << "  cost " << Kitchen::chg(sc[1], sc[2]) << "\n";

    cout << "\n=== RESERVATION BENCHMARK: " << runs * 4 << " random evenings (45 requests each) ===\n";
    int conf[2] = {0}, tot = 0, bigConf[2] = {0}, bigTot = 0;
    for (int r = 0; r < runs * 4; ++r) {
        mt19937 rng(5000 + r);
        vector<array<int, 3>> reqs;
        for (int i = 0; i < 45; ++i) {
            int x = rng() % 100, party = x < 35 ? 2 : x < 50 ? 3 : x < 75 ? 4 : x < 80 ? 5 : x < 90 ? 6 : x < 96 ? 8 : 10;
            reqs.push_back({party, 1020 + 15 * (int)(rng() % 17), (int)(rng() % 100 < 10)});
        }
        for (int m = 0; m < 2; ++m) {
            Floor f; setupFloor(f); f.combine = (m == 1); f.verbose = false;
            for (auto &q : reqs) {
                bool ok = false;
                f.reserve("g", q[0], q[1], q[2], &ok);
                if (ok) { ++conf[m]; if (q[0] >= 7) ++bigConf[m]; }
            }
        }
        for (auto &q : reqs) { ++tot; if (q[0] >= 7) ++bigTot; }
    }
    cout << fixed << setprecision(1) << "  Seated, no combining   : " << 100.0 * conf[0] / tot << "%   (parties of 7+: "
         << (bigTot ? 100.0 * bigConf[0] / bigTot : 0) << "%)\n  Seated, with combining : " << 100.0 * conf[1] / tot
         << "%   (parties of 7+: " << (bigTot ? 100.0 * bigConf[1] / bigTot : 0) << "%)\n";
}

// ===================================================================
//                              SELF-TESTS
// ===================================================================
static int fails = 0, checks = 0;
static void check(bool c, const string &m) {
    ++checks;
    cout << (c ? "  PASS  " : "  FAIL  ") << m << "\n";
    if (!c) ++fails;
}
static int selfTest() {
    cout << "=== SELF-TESTS ===\n";
    {   // best fit
        Floor f; setupFloor(f); f.verbose = false;
        f.reserve("a", 2, 1140);
        check(f.booked.back().tables == vector<int>{1}, "party of 2 gets smallest table (T1)");
        f.reserve("b", 2, 1140);
        check(f.booked.back().tables == vector<int>{2}, "second party of 2 gets T2");
        f.reserve("c", 2, 1140);
        check(f.booked.back().tables == vector<int>{3}, "third party of 2 falls to a 4-top");
        f.reserve("d", 2, 1140 + 90);
        check(f.booked.back().tables == vector<int>{1}, "back-to-back booking reuses T1 (no false overlap)");
    }
    {   // combining
        Floor f; setupFloor(f); f.verbose = false;
        f.reserve("big", 10, 1260);
        check(f.booked.size() == 1 && f.booked[0].tables.size() == 2, "party of 10 gets two combined tables");
        Floor g; setupFloor(g); g.verbose = false; g.combine = false;
        g.reserve("big", 10, 1260);
        check(g.waitlist.size() == 1, "party of 10 waitlisted when combining disabled");
    }
    {   // waitlist + VIP promotion
        Floor f; setupFloor(f); f.verbose = false; f.combine = false;
        int g = f.reserve("gupta", 8, 1170);
        f.reserve("reg", 8, 1200);
        f.reserve("vip", 8, 1200, true);
        check(f.waitlist.size() == 2, "two parties waitlisted behind T7");
        f.cancel(g);
        check(f.booked.size() == 1 && f.booked[0].vip, "cancellation promotes the VIP first");
    }
    {   // kitchen sanity
        Kitchen k; setupKitchen(k);
        k.addOrder(0, 1140, false, {0, 6});   // tandoor 12 + salad 5, different stations
        auto T = k.tasks();
        Plan p = k.evaluate(T, k.makePerm(T, 1), false);
        check(p.ready[0] == 1140 + 12, "parallel stations -> ready = longest dish");
        Kitchen k2; setupKitchen(k2);
        k2.addOrder(0, 1140, false, {0, 0});  // 1 tandoor cook, two 12-min dishes
        auto T2 = k2.tasks();
        check(k2.evaluate(T2, k2.makePerm(T2, 1), false).ready[0] == 1140 + 24, "single cook serializes same-station dishes");
    }
    {   // annealing never worse than its start
        Floor f; Kitchen k; loadDemo(f, k, true);
        auto T = k.tasks();
        auto g = k.makePerm(T, 1);
        double gc = k.evaluate(T, g, false).cost, sc = k.evaluate(T, k.anneal(T, g, 5000, 1), false).cost;
        check(sc <= gc + 1e-9, "annealed cost <= greedy cost");
    }
    {   // persistence round-trip
        Floor f, f2; Kitchen k, k2; loadDemo(f, k, true);
        saveState(f, k, "/tmp/rest_state_test.txt");
        bool ok = loadState(f2, k2, "/tmp/rest_state_test.txt");
        check(ok && f2.booked.size() == f.booked.size() && f2.waitlist.size() == f.waitlist.size() &&
                  k2.orders.size() == k.orders.size() && k2.menu.size() == k.menu.size(),
              "save/load round-trip preserves state");
    }
    cout << "\n  " << checks - fails << "/" << checks << " checks passed\n";
    return fails ? 1 : 0;
}

// ===================================================================
//                                 MAIN
// ===================================================================
static void runDemo() {
    Floor f; Kitchen k;
    cout << "\n--- 1. Reservations (best-fit, combining, VIP waitlist) ---\n";
    loadDemo(f, k);
    cout << "\n--- 2. Reservation list ---\n";
    f.list();
    cout << "\n--- 3. Table timeline ---\n";
    f.timeline();
    cout << "\n--- 4. Gupta cancels -> waitlist promotion ---\n";
    f.cancel(4);
    f.list();
    cout << "\n--- 5. Kitchen optimization ---";
    k.report();
}

int main(int argc, char **argv) {
    if (argc > 1) {
        string a = argv[1];
        if (a == "--test") return selfTest();
        if (a == "--demo") { runDemo(); return 0; }
        if (a == "--bench") { benchmark(argc > 2 ? max(1, atoi(argv[2])) : 30); return 0; }
        cout << "Usage: " << argv[0] << " [--demo | --bench N | --test]\n";
        return 1;
    }
    Floor f; Kitchen k;
    setupFloor(f); setupKitchen(k);
    const string SAVE = "restaurant_state.txt";
    while (true) {
        cout << "\n======== Restaurant Reservation & Kitchen Optimizer ========\n"
                "  1. Load demo data          8. Place order\n"
                "  2. Make reservation        9. Kitchen optimization report\n"
                "  3. Cancel reservation     10. Benchmark (random rushes)\n"
                "  4. List reservations      11. Save state\n"
                "  5. Table timeline         12. Load state\n"
                "  6. Floor status at time   13. Run self-tests\n"
                "  7. Show menu               0. Exit\n";
        int ch = readInt("Choice: ");
        switch (ch) {
        case 1: loadDemo(f, k); break;
        case 2: {
            string n = readLine("  Guest name: ");
            int party = readInt("  Party size: ");
            int s = readTime("  Time (HH:MM): ");
            string v = readLine("  VIP? (y/n): ");
            if (party <= 0) { cout << "  Invalid party size.\n"; break; }
            f.reserve(n, party, s, !v.empty() && (v[0] == 'y' || v[0] == 'Y'));
            break;
        }
        case 3: cout << (f.cancel(readInt("  Reservation id: ")) ? "  Cancelled.\n" : "  Not found.\n"); break;
        case 4: f.list(); break;
        case 5: f.timeline(); break;
        case 6: {
            int t = readTime("  Time (HH:MM): ");
            for (auto &tb : f.tables) {
                const Reservation *occ = nullptr;
                for (auto &r : f.booked)
                    if (find(r.tables.begin(), r.tables.end(), tb.id) != r.tables.end() && t >= r.start && t < r.start + r.dur)
                        occ = &r;
                cout << "  [T" << tb.id << " seats " << tb.cap << "] "
                     << (occ ? "BUSY  #" + to_string(occ->id) + " " + occ->name + " until " + fmtTime(occ->start + occ->dur) : "FREE") << "\n";
            }
            break;
        }
        case 7: k.listMenu(); break;
        case 8: {
            int rid = readInt("  Reservation id (0 = walk-in): ");
            const Reservation *r = f.findRes(rid);
            if (rid != 0 && !r) { cout << "  Not a confirmed reservation.\n"; break; }
            int placed = readTime("  Order time (HH:MM): ");
            k.listMenu();
            stringstream ss(readLine("  Item numbers (space separated): "));
            vector<int> items; int n;
            while (ss >> n) if (n >= 1 && n <= (int)k.menu.size()) items.push_back(n - 1);
            if (items.empty()) { cout << "  No valid items.\n"; break; }
            cout << "  Order #" << k.addOrder(rid, placed, r && r->vip, items) << " placed.\n";
            break;
        }
        case 9: k.report(); break;
        case 10: benchmark(readInt("  Number of runs (e.g. 30): ")); break;
        case 11: cout << (saveState(f, k, SAVE) ? "  Saved to " + SAVE + "\n" : "  Save failed.\n"); break;
        case 12: cout << (loadState(f, k, SAVE) ? "  Loaded " + SAVE + "\n" : "  Load failed (no/invalid file).\n"); break;
        case 13: selfTest(); break;
        case 0: return 0;
        default: cout << "  Invalid choice.\n";
        }
    }
}
