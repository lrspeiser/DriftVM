#include "core.hpp"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#ifndef DRIFTVM_REVISION
#define DRIFTVM_REVISION "unknown"
#endif
namespace driftvm {
namespace fs=std::filesystem;
constexpr const char* VERSION="Cambrian-0.1";
struct Config {
  uint64_t births=100000,seed=1,world_seed=20260920,report_every=100000;
  std::size_t population=256;
  double semantic_rate=.003,novelty_fraction=.125,drift_fraction=.125;
  std::string out="out/run",inspect;bool all_events=false;
};
uint64_t integer(const std::string& s){
  if(s.empty()||s.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("expected nonnegative integer: "+s);
  std::size_t used=0;auto n=std::stoull(s,&used);if(used!=s.size())throw std::runtime_error("invalid integer");return n;
}
double probability(const std::string& s){
  std::size_t used=0;auto p=std::stod(s,&used);
  if(used!=s.size()||!std::isfinite(p)||p<0||p>1)throw std::runtime_error("probability must be finite and in [0,1]");
  return p;
}
Config parse(int argc,char** argv){
  Config c;for(int i=1;i<argc;++i){std::string a=argv[i];
    auto next=[&](){if(i+1>=argc)throw std::runtime_error("missing value for "+a);return std::string(argv[++i]);};
    if(a=="--births")c.births=integer(next());else if(a=="--seed")c.seed=integer(next());
    else if(a=="--world-seed")c.world_seed=integer(next());else if(a=="--population")c.population=std::size_t(integer(next()));
    else if(a=="--report-every")c.report_every=integer(next());else if(a=="--out")c.out=next();
    else if(a=="--semantic-mutation-rate")c.semantic_rate=probability(next());
    else if(a=="--novelty-fraction")c.novelty_fraction=probability(next());
    else if(a=="--drift-fraction")c.drift_fraction=probability(next());
    else if(a=="--inspect")c.inspect=next();else if(a=="--all-events")c.all_events=true;
    else if(a=="--drift-survival-rate")throw std::runtime_error("use --drift-fraction; the old mixed-score survival rule was removed");
    else if(a=="--help"||a=="-h"){
      std::cout<<"DriftVM "<<VERSION<<"\n--births N --population N --seed N --world-seed N\n"
        <<"--out NEW_DIRECTORY --report-every N (0 disables interim reports)\n"
        <<"--semantic-mutation-rate P --novelty-fraction P --drift-fraction P\n"
        <<"--all-events (otherwise accepted births plus every 10000th failure)\n"
        <<"--inspect FILE.genome (exhaustively verify and disassemble a saved organism)\n";std::exit(0);
    }else throw std::runtime_error("unknown argument: "+a);
  }
  if(c.population<8||c.population>1000000)throw std::runtime_error("population must be 8..1000000");
  if(c.novelty_fraction+c.drift_fraction>.875)throw std::runtime_error("reserve at least 12.5% for performance");
  if(c.births>std::numeric_limits<uint64_t>::max()-uint64_t(c.population)-1)throw std::runtime_error("birth ID overflow");
  return c;
}
struct Organism{uint64_t id=0,parent=0,birth=0,depth=0;Genome genome;Behavior behavior;std::string mutation;};
class Simulation{
  Config c;Rng rng;std::vector<Probe> training,screen;
  std::vector<Organism> population;Counts live{},candidate_births{},admitted_matches{},verification_failures{},first_verified{};
  Mask verified=0;std::unordered_map<std::string,uint64_t> historical,live_behaviors;
  std::unordered_set<std::string> failed_validation_cache;
  std::array<std::size_t,4> boundaries{};
  std::ofstream lineage,events,discoveries,progress;
  uint64_t next_id=1,accepted=0,semantic_births=0,semantic_admitted=0,verification_inputs=0;
  std::array<uint64_t,3> lane_admitted{};
  const std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
  fs::path path(const std::string& name)const{return fs::path(c.out)/name;}
  void open(std::ofstream& f,const std::string& name){f.exceptions(std::ios::failbit|std::ios::badbit);f.open(path(name));}
  static const char* name(std::size_t lane){return lane==0?"performance":lane==1?"novelty":"drift";}
  std::size_t lane_of(std::size_t i)const{return i<boundaries[1]?0:i<boundaries[2]?1:2;}
  double score(const Behavior& b,std::size_t lane)const{
    if(lane==0)return ecological_score(b.candidates,live);
    auto it=historical.find(b.signature);return lane==1?1.0/(1.0+double(it==historical.end()?0:it->second)):0;
  }
  std::size_t parent(std::size_t lane){
    // Protected pools reproduce themselves, with occasional cross-pool transfer.
    if(rng.unit()<.10)return rng.index(population.size());
    auto pick=[&](){return boundaries[lane]+rng.index(boundaries[lane+1]-boundaries[lane]);};
    auto a=pick(),b=pick();if(lane==2)return a;
    double sa=score(population[a].behavior,lane),sb=score(population[b].behavior,lane);
    return sa==sb?(rng.index(2)?a:b):(sa>sb?a:b);
  }
  void add_live(const Behavior& b){++live_behaviors[b.signature];
    for(std::size_t t=0;t<10;++t)if(b.candidates&(1u<<t))++live[t];}
  void remove_live(const Behavior& b){
    auto it=live_behaviors.find(b.signature);if(it==live_behaviors.end()||!it->second)throw std::runtime_error("live behavior underflow");
    if(!--it->second)live_behaviors.erase(it);
    for(std::size_t t=0;t<10;++t)if(b.candidates&(1u<<t)){if(!live[t])throw std::runtime_error("live task underflow");--live[t];}
  }
  void admit_counts(const Behavior& b){++historical[b.signature];for(std::size_t t=0;t<10;++t)if(b.candidates&(1u<<t))++admitted_matches[t];}
  void candidate_counts(const Behavior& b){for(std::size_t t=0;t<10;++t)if(b.candidates&(1u<<t))++candidate_births[t];}
  void record_discovery(const Organism& o,std::size_t t){
    verified|=Mask(1u<<t);first_verified[t]=o.birth;
    save_genome(o.genome,path(std::string("discoveries/")+TASKS[t]+".genome").string());
    std::ofstream text;open(text,std::string("discoveries/")+TASKS[t]+".txt");
    text<<"id="<<o.id<<" parent="<<o.parent<<" birth="<<o.birth<<" depth="<<o.depth<<"\nverified_inputs=65536\nmutation="<<o.mutation<<'\n';
    disassemble(o.genome,text);
    discoveries<<t<<','<<TASKS[t]<<','<<o.id<<','<<o.parent<<','<<o.birth<<','<<o.depth<<",65536\n";discoveries.flush();
    std::cout<<"VERIFIED task="<<TASKS[t]<<" birth="<<o.birth<<" id="<<o.id<<" inputs=65536\n"<<std::flush;
  }
  void check_new_discoveries(const Organism& o){
    const Mask todo=Mask(o.behavior.candidates&Mask(~verified));if(!todo)return;
    auto key=pack(o.genome);if(failed_validation_cache.count(key))return;
    bool failed=false;for(std::size_t t=0;t<10;++t)if(todo&(1u<<t)){
      auto v=verify(o.genome,t);verification_inputs+=v.checked;
      if(v.pass)record_discovery(o,t);else{++verification_failures[t];failed=true;}}
    if(failed){if(failed_validation_cache.size()>=8192)failed_validation_cache.clear();failed_validation_cache.insert(std::move(key));}
  }
  void save_population(uint64_t birth){
    // Inspection snapshots, not resumable RNG checkpoints. Lineage preserves extinct ancestors.
    std::ofstream out;open(out,"population-"+std::to_string(birth)+".tsv");
    out<<"id\tparent\tbirth\tdepth\tpool\tcandidate_mask\tgenome\n";
    for(std::size_t i=0;i<population.size();++i){const auto& o=population[i];
      out<<o.id<<'\t'<<o.parent<<'\t'<<o.birth<<'\t'<<o.depth<<'\t'<<name(lane_of(i))<<'\t'<<o.behavior.candidates<<'\t'<<pack(o.genome)<<'\n';}
  }
  void report(uint64_t birth){
    double best=0;uint64_t depth=0;std::size_t bearers=0;
    for(const auto& o:population){best=std::max(best,quality(o.behavior.candidates));depth=std::max(depth,o.depth);if(o.behavior.candidates)++bearers;}
    unsigned tasks=0;for(unsigned t=0;t<10;++t)if(verified&(1u<<t))++tasks;
    const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::cout<<"birth="<<birth<<" verified_tasks="<<tasks<<"/10 best_candidate_quality="<<best
      <<" admitted_behaviors="<<historical.size()<<" live_behaviors="<<live_behaviors.size()
      <<" candidate_bearers="<<bearers<<" max_lineage_depth="<<depth<<" accepted="<<accepted<<'\n'<<std::flush;
    progress<<birth<<','<<tasks<<','<<best<<','<<historical.size()<<','<<live_behaviors.size()<<','<<bearers<<','<<depth<<','<<accepted<<','<<elapsed<<'\n';
    lineage.flush();events.flush();progress.flush();save_population(birth);
  }
 public:
  explicit Simulation(Config cfg):c(std::move(cfg)),rng(c.seed),training(probes(c.world_seed,32)),screen(probes(c.world_seed^0x517cc1b727220a95ULL,128)){
    // create_directory is an atomic claim; never truncate an old experiment.
    fs::path root(c.out);if(!root.parent_path().empty())fs::create_directories(root.parent_path());
    if(!fs::create_directory(root))throw std::runtime_error("output already exists; choose a new --out directory: "+c.out);
    fs::create_directory(path("discoveries"));
    auto novelty=std::size_t(double(c.population)*c.novelty_fraction),drift=std::size_t(double(c.population)*c.drift_fraction);
    boundaries={0,c.population-novelty-drift,c.population-drift,c.population};
    open(lineage,"lineage.tsv");lineage<<"id\tparent\tbirth\tdepth\treplaced\treason\tmutation\n";
    open(events,"events.csv");events<<"birth,id,parent,survived,pool,reason,child_current_score,victim_current_score,candidate_mask,semantic_change\n";
    open(discoveries,"discoveries.csv");discoveries<<"task_index,task,id,parent,birth,depth,checked_inputs\n";
    open(progress,"progress.csv");progress<<"birth,verified_tasks,best_candidate_quality,admitted_behaviors,live_behaviors,candidate_bearers,max_lineage_depth,accepted,elapsed_seconds\n";
    std::ofstream manifest;open(manifest,"config.txt");manifest<<"version="<<VERSION<<"\nrevision="<<DRIFTVM_REVISION
      <<"\nseed="<<c.seed<<"\nworld_seed="<<c.world_seed<<"\nbirths="<<c.births<<"\npopulation="<<c.population
      <<"\nsemantic_mutation_rate="<<c.semantic_rate<<"\nnovelty_fraction="<<c.novelty_fraction<<"\ndrift_fraction="<<c.drift_fraction
      <<"\nreport_every="<<c.report_every<<"\nall_events="<<c.all_events<<"\ntraining_probes=32\nscreening_probes=128\n";
    for(std::size_t l=0;l<3;++l)manifest<<name(l)<<"_slots="<<boundaries[l+1]-boundaries[l]<<'\n';
    manifest<<"sampling=mt19937_64_rejection_v1\nvm=forward_bytecode_v1_implicit_output\n";
    for(auto p:training)manifest<<"training="<<unsigned(p.a)<<','<<unsigned(p.b)<<'\n';
    for(auto p:screen)manifest<<"screening="<<unsigned(p.a)<<','<<unsigned(p.b)<<'\n';
    std::cout<<"DriftVM "<<VERSION<<" revision="<<DRIFTVM_REVISION<<" performance="<<boundaries[1]<<" novelty="<<novelty<<" drift="<<drift<<'\n';
  }
  void run(){
    population.reserve(c.population);
    for(std::size_t i=0;i<c.population;++i){Organism o;o.id=next_id++;o.genome=founder(rng);o.mutation="F "+pack(o.genome);o.behavior=evaluate(o.genome,training,screen);
      candidate_counts(o.behavior);check_new_discoveries(o);add_live(o.behavior);admit_counts(o.behavior);
      lineage<<o.id<<"\t0\t0\t0\t0\tfounder\tF "<<pack(o.genome)<<'\n';population.push_back(std::move(o));}
    save_population(0);
    for(uint64_t birth=1;birth<=c.births;++birth){
      auto victim=rng.index(population.size()),lane=lane_of(victim);Organism child=population[parent(lane)];
      child.parent=child.id;child.id=next_id++;child.birth=birth;++child.depth;
      auto mutation=mutate(child.genome,rng,c.semantic_rate);child.mutation=mutation.delta;semantic_births+=mutation.semantic?1:0;
      child.behavior=evaluate(child.genome,training,screen);candidate_counts(child.behavior);
      // Certification is an observer, not an extra selection reward; no positive controls are seeded.
      check_new_discoveries(child);
      const double cs=score(child.behavior,lane),vs=score(population[victim].behavior,lane);
      const bool tie_accept=rng.index(2)==0;
      const bool keep=lane==2||cs>vs||(cs==vs&&tie_accept);
      const char* reason=lane==2?"drift":cs>vs?"better":cs==vs?"neutral":"rejected";
      if(keep||c.all_events||birth%10000==0)events<<birth<<','<<child.id<<','<<child.parent<<','<<keep<<','<<name(lane)<<','
        <<(keep?reason:"rejected")<<','<<std::setprecision(17)<<cs<<','<<vs<<','<<child.behavior.candidates<<','<<mutation.semantic<<'\n';
      if(keep){++accepted;++lane_admitted[lane];semantic_admitted+=mutation.semantic?1:0;
        lineage<<child.id<<'\t'<<child.parent<<'\t'<<birth<<'\t'<<child.depth<<'\t'<<population[victim].id<<'\t'<<name(lane)<<'_'<<reason<<'\t'<<mutation.delta<<'\n';
        remove_live(population[victim].behavior);add_live(child.behavior);admit_counts(child.behavior);population[victim]=std::move(child);}
      if(c.report_every&&birth%c.report_every==0)report(birth);
    }
    if(!c.report_every||c.births%c.report_every!=0||c.births==0)report(c.births);
    // Verify every surviving candidate, not every one of millions of trial offspring.
    std::cout<<"Checking final candidate survivors on all 65536 inputs...\n"<<std::flush;
    Counts final_verified{};std::ofstream final;open(final,"final-verification.csv");
    final<<"id,task,pass,checked_inputs,counterexample_a,counterexample_b,actual,expected\n";
    for(const auto& o:population)for(std::size_t t=0;t<10;++t)if(o.behavior.candidates&(1u<<t)){
      auto v=verify(o.genome,t);verification_inputs+=v.checked;if(v.pass)++final_verified[t];
      final<<o.id<<','<<TASKS[t]<<','<<v.pass<<','<<v.checked<<','<<v.a<<','<<v.b<<','<<v.actual<<','<<v.expected<<'\n';}
    std::ofstream summary;open(summary,"summary.txt");
    summary<<"DriftVM "<<VERSION<<"\nseed="<<c.seed<<"\nbirths="<<c.births<<"\npopulation="<<c.population
      <<"\naccepted_offspring="<<accepted<<"\ndistinct_admitted_probe_behaviors="<<historical.size()
      <<"\nsemantic_mutant_births="<<semantic_births<<"\nsemantic_mutants_admitted="<<semantic_admitted
      <<"\nexhaustive_verifier_inputs="<<verification_inputs<<"\n";
    for(std::size_t l=0;l<3;++l)summary<<name(l)<<"_accepted="<<lane_admitted[l]<<'\n';
    summary<<"task,verified_ever,first_verified_birth,candidate_evaluations,admitted_candidate_matches,final_candidate_carriers,final_verified_carriers,discovery_verification_failures\n";
    for(std::size_t t=0;t<10;++t)summary<<TASKS[t]<<','<<bool(verified&(1u<<t))<<','<<((verified&(1u<<t))?std::to_string(first_verified[t]):"NA")
      <<','<<candidate_births[t]<<','<<admitted_matches[t]<<','<<live[t]<<','<<final_verified[t]<<','<<verification_failures[t]<<'\n';
    summary<<"\nCandidate matches are not independent discoveries. Verification certifies the finite byte-input domain only.\n";
    std::cout<<"Completed. Results: "<<c.out<<"\n";
  }
};
int inspect(const std::string& file){auto g=load_genome(file);disassemble(g,std::cout);unsigned passed=0;
  for(std::size_t t=0;t<10;++t){auto v=verify(g,t);passed+=v.pass?1:0;std::cout<<TASKS[t]<<": "<<(v.pass?"VERIFIED":"NO_MATCH")<<" checked="<<v.checked;
    if(!v.pass)std::cout<<" counterexample="<<v.a<<','<<v.b<<" actual="<<v.actual<<" expected="<<v.expected;
    std::cout<<'\n';}
  std::cout<<"verified_tasks="<<passed<<"/10\n";return 0;}
} // namespace driftvm
int main(int argc,char** argv){try{auto c=driftvm::parse(argc,argv);if(!c.inspect.empty())return driftvm::inspect(c.inspect);
  driftvm::Simulation sim(c);sim.run();return 0;}catch(const std::exception& e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}}
