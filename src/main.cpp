#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace driftvm {

constexpr std::size_t kRegisters = 8;
constexpr std::size_t kMemory = 32;
constexpr std::size_t kOpcodes = 32;
constexpr std::size_t kMicroOpsPerOpcode = 4;
constexpr std::size_t kInitialProgramLength = 24;
constexpr std::size_t kMinProgramLength = 4;
constexpr std::size_t kMaxProgramLength = 96;
constexpr std::size_t kProbeCount = 16;
constexpr std::size_t kMaxStepsPerProbe = 128;

enum class MicroOp : uint8_t {
  NOP, LOAD_A, LOAD_B, LOAD_IMM, MOV, ADD, SUB, XOR, AND, OR,
  SHL1, SHR1, INC, DEC, LOAD_MEM, STORE_MEM, CMP_EQ, CMP_LT,
  SKIP_IF_ZERO, SKIP_IF_NONZERO, EMIT, COUNT
};

constexpr std::size_t kMicroOpCount = static_cast<std::size_t>(MicroOp::COUNT);

struct Semantics {
  std::array<MicroOp, kMicroOpsPerOpcode> ops{};
};

struct Genome {
  std::vector<uint8_t> program;
  std::array<Semantics, kOpcodes> language;
};

struct Behavior {
  std::array<uint8_t, kProbeCount> output{};
  uint64_t signature = 0;
  double reward = 0.0;
  double novelty = 0.0;
  int resources = 0;
};

struct Organism {
  uint64_t id = 0;
  uint64_t parent_id = 0;
  uint64_t birth = 0;
  Genome genome;
  Behavior behavior;
  double score = 0.0;
};

struct Config {
  uint64_t births = 100000;
  std::size_t population = 256;
  uint64_t seed = 1;
  uint64_t report_every = 10000;
  std::string out = "out/run";
  double semantic_mutation_rate = 0.003;
  double drift_survival_rate = 0.01;
};

struct Probe {
  uint8_t a;
  uint8_t b;
};

using ResourceFn = uint8_t (*)(uint8_t, uint8_t);

struct Resource {
  std::string name;
  ResourceFn fn;
  double base_reward;
};

static uint8_t r_xor(uint8_t a, uint8_t b) { return a ^ b; }
static uint8_t r_add(uint8_t a, uint8_t b) { return static_cast<uint8_t>(a + b); }
static uint8_t r_sub(uint8_t a, uint8_t b) { return static_cast<uint8_t>(a - b); }
static uint8_t r_and(uint8_t a, uint8_t b) { return a & b; }
static uint8_t r_or(uint8_t a, uint8_t b) { return a | b; }
static uint8_t r_max(uint8_t a, uint8_t b) { return std::max(a, b); }
static uint8_t r_min(uint8_t a, uint8_t b) { return std::min(a, b); }
static uint8_t r_eq(uint8_t a, uint8_t b) { return a == b ? 1 : 0; }
static uint8_t r_parity(uint8_t a, uint8_t b) {
  uint8_t x = static_cast<uint8_t>(a ^ b);
  x ^= static_cast<uint8_t>(x >> 4);
  x ^= static_cast<uint8_t>(x >> 2);
  x ^= static_cast<uint8_t>(x >> 1);
  return x & 1;
}
static uint8_t r_rotate_xor(uint8_t a, uint8_t b) {
  uint8_t r = static_cast<uint8_t>((a << 1) | (a >> 7));
  return static_cast<uint8_t>(r ^ b);
}

const std::array<Resource, 10> kResources{{
  {"XOR", r_xor, 8.0},
  {"ADD", r_add, 8.0},
  {"SUB", r_sub, 8.0},
  {"AND", r_and, 7.0},
  {"OR", r_or, 7.0},
  {"MAX", r_max, 9.0},
  {"MIN", r_min, 9.0},
  {"EQ", r_eq, 11.0},
  {"PARITY", r_parity, 13.0},
  {"ROTATE_XOR", r_rotate_xor, 15.0},
}};

uint64_t fnv1a(const std::array<uint8_t, kProbeCount>& data) {
  uint64_t h = 1469598103934665603ull;
  for (auto x : data) {
    h ^= x;
    h *= 1099511628211ull;
  }
  return h;
}

class Simulation {
 public:
  explicit Simulation(Config cfg)
      : cfg_(std::move(cfg)), rng_(cfg_.seed), byte_(0, 255), opcode_(0, kOpcodes - 1) {
    probes_ = make_probes();
    std::filesystem::create_directories(cfg_.out);
    events_.open(std::filesystem::path(cfg_.out) / "events.csv");
    events_ << "birth,id,parent,survived,replaced,score,reward,novelty,resources,signature,program_len,semantic_mutation\n";
  }

  void run() {
    initialize_population();

    for (uint64_t birth = 1; birth <= cfg_.births; ++birth) {
      const std::size_t parent_idx = select_parent();
      Organism child = population_[parent_idx];
      child.parent_id = child.id;
      child.id = next_id_++;
      child.birth = birth;

      const bool semantic_mutation = mutate(child.genome);
      child.behavior = evaluate(child.genome);
      child.score = child.behavior.reward + child.behavior.novelty;

      const std::size_t victim_idx = random_index(population_.size());
      const auto victim_score = population_[victim_idx].score;
      const bool better = child.score > victim_score;
      const bool neutral = uniform01() < cfg_.drift_survival_rate;
      const bool survives = better || neutral;

      uint64_t replaced_id = 0;
      if (survives) {
        replaced_id = population_[victim_idx].id;
        population_[victim_idx] = child;
        note_behavior(child.behavior.signature);
        note_resources(child.genome);
      }

      events_ << birth << ',' << child.id << ',' << child.parent_id << ','
              << (survives ? 1 : 0) << ',' << replaced_id << ','
              << std::fixed << std::setprecision(6) << child.score << ','
              << child.behavior.reward << ',' << child.behavior.novelty << ','
              << child.behavior.resources << ',' << child.behavior.signature << ','
              << child.genome.program.size() << ',' << (semantic_mutation ? 1 : 0) << '\n';

      if (cfg_.report_every && birth % cfg_.report_every == 0) {
        report(birth);
      }
    }

    write_summary();
  }

 private:
  Config cfg_;
  std::mt19937_64 rng_;
  std::uniform_int_distribution<int> byte_;
  std::uniform_int_distribution<int> opcode_;
  std::array<Probe, kProbeCount> probes_{};
  std::vector<Organism> population_;
  std::unordered_map<uint64_t, uint64_t> behavior_counts_;
  std::array<uint64_t, kResources.size()> resource_discoveries_{};
  std::ofstream events_;
  uint64_t next_id_ = 1;

  std::array<Probe, kProbeCount> make_probes() {
    std::array<Probe, kProbeCount> p{};
    std::mt19937_64 r(cfg_.seed ^ 0x9E3779B97F4A7C15ull);
    std::uniform_int_distribution<int> d(0, 255);
    for (auto& x : p) x = {static_cast<uint8_t>(d(r)), static_cast<uint8_t>(d(r))};
    p[0] = {0, 0};
    p[1] = {0, 255};
    p[2] = {255, 0};
    p[3] = {255, 255};
    return p;
  }

  void initialize_population() {
    population_.reserve(cfg_.population);
    for (std::size_t i = 0; i < cfg_.population; ++i) {
      Organism o;
      o.id = next_id_++;
      o.genome = random_genome();
      o.behavior = evaluate(o.genome);
      o.score = o.behavior.reward + o.behavior.novelty;
      population_.push_back(std::move(o));
      note_behavior(population_.back().behavior.signature);
      note_resources(population_.back().genome);
    }
  }

  Genome random_genome() {
    Genome g;
    g.program.resize(kInitialProgramLength);
    for (auto& x : g.program) x = static_cast<uint8_t>(opcode_(rng_));
    for (auto& sem : g.language) {
      for (auto& op : sem.ops) {
        op = static_cast<MicroOp>(random_index(kMicroOpCount));
      }
    }
    return g;
  }

  bool mutate(Genome& g) {
    std::uniform_int_distribution<int> edits_dist(1, 3);
    const int edits = edits_dist(rng_);
    for (int e = 0; e < edits; ++e) {
      const int kind = static_cast<int>(random_index(3));
      if (kind == 0 || g.program.size() <= kMinProgramLength) {
        g.program[random_index(g.program.size())] = static_cast<uint8_t>(opcode_(rng_));
      } else if (kind == 1 && g.program.size() < kMaxProgramLength) {
        auto pos = g.program.begin() + static_cast<std::ptrdiff_t>(random_index(g.program.size() + 1));
        g.program.insert(pos, static_cast<uint8_t>(opcode_(rng_)));
      } else if (g.program.size() > kMinProgramLength) {
        auto pos = g.program.begin() + static_cast<std::ptrdiff_t>(random_index(g.program.size()));
        g.program.erase(pos);
      }
    }

    bool semantic = false;
    if (uniform01() < cfg_.semantic_mutation_rate) {
      auto& sem = g.language[random_index(kOpcodes)];
      sem.ops[random_index(kMicroOpsPerOpcode)] =
          static_cast<MicroOp>(random_index(kMicroOpCount));
      semantic = true;
    }
    return semantic;
  }

  Behavior evaluate(const Genome& g) {
    Behavior b;
    for (std::size_t i = 0; i < probes_.size(); ++i) {
      b.output[i] = execute(g, probes_[i]);
    }
    b.signature = fnv1a(b.output);

    for (std::size_t r = 0; r < kResources.size(); ++r) {
      bool match = true;
      for (std::size_t i = 0; i < probes_.size(); ++i) {
        if (b.output[i] != kResources[r].fn(probes_[i].a, probes_[i].b)) {
          match = false;
          break;
        }
      }
      if (match) {
        const double crowding = 1.0 + static_cast<double>(resource_discoveries_[r]) / 1000.0;
        b.reward += kResources[r].base_reward / crowding;
        ++b.resources;
      }
    }

    auto it = behavior_counts_.find(b.signature);
    const uint64_t seen = it == behavior_counts_.end() ? 0 : it->second;
    b.novelty = 2.0 / (1.0 + static_cast<double>(seen));
    return b;
  }

  uint8_t execute(const Genome& g, Probe p) const {
    std::array<uint8_t, kRegisters> r{};
    std::array<uint8_t, kMemory> mem{};
    r[0] = p.a;
    r[1] = p.b;
    std::size_t pc = 0;
    uint8_t last_emit = 0;
    bool emitted = false;

    for (std::size_t step = 0; step < kMaxStepsPerProbe && !g.program.empty(); ++step) {
      const uint8_t code = g.program[pc % g.program.size()] % kOpcodes;
      const auto& sem = g.language[code];
      bool skip_next_opcode = false;

      for (auto op : sem.ops) {
        switch (op) {
          case MicroOp::NOP: break;
          case MicroOp::LOAD_A: r[2] = p.a; break;
          case MicroOp::LOAD_B: r[2] = p.b; break;
          case MicroOp::LOAD_IMM: r[2] = code; break;
          case MicroOp::MOV: r[3] = r[2]; break;
          case MicroOp::ADD: r[2] = static_cast<uint8_t>(r[2] + r[3]); break;
          case MicroOp::SUB: r[2] = static_cast<uint8_t>(r[2] - r[3]); break;
          case MicroOp::XOR: r[2] ^= r[3]; break;
          case MicroOp::AND: r[2] &= r[3]; break;
          case MicroOp::OR: r[2] |= r[3]; break;
          case MicroOp::SHL1: r[2] = static_cast<uint8_t>(r[2] << 1); break;
          case MicroOp::SHR1: r[2] = static_cast<uint8_t>(r[2] >> 1); break;
          case MicroOp::INC: ++r[2]; break;
          case MicroOp::DEC: --r[2]; break;
          case MicroOp::LOAD_MEM: r[2] = mem[r[3] % kMemory]; break;
          case MicroOp::STORE_MEM: mem[r[3] % kMemory] = r[2]; break;
          case MicroOp::CMP_EQ: r[2] = (r[2] == r[3]) ? 1 : 0; break;
          case MicroOp::CMP_LT: r[2] = (r[2] < r[3]) ? 1 : 0; break;
          case MicroOp::SKIP_IF_ZERO: if (r[2] == 0) skip_next_opcode = true; break;
          case MicroOp::SKIP_IF_NONZERO: if (r[2] != 0) skip_next_opcode = true; break;
          case MicroOp::EMIT: last_emit = r[2]; emitted = true; break;
          case MicroOp::COUNT: break;
        }
      }
      pc += skip_next_opcode ? 2 : 1;
      if (pc >= g.program.size()) break;
    }
    return emitted ? last_emit : r[2];
  }

  std::size_t select_parent() {
    const std::size_t a = random_index(population_.size());
    const std::size_t b = random_index(population_.size());
    if (population_[a].score == population_[b].score) {
      return uniform01() < 0.5 ? a : b;
    }
    return population_[a].score > population_[b].score ? a : b;
  }

  void note_behavior(uint64_t sig) { ++behavior_counts_[sig]; }

  void note_resources(const Genome& g) {
    Behavior b = evaluate(g);
    for (std::size_t r = 0; r < kResources.size(); ++r) {
      bool match = true;
      for (std::size_t i = 0; i < probes_.size(); ++i) {
        if (b.output[i] != kResources[r].fn(probes_[i].a, probes_[i].b)) {
          match = false;
          break;
        }
      }
      if (match) ++resource_discoveries_[r];
    }
  }

  void report(uint64_t birth) {
    double best = 0.0;
    double avg = 0.0;
    std::size_t resource_bearers = 0;
    for (const auto& o : population_) {
      best = std::max(best, o.score);
      avg += o.score;
      if (o.behavior.resources > 0) ++resource_bearers;
    }
    avg /= std::max<std::size_t>(1, population_.size());
    std::cout << "birth=" << birth
              << " best=" << std::fixed << std::setprecision(3) << best
              << " avg=" << avg
              << " behaviors=" << behavior_counts_.size()
              << " resource_bearers=" << resource_bearers
              << '\n';
  }

  void write_summary() {
    std::ofstream out(std::filesystem::path(cfg_.out) / "summary.txt");
    out << "DriftVM Cambrian-0\n";
    out << "seed=" << cfg_.seed << "\n";
    out << "births=" << cfg_.births << "\n";
    out << "population=" << cfg_.population << "\n";
    out << "distinct_behaviors=" << behavior_counts_.size() << "\n";
    out << "resource_discoveries:\n";
    for (std::size_t i = 0; i < kResources.size(); ++i) {
      out << "  " << kResources[i].name << "=" << resource_discoveries_[i] << "\n";
    }
  }

  std::size_t random_index(std::size_t n) {
    std::uniform_int_distribution<std::size_t> d(0, n - 1);
    return d(rng_);
  }

  double uniform01() {
    return std::generate_canonical<double, 64>(rng_);
  }
};

Config parse_args(int argc, char** argv) {
  Config cfg;
  auto need_value = [&](int& i) -> std::string {
    if (i + 1 >= argc) throw std::runtime_error(std::string("missing value after ") + argv[i]);
    return argv[++i];
  };

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--births") cfg.births = std::stoull(need_value(i));
    else if (a == "--population") cfg.population = std::stoull(need_value(i));
    else if (a == "--seed") cfg.seed = std::stoull(need_value(i));
    else if (a == "--report-every") cfg.report_every = std::stoull(need_value(i));
    else if (a == "--out") cfg.out = need_value(i);
    else if (a == "--semantic-mutation-rate") cfg.semantic_mutation_rate = std::stod(need_value(i));
    else if (a == "--drift-survival-rate") cfg.drift_survival_rate = std::stod(need_value(i));
    else if (a == "--help" || a == "-h") {
      std::cout
          << "DriftVM Cambrian-0\n"
          << "  --births N                    total mutant births (default 100000)\n"
          << "  --population N                live population (default 256)\n"
          << "  --seed N                      deterministic random seed\n"
          << "  --report-every N              console progress interval\n"
          << "  --out PATH                    output directory\n"
          << "  --semantic-mutation-rate P    probability per birth\n"
          << "  --drift-survival-rate P       neutral replacement probability\n";
      std::exit(0);
    } else {
      throw std::runtime_error("unknown argument: " + a);
    }
  }

  if (cfg.population < 2) throw std::runtime_error("population must be >= 2");
  return cfg;
}

}  // namespace driftvm

int main(int argc, char** argv) {
  try {
    driftvm::Simulation sim(driftvm::parse_args(argc, argv));
    sim.run();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << '\n';
    return 1;
  }
}
