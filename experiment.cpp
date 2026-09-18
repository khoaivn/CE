#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// C++17. Small-instance verification of Algorithm 1 in the supplied paper.
// Exact exhaustive USM replaces BF-USM: stronger value, exponential cost.
// Floating-point arithmetic does NOT implement the paper's exact-real model.
using Mask = uint64_t;
using Real = long double;
struct Instance
{
    int n;
    std::vector<std::vector<Real>> w;
    std::vector<Real> mu, variance, value;
    Real B, z;
    uint64_t queries = 0;
    Real f(Mask s)
    {
        ++queries;
        return value.at(s);
    }
    Real score(Mask s) const
    {
        Real m = 0, v = 0;
        for (int e = 0; e < n; ++e)
            if (s & (Mask(1) << e))
            {
                m += mu[e];
                v += variance[e];
            }
        return m + z * std::sqrt(v);
    }
    bool feasible(Mask s) const { return score(s) <= B; }
};
Real quantile(Real alpha)
{
    Real lo = 0, hi = 12;
    for (int i = 0; i < 150; ++i)
    {
        Real mid = (lo + hi) / 2;
        if (std::erfc(mid / std::sqrt(Real(2))) / 2 > alpha)
            lo = mid;
        else
            hi = mid;
    }
    return (lo + hi) / 2;
}
Instance generate(int n, uint64_t seed, Real alpha, Real ratio, Real uncertainty)
{
    Instance p;
    p.n = n;
    p.z = quantile(alpha);
    p.w.assign(n, std::vector<Real>(n));
    p.mu.resize(n);
    p.variance.resize(n);
    std::mt19937_64 rng(seed);
    auto unit = [&]()
    { return Real(rng() >> 11) / Real(uint64_t(1) << 53); };
    for (int e = 0; e < n; ++e)
    {
        p.mu[e] = 1 + 9 * unit();
        p.variance[e] = std::pow(uncertainty * p.mu[e], 2);
    }
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            if (unit() < 0.3L)
                p.w[i][j] = p.w[j][i] = 1 + Real(rng() % 10);
    p.B = ratio * p.score((Mask(1) << n) - 1);
    // Precomputed exact integer-valued cut oracle. Not counted as algorithm memory.
    p.value.assign(size_t(1) << n, 0);
    for (Mask s = 1; s < (Mask(1) << n); ++s)
    {
        int e = __builtin_ctzll(s);
        Mask t = s & (s - 1);
        Real d = 0;
        for (int j = 0; j < n; ++j)
            d += ((t & (Mask(1) << j)) ? -1 : 1) * p.w[e][j];
        p.value[s] = p.value[t] + d;
    }
    return p;
}
void improve(Instance &p, Mask candidate, Mask &best, Real &bestValue)
{
    Real v = p.f(candidate);
    if (v > bestValue)
    {
        best = candidate;
        bestValue = v;
    }
}
Mask optimum(Instance &p)
{
    Mask best = 0;
    Real value = 0;
    for (Mask s = 1; s < (Mask(1) << p.n); ++s)
        if (p.feasible(s))
            improve(p, s, best, value);
    return best;
}
Mask greedy(Instance &p)
{
    Mask s = 0, best = 0;
    Real bestValue = 0;
    for (int e = 0; e < p.n; ++e)
        if (p.feasible(Mask(1) << e))
            improve(p, Mask(1) << e, best, bestValue);
    while (true)
    {
        int pick = -1;
        Real density = 0, base = p.f(s), cost = p.score(s);
        for (int e = 0; e < p.n; ++e)
            if (!(s & (Mask(1) << e)))
            {
                Mask t = s | (Mask(1) << e);
                if (!p.feasible(t))
                    continue;
                Real d = (p.f(t) - base) / (p.score(t) - cost);
                if (d > density)
                {
                    density = d;
                    pick = e;
                }
            }
        if (pick < 0)
            break;
        s |= Mask(1) << pick;
    }
    improve(p, s, best, bestValue);
    return best;
}
struct State
{
    Mask s1 = 0, s2 = 0;
    Real a1 = 0, a2 = 0;
    bool terminal = false;
};

struct Tangent
{
    Real theta = 0, capacity = 0, M = 0, D = 0;
    std::map<int, State> states;
};
struct Stats
{
    size_t peakStates = 0, peakSlots = 0, tangents = 0;
};
Mask focus(Instance &p, const std::vector<int> &order, Real eps, Stats &stats)
{
    Real delta = 16 * eps, logBase = std::log1p(delta), top = p.B / p.z;
    std::map<int, Tangent> tangents;
    Mask best = 0;
    Real bestValue = 0;
    auto update = [&](Mask s)
    {if(p.feasible(s))improve(p,s,best,bestValue); };
    for (int e : order)
    {
        Mask bit = Mask(1) << e;
        Real fe = p.f(bit);
        if (!p.feasible(bit))
            continue;
        if (fe > bestValue)
        {
            best = bit;
            bestValue = fe;
        }
        Real A = p.B - p.mu[e];
        Real disc = std::max(Real(0), A * A - p.z * p.z * p.variance[e]);
        // Stable smaller root; avoids cancellation in A-sqrt(disc).
        Real tminus = p.z * p.variance[e] / (A + std::sqrt(disc));
        Real tplus = (A + std::sqrt(disc)) / p.z;
        int first = std::max(0, int(std::floor(std::log(top / std::min(top, tplus)) / std::log(1.5L))) - 2);
        int last = int(std::ceil(std::log(top / tminus) / std::log(1.5L))) + 2;
        if (last - first > 100000)
            throw std::runtime_error("Tangent range too wide");
        for (int j = first; j <= last; ++j)
        {
            Real theta = top * std::pow(1.5L, -j), C = p.B - p.z * theta / 2;
            Real a = (p.mu[e] + p.z * p.variance[e] / (2 * theta)) / C;
            if (a > 1)
                continue;
            auto &t = tangents[j];
            t.theta = theta;
            t.capacity = C;
            t.M = std::max(t.M, fe);
            t.D = std::max(t.D, fe / a);
            if (t.M == 0)
                continue; // No positive objective guesses exist yet.
            Real lower = t.M / (1 + delta), upper = 4 * t.D;
            int kmin = int(std::floor(std::log(lower) / logBase)) - 1;
            int kmax = int(std::ceil(std::log(upper) / logBase)) + 1;
            if (kmax - kmin > 100000)
                throw std::runtime_error("Value range too wide");
            for (int k = kmin; k <= kmax; ++k)
            {
                Real guess = std::exp(k * logBase);
                if (guess >= lower && guess <= upper)
                    t.states.try_emplace(k);
            }
            for (auto it = t.states.begin(); it != t.states.end();)
            {
                if (std::exp(it->first * logBase) < lower)
                    it = t.states.erase(it);
                else
                    ++it;
            }
            for (auto &[k, s] : t.states)
            {
                if (s.terminal)
                    continue;
                Real threshold = std::exp(k * logBase) / 4;
                if (a >= 0.5L && fe / a >= threshold)
                {
                    update(bit);
                    s.terminal = true;
                    continue;
                }
                // Original feasibility guard protects against floating-point drift.
                if (s.a1 + a <= 1 && p.feasible(s.s1 | bit) && p.f(s.s1 | bit) - p.f(s.s1) >= threshold * a)
                {
                    s.s1 |= bit;
                    s.a1 += a;
                }
                else if (s.a2 + a <= 1 && p.feasible(s.s2 | bit) && p.f(s.s2 | bit) - p.f(s.s2) >= threshold * a)
                {
                    s.s2 |= bit;
                    s.a2 += a;
                }
            }
        }
        size_t states = 0, slots = 0;
        for (const auto &[j, t] : tangents)
            for (const auto &[k, s] : t.states)
            {
                ++states;
                slots += __builtin_popcountll(s.s1) + __builtin_popcountll(s.s2);
            }
        stats.peakStates = std::max(stats.peakStates, states);
        stats.peakSlots = std::max(stats.peakSlots, slots);
    }
    std::unordered_map<Mask, Mask> usmCache;
    for (auto &[j, t] : tangents)
        for (auto &[k, s] : t.states)
        {
            if (s.terminal)
                continue;
            update(s.s1);
            update(s.s2);
            auto it = usmCache.find(s.s1);
            if (it == usmCache.end())
            {
                Mask out = 0;
                Real value = 0;
                for (Mask sub = s.s1; sub; sub = (sub - 1) & s.s1)
                    improve(p, sub, out, value);
                it = usmCache.emplace(s.s1, out).first;
            }
            update(it->second);
        }
    stats.tangents = tangents.size();
    return best;
}
int main(int argc, char **argv)
{
    try
    {
        int n = argc > 1 ? std::stoi(argv[1]) : 16;
        uint64_t seed = argc > 2 ? std::stoull(argv[2]) : 42;
        Real eps = argc > 3 ? std::stold(argv[3]) : 0.02L;
        Real alpha = argc > 4 ? std::stold(argv[4]) : 0.05L;
        Real budget = argc > 5 ? std::stold(argv[5]) : 0.2L;
        Real uncertainty = argc > 6 ? std::stold(argv[6]) : 0.5L;
        std::string ordering = argc > 7 ? argv[7] : "random";
        if (n < 1 || n > 20 || !(eps > 0 && eps < 0.0625L) || !(alpha >= 1e-12L && alpha < 0.5L) || !(budget > 0 && budget <= 1) || !(uncertainty > 0 && uncertainty <= 100))
            throw std::runtime_error("Require n=1..20, 0<epsilon<1/16, 1e-12<=alpha<0.5, 0<budget_ratio<=1, 0<uncertainty<=100");
        if (ordering != "random" && ordering != "mu_asc" && ordering != "mu_desc")
            throw std::runtime_error("Order: random | mu_asc | mu_desc");
        auto p = generate(n, seed, alpha, budget, uncertainty);
        std::vector<int> order(n);
        std::iota(order.begin(), order.end(), 0);
        std::mt19937_64 rng(seed + 1);
        if (ordering == "random")
            std::shuffle(order.begin(), order.end(), rng);
        else
            std::stable_sort(order.begin(), order.end(), [&](int a, int b)
                             { return ordering == "mu_asc" ? p.mu[a] < p.mu[b] : p.mu[a] > p.mu[b]; });
        std::cout << std::setprecision(12);
        std::cout << "algorithm,n,seed,epsilon,alpha,budget_ratio,uncertainty,order,B,value,opt_ratio,chance_score,score_over_B,feasible,size,queries,ms,tangents,peak_states,peak_candidate_slots\n";
        Real optValue = 0;
        auto run = [&](const std::string &name, auto solve)
        {
            p.queries = 0;
            Stats stats;
            auto start = std::chrono::steady_clock::now();
            Mask s = solve(stats);
            auto finish = std::chrono::steady_clock::now();
            Real value = p.value[s];
            if (name == "OPT")
                optValue = value;
            std::cout << name << ',' << n << ',' << seed << ',' << eps << ',' << alpha << ',' << budget << ',' << uncertainty << ',' << ordering << ',' << p.B << ',' << value << ',';
            if (optValue > 0)
                std::cout << value / optValue;
            else
                std::cout << "NA";
            std::cout << ',' << p.score(s) << ',' << p.score(s) / p.B << ',' << p.feasible(s) << ',' << __builtin_popcountll(s) << ',' << p.queries << ','
                      << std::chrono::duration<double, std::milli>(finish - start).count() << ',' << stats.tangents << ',' << stats.peakStates << ',' << stats.peakSlots << '\n';
            if (!p.feasible(s))
                throw std::runtime_error("Infeasible output");
            if (name == "FOCUS_EXACT_USM" && value + 1e-10L < (0.0625L - eps) * optValue)
                throw std::runtime_error("Approximation check failed");
            std::cerr << name << " selected:";
            for (int e = 0; e < n; ++e)
                if (s & (Mask(1) << e))
                    std::cerr << ' ' << e;
            std::cerr << '\n';
        };
        run("OPT", [&](Stats &)
            { return optimum(p); });
        run("Offline_Greedy_CC", [&](Stats &)
            { return greedy(p); });
        run("FOCUS_EXACT_USM", [&](Stats &s)
            { return focus(p, order, eps, s); });
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
