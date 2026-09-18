#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

// Scalable C++17 experiment for the Facebook edge list. Influence is estimated
// with one fixed bank of reverse-reachable (RR) sets shared by both algorithms.
using Real = long double;
using Clock = std::chrono::steady_clock;

struct Config {
    std::string graph = "facebook";
    std::string order = "random";
    size_t rrSamples = 50000, evalSamples = 100000;
    Real edgeProbability = 0.01L, epsilon = 0.02L, alpha = 0.05L;
    Real budget = 100.0L, budgetRatio = -1.0L, uncertainty = 0.5L;
    uint64_t resourceSeed = 42, rrSeed = 43, evalSeed = 44, orderSeed = 45;
    bool printSeeds = false;
};

Real gaussianQuantile(Real alpha) {
    auto upperTail = [](Real z) { return std::erfc(z/std::sqrt(Real(2)))/2; };
    Real lo = 0, hi = 1;
    while (upperTail(hi) > alpha) {
        hi *= 2;
        if (!std::isfinite(hi) || hi > 256)
            throw std::runtime_error("alpha is too small for the available floating-point range");
    }
    for (int i = 0; i < 200; ++i) {
        Real mid = (lo + hi) / 2;
        if (upperTail(mid) > alpha) lo = mid;
        else hi = mid;
    }
    return (lo + hi) / 2;
}

struct Graph {
    int n = 0;
    size_t arcs = 0;
    std::vector<std::vector<int>> reverse;
};

Graph loadGraph(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Cannot open graph: " + path);
    long long nRaw = 0, mRaw = 0;
    if (!(in >> nRaw >> mRaw) || nRaw <= 0 || mRaw < 0 ||
        nRaw > std::numeric_limits<int>::max())
        throw std::runtime_error("Invalid graph header; expected: number_of_nodes number_of_arcs");
    Graph g; g.n = static_cast<int>(nRaw); g.arcs = static_cast<size_t>(mRaw);
    std::vector<std::pair<int,int>> edges;
    edges.reserve(g.arcs);
    std::vector<size_t> indegree(static_cast<size_t>(g.n), 0);
    std::unordered_set<uint64_t> seen;
    seen.reserve(g.arcs * 2 + 1);
    for (size_t i = 0; i < g.arcs; ++i) {
        long long uRaw = -1, vRaw = -1;
        if (!(in >> uRaw >> vRaw))
            throw std::runtime_error("Graph ended before all declared arcs were read");
        if (uRaw < 0 || vRaw < 0 || uRaw >= g.n || vRaw >= g.n || uRaw == vRaw)
            throw std::runtime_error("Invalid endpoint or self-loop at arc " + std::to_string(i));
        int u = static_cast<int>(uRaw), v = static_cast<int>(vRaw);
        uint64_t key = (uint64_t(static_cast<uint32_t>(u)) << 32) |
                       uint32_t(v);
        if (!seen.insert(key).second)
            throw std::runtime_error("Duplicate directed arc: " + std::to_string(u) + " " + std::to_string(v));
        edges.emplace_back(u, v);
        ++indegree[static_cast<size_t>(v)];
    }
    std::string extra;
    if (in >> extra) throw std::runtime_error("Extra data after the declared arc count");
    g.reverse.resize(static_cast<size_t>(g.n));
    for (int v = 0; v < g.n; ++v) g.reverse[static_cast<size_t>(v)].reserve(indegree[static_cast<size_t>(v)]);
    for (auto [u,v] : edges) g.reverse[static_cast<size_t>(v)].push_back(u);
    return g;
}

struct RRBank {
    size_t samples = 0, words = 0, memberships = 0, maxSetSize = 0;
    std::vector<std::vector<uint32_t>> incidence; // vertex -> RR-set IDs
};

RRBank makeRRBank(const Graph& g, size_t samples, Real probability, uint64_t seed) {
    RRBank bank;
    bank.samples = samples; bank.words = (samples + 63) / 64;
    bank.incidence.resize(static_cast<size_t>(g.n));
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> target(0, g.n - 1);
    std::bernoulli_distribution live(static_cast<double>(probability));
    std::vector<uint32_t> seen(static_cast<size_t>(g.n), 0);
    std::vector<int> queue; queue.reserve(static_cast<size_t>(g.n));
    for (size_t r = 0; r < samples; ++r) {
        uint32_t stamp = static_cast<uint32_t>(r + 1);
        if (stamp == 0) throw std::runtime_error("Too many RR samples for 32-bit stamps");
        queue.clear();
        int root = target(rng); seen[static_cast<size_t>(root)] = stamp; queue.push_back(root);
        for (size_t head = 0; head < queue.size(); ++head) {
            int v = queue[head];
            for (int u : g.reverse[static_cast<size_t>(v)]) {
                if (seen[static_cast<size_t>(u)] != stamp && live(rng)) {
                    seen[static_cast<size_t>(u)] = stamp;
                    queue.push_back(u);
                }
            }
        }
        bank.maxSetSize = std::max(bank.maxSetSize, queue.size());
        bank.memberships += queue.size();
        for (int v : queue) bank.incidence[static_cast<size_t>(v)].push_back(static_cast<uint32_t>(r));
    }
    return bank;
}

struct Resources {
    std::vector<Real> mu, variance;
    Real sumMu = 0;
};

Resources makeResources(int n, Real uncertainty, uint64_t seed) {
    Resources r; r.mu.resize(static_cast<size_t>(n)); r.variance.resize(static_cast<size_t>(n));
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> mean(0.0, 1.0);
    for (int e = 0; e < n; ++e) {
        Real mu = 0;
        while (mu == 0) mu = mean(rng); // Keep sigma^2 strictly positive.
        Real sigma = uncertainty * mu;
        if (!std::isfinite(mu) || !std::isfinite(sigma) || sigma <= 0 ||
            !std::isfinite(sigma*sigma) || sigma*sigma <= 0)
            throw std::runtime_error("Resource scale produced a zero or non-finite variance");
        r.mu[static_cast<size_t>(e)] = mu;
        r.variance[static_cast<size_t>(e)] = sigma * sigma;
        r.sumMu += mu;
    }
    return r;
}

struct Candidate {
    std::vector<int> items;
    std::vector<uint64_t> covered;
    uint32_t hits = 0;
    Real mu = 0, variance = 0;
    explicit Candidate(size_t words = 0) : covered(words, 0) {}
};

class RROracle {
public:
    const RRBank& bank;
    int nodes;
    uint64_t queries = 0;
    RROracle(const RRBank& b, int n) : bank(b), nodes(n) {}
    Real spread(uint32_t hits) {
        ++queries;
        return Real(nodes) * hits / bank.samples;
    }
    uint32_t singletonHits(int e) {
        ++queries;
        return static_cast<uint32_t>(bank.incidence[static_cast<size_t>(e)].size());
    }
    uint32_t marginalHits(const Candidate& set, int e) {
        ++queries;
        uint32_t result = 0;
        for (uint32_t r : bank.incidence[static_cast<size_t>(e)])
            if (!(set.covered[r >> 6] & (uint64_t(1) << (r & 63)))) ++result;
        return result;
    }
    Real toSpread(uint32_t hits) const { return Real(nodes) * hits / bank.samples; }
};

void addItem(Candidate& set, int e, uint32_t marginal, const RRBank& bank,
             const Resources& resources) {
    for (uint32_t r : bank.incidence[static_cast<size_t>(e)])
        set.covered[r >> 6] |= uint64_t(1) << (r & 63);
    set.hits += marginal;
    set.items.push_back(e);
    set.mu += resources.mu[static_cast<size_t>(e)];
    set.variance += resources.variance[static_cast<size_t>(e)];
}

Real chanceScore(Real mu, Real variance, Real z) { return mu + z * std::sqrt(variance); }
bool feasible(Real mu, Real variance, Real z, Real budget) {
    Real score = chanceScore(mu, variance, z);
    Real tolerance = 64 * std::numeric_limits<Real>::epsilon() * std::max(Real(1), budget);
    return score <= budget + tolerance;
}

uint32_t evaluate(const RRBank& bank, const std::vector<int>& items) {
    std::vector<uint64_t> covered(bank.words, 0);
    for (int e : items)
        for (uint32_t r : bank.incidence[static_cast<size_t>(e)])
            covered[r >> 6] |= uint64_t(1) << (r & 63);
    uint64_t hits = 0;
    for (uint64_t word : covered) hits += static_cast<uint64_t>(__builtin_popcountll(word));
    return static_cast<uint32_t>(hits);
}

struct RunResult {
    std::vector<int> items;
    uint32_t trainHits = 0, evalHits = 0;
    Real mu = 0, variance = 0;
    uint64_t queries = 0;
    double milliseconds = 0;
    size_t tangents = 0, peakStates = 0, peakSlots = 0;
    size_t algorithmMemoryBytes = 0;
};

size_t candidateDynamicBytes(const Candidate& candidate) {
    return candidate.items.capacity()*sizeof(int) +
           candidate.covered.capacity()*sizeof(uint64_t);
}

size_t graphStorageBytes(const Graph& graph) {
    size_t bytes = sizeof(Graph) + graph.reverse.capacity()*sizeof(std::vector<int>);
    for (const auto& adjacency : graph.reverse) bytes += adjacency.capacity()*sizeof(int);
    return bytes;
}

size_t rrStorageBytes(const RRBank& bank) {
    size_t bytes = sizeof(RRBank) +
                   bank.incidence.capacity()*sizeof(std::vector<uint32_t>);
    for (const auto& list : bank.incidence) bytes += list.capacity()*sizeof(uint32_t);
    return bytes;
}

size_t resourcesStorageBytes(const Resources& resources) {
    return sizeof(Resources) + resources.mu.capacity()*sizeof(Real) +
           resources.variance.capacity()*sizeof(Real);
}

void consider(RROracle& oracle, const Candidate& set, Candidate& best) {
    (void)oracle.spread(set.hits);
    if (set.hits > best.hits) best = set;
}

RunResult offlineGreedy(const Graph& g, const RRBank& train, const RRBank& evaluation,
                        const Resources& resources, Real z, Real budget) {
    RROracle oracle(train, g.n);
    auto started = Clock::now();
    Candidate best(train.words), current(train.words);
    std::vector<char> selected(static_cast<size_t>(g.n), false);
    for (int e = 0; e < g.n; ++e) {
        uint32_t hits = oracle.singletonHits(e);
        Real mu = resources.mu[static_cast<size_t>(e)], var = resources.variance[static_cast<size_t>(e)];
        if (feasible(mu, var, z, budget) && hits > best.hits) {
            best = Candidate(train.words); addItem(best, e, hits, train, resources);
        }
    }
    while (true) {
        int chosen = -1; uint32_t chosenMarginal = 0; Real bestDensity = 0;
        for (int e = 0; e < g.n; ++e) if (!selected[static_cast<size_t>(e)]) {
            Real nextMu = current.mu + resources.mu[static_cast<size_t>(e)];
            Real nextVar = current.variance + resources.variance[static_cast<size_t>(e)];
            if (!feasible(nextMu, nextVar, z, budget)) continue;
            uint32_t marginal = oracle.marginalHits(current, e);
            Real cost = chanceScore(nextMu, nextVar, z) - chanceScore(current.mu, current.variance, z);
            Real density = oracle.toSpread(marginal) / cost;
            if (density > bestDensity) { bestDensity = density; chosen = e; chosenMarginal = marginal; }
        }
        if (chosen < 0 || chosenMarginal == 0) break;
        selected[static_cast<size_t>(chosen)] = true;
        addItem(current, chosen, chosenMarginal, train, resources);
    }
    consider(oracle, current, best);
    auto finished = Clock::now();
    RunResult result;
    result.items = best.items; result.trainHits = best.hits;
    result.evalHits = evaluate(evaluation, result.items);
    result.mu = best.mu; result.variance = best.variance; result.queries = oracle.queries;
    result.milliseconds = std::chrono::duration<double,std::milli>(finished-started).count();
    result.algorithmMemoryBytes = sizeof(best)+sizeof(current) +
        candidateDynamicBytes(best)+candidateDynamicBytes(current) +
        selected.capacity()*sizeof(char) + evaluation.words*sizeof(uint64_t);
    return result;
}

struct State {
    Candidate first, second;
    Real firstCost = 0, secondCost = 0;
    explicit State(size_t words = 0) : first(words), second(words) {}
};
struct Tangent {
    Real theta = 0, capacity = 0, maxSingleton = 0, maxDensity = 0;
    std::map<int,State> active;
    std::set<int> retired;
};

size_t tangentStorageBytes(const Tangent& tangent) {
    size_t bytes = sizeof(std::pair<const int,Tangent>)+3*sizeof(void*) +
        tangent.retired.size()*(sizeof(int)+3*sizeof(void*));
    for (const auto& [k,state] : tangent.active) {
        (void)k;
        bytes += sizeof(std::pair<const int,State>)+3*sizeof(void*) +
            candidateDynamicBytes(state.first)+candidateDynamicBytes(state.second);
    }
    return bytes;
}

size_t focusStorageBytes(const Candidate& best, const std::map<int,Tangent>& tangents) {
    size_t bytes = sizeof(tangents)+sizeof(best)+candidateDynamicBytes(best);
    for (const auto& [j,tangent] : tangents) {
        (void)j; bytes += tangentStorageBytes(tangent);
    }
    return bytes;
}

void replaceStorageBytes(size_t& total, size_t oldBytes, size_t newBytes) {
    if (newBytes >= oldBytes) total += newBytes-oldBytes;
    else total -= oldBytes-newBytes;
}

std::pair<int,int> checkedIndexRange(Real first, Real last, const char* message) {
    if (!std::isfinite(first) || !std::isfinite(last) || last < first ||
        first < Real(std::numeric_limits<int>::min()) ||
        last >= Real(std::numeric_limits<int>::max()) || last-first > 100000)
        throw std::runtime_error(message);
    return {static_cast<int>(first), static_cast<int>(last)};
}

RunResult focus(const Graph& g, const RRBank& train, const RRBank& evaluation,
                const Resources& resources, const std::vector<int>& order,
                Real z, Real budget, Real epsilon) {
    RROracle oracle(train, g.n);
    auto started = Clock::now();
    const Real delta = 16 * epsilon, logValueBase = std::log1p(delta);
    const Real tangentBase = std::log(1.5L), top = budget / z;
    std::map<int,Tangent> tangents;
    Candidate best(train.words);
    size_t peakStates = 0, peakSlots = 0, peakAlgorithmBytes = 0;
    size_t currentAlgorithmBytes = focusStorageBytes(best,tangents);
    for (int e : order) {
        uint32_t singletonHits = oracle.singletonHits(e);
        Real singletonValue = oracle.toSpread(singletonHits);
        Real mu = resources.mu[static_cast<size_t>(e)], var = resources.variance[static_cast<size_t>(e)];
        if (!feasible(mu, var, z, budget)) continue;
        if (singletonHits > best.hits) {
            best = Candidate(train.words); addItem(best, e, singletonHits, train, resources);
        }
        currentAlgorithmBytes = focusStorageBytes(best,tangents);
        peakAlgorithmBytes = std::max(peakAlgorithmBytes,currentAlgorithmBytes);
        Real A = budget - mu;
        Real discriminant = std::max(Real(0), A*A-z*z*var);
        Real root = std::sqrt(discriminant);
        Real tMinus = z*var/(A+root), tPlus = (A+root)/z;
        if (!(tMinus > 0 && tPlus > 0 && std::isfinite(tMinus) && std::isfinite(tPlus)))
            throw std::runtime_error("Invalid tangent interval");
        auto [firstJ,lastJ] = checkedIndexRange(
            std::max(Real(0), std::floor((std::log(top)-std::log(std::min(top,tPlus)))/tangentBase)-2),
            std::ceil((std::log(top)-std::log(tMinus))/tangentBase)+2,
            "Tangent range is too wide");
        for (int j = firstJ; j <= lastJ; ++j) {
            Real theta = top * std::pow(1.5L, -j);
            Real capacity = budget - z*theta/2;
            Real normalized = (mu + z*var/(2*theta))/capacity;
            if (!std::isfinite(normalized) || normalized > 1) continue;
            auto existing = tangents.find(j);
            size_t oldTangentBytes = existing == tangents.end() ? 0 : tangentStorageBytes(existing->second);
            auto [where,inserted] = tangents.try_emplace(j);
            Tangent& tangent = where->second;
            if (inserted) { tangent.theta = theta; tangent.capacity = capacity; }
            tangent.maxSingleton = std::max(tangent.maxSingleton, singletonValue);
            tangent.maxDensity = std::max(tangent.maxDensity, singletonValue/normalized);
            if (tangent.maxSingleton == 0) continue;
            Real lower = tangent.maxSingleton/(1+delta), upper = 4*tangent.maxDensity;
            auto [firstK,lastK] = checkedIndexRange(
                std::floor(std::log(lower)/logValueBase)-1,
                std::ceil(std::log(upper)/logValueBase)+1,
                "Value range is too wide; increase epsilon");
            for (int k = firstK; k <= lastK; ++k) {
                Real guess = std::exp(k*logValueBase);
                if (guess >= lower && guess <= upper && !tangent.retired.count(k))
                    tangent.active.try_emplace(k, train.words);
            }
            for (auto it = tangent.active.begin(); it != tangent.active.end();) {
                Real guess = std::exp(it->first*logValueBase);
                if (guess < lower) it = tangent.active.erase(it); else ++it;
            }
            for (auto it = tangent.retired.begin(); it != tangent.retired.end();) {
                if (std::exp(*it*logValueBase) < lower) it = tangent.retired.erase(it); else ++it;
            }
            // Capture states that may be created and retired by the same item.
            size_t beforeProcessingBytes = tangentStorageBytes(tangent);
            replaceStorageBytes(currentAlgorithmBytes,oldTangentBytes,beforeProcessingBytes);
            peakAlgorithmBytes = std::max(peakAlgorithmBytes,currentAlgorithmBytes);
            for (auto it = tangent.active.begin(); it != tangent.active.end();) {
                int k = it->first; State& state = it->second;
                Real threshold = std::exp(k*logValueBase)/4;
                if (normalized >= 0.5L && singletonValue/normalized >= threshold) {
                    tangent.retired.insert(k);
                    it = tangent.active.erase(it);
                    continue;
                }
                bool added = false;
                if (state.firstCost+normalized <= 1 &&
                    feasible(state.first.mu+mu, state.first.variance+var, z, budget)) {
                    uint32_t marginal = oracle.marginalHits(state.first, e);
                    if (oracle.toSpread(marginal) >= threshold*normalized) {
                        addItem(state.first, e, marginal, train, resources);
                        state.firstCost += normalized; added = true;
                    }
                }
                if (!added && state.secondCost+normalized <= 1 &&
                    feasible(state.second.mu+mu, state.second.variance+var, z, budget)) {
                    uint32_t marginal = oracle.marginalHits(state.second, e);
                    if (oracle.toSpread(marginal) >= threshold*normalized) {
                        addItem(state.second, e, marginal, train, resources);
                        state.secondCost += normalized;
                    }
                }
                ++it;
            }
            size_t afterProcessingBytes = tangentStorageBytes(tangent);
            replaceStorageBytes(currentAlgorithmBytes,beforeProcessingBytes,afterProcessingBytes);
            peakAlgorithmBytes = std::max(peakAlgorithmBytes,currentAlgorithmBytes);
        }
        size_t states = 0, slots = 0;
        for (const auto& [j,tangent] : tangents) {
            for (const auto& [k,state] : tangent.active) {
                (void)j; (void)k; ++states;
                slots += state.first.items.size()+state.second.items.size();
            }
        }
        peakStates = std::max(peakStates, states); peakSlots = std::max(peakSlots, slots);
    }
    // RR coverage is monotone, so S1 itself is an optimal unconstrained subset of S1.
    for (const auto& [j,tangent] : tangents)
        for (const auto& [k,state] : tangent.active) {
            (void)j; (void)k; consider(oracle, state.first, best); consider(oracle, state.second, best);
        }
    peakAlgorithmBytes = std::max(peakAlgorithmBytes,focusStorageBytes(best,tangents));
    auto finished = Clock::now();
    RunResult result;
    result.items = best.items; result.trainHits = best.hits;
    result.evalHits = evaluate(evaluation, result.items);
    result.mu = best.mu; result.variance = best.variance; result.queries = oracle.queries;
    result.milliseconds = std::chrono::duration<double,std::milli>(finished-started).count();
    result.tangents = tangents.size(); result.peakStates = peakStates; result.peakSlots = peakSlots;
    result.algorithmMemoryBytes = std::max(peakAlgorithmBytes,
        sizeof(best)+candidateDynamicBytes(best)) + evaluation.words*sizeof(uint64_t);
    return result;
}

Config parseArguments(int argc, char** argv) {
    Config c;
    auto take = [&](int& i) -> std::string {
        if (++i >= argc) throw std::runtime_error("Missing value after option");
        return argv[i];
    };
    bool absoluteBudget = false, ratioBudget = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--graph") c.graph = take(i);
        else if (a == "--order") c.order = take(i);
        else if (a == "--rr-samples") c.rrSamples = std::stoull(take(i));
        else if (a == "--eval-samples") c.evalSamples = std::stoull(take(i));
        else if (a == "--p") c.edgeProbability = std::stold(take(i));
        else if (a == "--epsilon") c.epsilon = std::stold(take(i));
        else if (a == "--alpha") c.alpha = std::stold(take(i));
        else if (a == "--budget") { c.budget = std::stold(take(i)); absoluteBudget = true; }
        else if (a == "--budget-ratio") { c.budgetRatio = std::stold(take(i)); ratioBudget = true; }
        else if (a == "--uncertainty") c.uncertainty = std::stold(take(i));
        else if (a == "--resource-seed") c.resourceSeed = std::stoull(take(i));
        else if (a == "--rr-seed") c.rrSeed = std::stoull(take(i));
        else if (a == "--eval-seed") c.evalSeed = std::stoull(take(i));
        else if (a == "--order-seed") c.orderSeed = std::stoull(take(i));
        else if (a == "--print-seeds") c.printSeeds = true;
        else if (a == "--help") {
            std::cout << "Usage: facebook_experiment [--graph facebook] [--budget 100] [--alpha .05]\n"
                      << "  [--epsilon .02] [--uncertainty .5] [--p .01]\n"
                      << "  [--rr-samples 50000] [--eval-samples 100000]\n"
                      << "  [--order random|mu_asc|mu_desc] [--resource-seed N] [--print-seeds]\n";
            std::exit(0);
        } else throw std::runtime_error("Unknown option: " + a);
    }
    if (absoluteBudget && ratioBudget) throw std::runtime_error("Choose --budget or --budget-ratio, not both");
    if (!(c.rrSamples > 0 && c.evalSamples > 0 && c.edgeProbability > 0 && c.edgeProbability <= 1 &&
          c.epsilon > 0 && c.epsilon < 0.0625L && c.alpha > 0 && c.alpha < 0.5L &&
          c.uncertainty > 0 && c.budget > 0))
        throw std::runtime_error("Invalid numeric parameter");
    if (c.rrSamples > std::numeric_limits<uint32_t>::max() ||
        c.evalSamples > std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("RR sample count exceeds the 32-bit representation");
    if (ratioBudget && !(c.budgetRatio > 0 && c.budgetRatio <= 1))
        throw std::runtime_error("budget-ratio must be in (0,1]");
    if (c.order != "random" && c.order != "mu_asc" && c.order != "mu_desc")
        throw std::runtime_error("order must be random, mu_asc, or mu_desc");
    return c;
}

void printResult(const std::string& name, const RunResult& r, const Config& c,
                 const Graph& g, const RRBank& train, const RRBank& evaluation,
                 Real z, Real budget, Real sumMu, double rrMilliseconds,
                 size_t sharedMemoryBytes) {
    Real trainSpread = Real(g.n)*r.trainHits/train.samples;
    Real evalSpread = Real(g.n)*r.evalHits/evaluation.samples;
    Real evalRate = Real(r.evalHits)/evaluation.samples;
    Real evalStandardError = Real(g.n)*std::sqrt(evalRate*(1-evalRate)/evaluation.samples);
    Real evalLow = std::max(Real(0), evalSpread-Real(1.96)*evalStandardError);
    Real evalHigh = std::min(Real(g.n), evalSpread+Real(1.96)*evalStandardError);
    Real score = chanceScore(r.mu, r.variance, z);
    constexpr Real bytesPerMiB = 1024*1024;
    Real algorithmMemoryMiB = Real(r.algorithmMemoryBytes)/bytesPerMiB;
    Real sharedMemoryMiB = Real(sharedMemoryBytes)/bytesPerMiB;
    Real totalMemoryMiB = algorithmMemoryMiB+sharedMemoryMiB;
    std::cout << name << ',' << c.budgetRatio << ',' << budget << ',' << sumMu << ','
              << trainSpread << ',' << evalSpread << ',' << r.queries << ',' << totalMemoryMiB << ',' << r.milliseconds << ','
              << algorithmMemoryMiB << ',' << sharedMemoryMiB << ',' << r.milliseconds+rrMilliseconds << ','
              << feasible(r.mu,r.variance,z,budget) << ',' << score << ',' << score/budget << ','
              << r.items.size() << ',' << evalStandardError << ','
              << evalLow << ',' << evalHigh << ',' << g.n << ',' << g.arcs << ','
              << c.rrSamples << ',' << c.evalSamples << ',' << c.edgeProbability << ','
              << c.epsilon << ',' << c.alpha << ',' << c.uncertainty << ',' << c.order << ','
              << c.resourceSeed << ',' << c.rrSeed << ',' << c.evalSeed << ',' << c.orderSeed << ','
              << rrMilliseconds << ','
              << r.tangents << ',' << r.peakStates << ',' << r.peakSlots << '\n';
    if (c.printSeeds) {
        std::cerr << name << " seeds:";
        for (int e : r.items) std::cerr << ' ' << e;
        std::cerr << '\n';
    }
}

int main(int argc, char** argv) {
    try {
        Config c = parseArguments(argc, argv);
        Graph graph = loadGraph(c.graph);
        Resources resources = makeResources(graph.n, c.uncertainty, c.resourceSeed);
        if (c.budgetRatio > 0) c.budget = c.budgetRatio*resources.sumMu;
        Real z = gaussianQuantile(c.alpha);
        auto rrStart = Clock::now();
        RRBank train = makeRRBank(graph, c.rrSamples, c.edgeProbability, c.rrSeed);
        RRBank evaluation = makeRRBank(graph, c.evalSamples, c.edgeProbability, c.evalSeed);
        auto rrEnd = Clock::now();
        double rrMilliseconds = std::chrono::duration<double,std::milli>(rrEnd-rrStart).count();
        std::cerr << "Loaded " << graph.n << " nodes, " << graph.arcs << " directed arcs. "
                  << "RR generation ms=" << rrMilliseconds
                  << ", train mean/max size=" << Real(train.memberships)/train.samples << '/' << train.maxSetSize
                  << ", eval mean/max size=" << Real(evaluation.memberships)/evaluation.samples << '/' << evaluation.maxSetSize << '\n';
        std::vector<int> order(static_cast<size_t>(graph.n));
        std::iota(order.begin(), order.end(), 0);
        if (c.order == "random") {
            std::mt19937_64 rng(c.orderSeed); std::shuffle(order.begin(), order.end(), rng);
        } else std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            return c.order == "mu_asc" ? resources.mu[static_cast<size_t>(a)] < resources.mu[static_cast<size_t>(b)]
                                       : resources.mu[static_cast<size_t>(a)] > resources.mu[static_cast<size_t>(b)];
        });
        RunResult greedy = offlineGreedy(graph, train, evaluation, resources, z, c.budget);
        RunResult streamed = focus(graph, train, evaluation, resources, order, z, c.budget, c.epsilon);
        size_t sharedMemoryBytes = graphStorageBytes(graph)+resourcesStorageBytes(resources)+
            rrStorageBytes(train)+rrStorageBytes(evaluation)+order.capacity()*sizeof(int);
        std::cout << std::setprecision(12)
                  << "algorithm,budget_ratio,B,sum_mu,f_value,eval_f_value,queries,memory_mb_est,running_time_ms,algorithm_memory_mb_est,shared_memory_mb_est,total_time_ms,feasible,chance_score,score_over_B,size,eval_f_se,eval_f_ci95_low,eval_f_ci95_high,n,arcs,rr_samples,eval_samples,ic_probability,epsilon,alpha,uncertainty,order,resource_seed,rr_seed,eval_seed,order_seed,rr_generation_ms,tangents,peak_active_states,peak_candidate_slots\n";
        printResult("Offline_Greedy_CC",greedy,c,graph,train,evaluation,z,c.budget,
                    resources.sumMu,rrMilliseconds,sharedMemoryBytes);
        printResult("FOCUS_RR",streamed,c,graph,train,evaluation,z,c.budget,
                    resources.sumMu,rrMilliseconds,sharedMemoryBytes);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
