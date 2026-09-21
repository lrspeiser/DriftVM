#include "modules.hpp"
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#ifndef DRIFTVM_REVISION
#define DRIFTVM_REVISION "unversioned"
#endif
namespace driftvm::modules {
namespace fs=std::filesystem;
constexpr const char* VERSION="Cambrian-0.2 / Reusable blocks";
struct Settings {
  uint64_t births=1000000,seed=1,world_seed=20260920,report=10000;
  std::size_t population=256;
  double semantic_rate=.003,module_rate=.01;
  bool modules=true,drift=true;std::string out="out/lab",inspect;
};
uint64_t number(const std::string& s){
  if(s.empty()||s.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("expected an unsigned integer");
  return std::stoull(s);
}
Settings parse(int argc,char** argv){
  Settings c;for(int i=1;i<argc;++i){std::string a=argv[i];auto next=[&](){if(++i>=argc)throw std::runtime_error("missing value for "+a);return std::string(argv[i]);};
    if(a=="--births")c.births=number(next());else if(a=="--seed")c.seed=number(next());
    else if(a=="--population")c.population=std::size_t(number(next()));else if(a=="--world-seed")c.world_seed=number(next());
    else if(a=="--report-every")c.report=number(next());else if(a=="--out")c.out=next();
    else if(a=="--no-modules")c.modules=false;else if(a=="--no-drift")c.drift=false;
    else if(a=="--inspect")c.inspect=next();
    else if(a=="--help"){std::cout<<"driftvm_lab --births N --seed N --out NEW_DIRECTORY --report-every N\n"
      <<"--population 256 --world-seed N --no-modules --no-drift --inspect FILE\n";std::exit(0);}
    else throw std::runtime_error("unknown argument: "+a);
  }
  if(c.population<16||c.population>4096)throw std::runtime_error("population must be 16..4096 for the lab");
  if(!c.report)throw std::runtime_error("report interval must be positive");
  if(c.births>std::numeric_limits<uint64_t>::max()-c.population-1)throw std::runtime_error("birth count overflow");
  return c;
}
struct Individual {
  uint64_t id=0,parent=0,birth=0,depth=0;
  OrganismCode code;Behavior behavior;std::string change;
};
class Lab {
  Settings c;Rng rng;std::vector<Probe> training,screen;
  std::vector<Individual> pop;Counts live{},first{},failures{},final_verified{};
  Mask known=0;std::unordered_map<std::string,uint64_t> history;
  std::unordered_set<std::string> validation_failures;
  std::array<std::size_t,4> bounds{};
  uint64_t accepted=0,invalid=0,semantic=0,block_changes=0,factored=0,verify_inputs=0,sequence=0;
  std::array<uint64_t,3> lane_accept{};
  std::ofstream lineage,progress,transitions;
  std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
  fs::path path(const std::string& s)const{return fs::path(c.out)/s;}
  void open(std::ofstream& o,const std::string& s){o.exceptions(std::ios::badbit|std::ios::failbit);o.open(path(s));}
  static const char* lane_name(std::size_t l){return l==0?"performance":l==1?"novelty":"drift";}
  std::size_t lane(std::size_t i)const{return i<bounds[1]?0:i<bounds[2]?1:2;}
  double score(const Behavior& b,std::size_t l)const{
    if(!l)return ecological_score(b.candidates,live);
    if(l==2)return 0;
    auto it=history.find(b.signature);return 1.0/(1.0+double(it==history.end()?0:it->second));
  }
  std::size_t parent(std::size_t l){
    if(rng.unit()<.10)return rng.index(pop.size());
    auto a=bounds[l]+rng.index(bounds[l+1]-bounds[l]);auto b=bounds[l]+rng.index(bounds[l+1]-bounds[l]);
    if(l==2)return a;
    auto x=score(pop[a].behavior,l),y=score(pop[b].behavior,l);
    return x==y?(rng.index(2)?a:b):x>y?a:b;
  }
  void counts(const Behavior& b,int d){for(std::size_t t=0;t<10;++t)if(b.candidates&(1u<<t)){
    if(d<0){if(!live[t])throw std::runtime_error("carrier accounting underflow");--live[t];}else ++live[t];}}
  std::string individual_json(const Individual& x,std::size_t l)const{
    auto e=expand(x.code);std::ostringstream o;
    o<<"{\"id\":"<<json_quote(std::to_string(x.id))<<",\"parent\":"<<json_quote(std::to_string(x.parent))
      <<",\"birth\":"<<x.birth<<",\"depth\":"<<x.depth<<",\"pool\":"<<json_quote(lane_name(l))
      <<",\"candidate_mask\":"<<x.behavior.candidates<<",\"encoded\":"<<x.code.base.program.size()
      <<",\"expanded\":"<<e.flat.program.size()<<",\"blocks\":"<<x.code.blocks.size()<<",\"block_depth\":"<<e.depth
      <<",\"reused_blocks\":"<<e.reused<<",\"change\":"<<json_quote(x.change)<<",\"genome\":"<<json_quote(pack_code(x.code))<<'}';return o.str();
  }
  void certify(const Individual& x,const Individual* ancestor,std::size_t l,bool admitted,std::size_t parent_lane=0){
    auto todo=Mask(x.behavior.candidates&Mask(~known));if(!todo)return;
    auto key=pack_code(x.code);if(validation_failures.count(key))return;
    auto flat=expand(x.code).flat;bool failed=false;
    for(std::size_t t=0;t<10;++t)if(todo&(1u<<t)){
      auto v=verify(flat,t);verify_inputs+=v.checked;
      if(!v.pass){++failures[t];failed=true;continue;}
      known|=Mask(1u<<t);first[t]=x.birth;Mask parent_verified=0;
      if(ancestor){auto p=expand(ancestor->code).flat;for(std::size_t j=0;j<10;++j){auto pv=verify(p,j);verify_inputs+=pv.checked;if(pv.pass)parent_verified|=Mask(1u<<j);}}
      const auto name=std::string(TASKS[t]);save_code(x.code,path("discoveries/"+name+".genome").string());
      std::ofstream o;open(o,"discoveries/"+name+".json");
      o<<"{\"task\":"<<json_quote(name)<<",\"checked\":65536,\"admitted\":"<<(admitted?"true":"false")
       <<",\"child\":"<<individual_json(x,l)<<",\"parent_verified_mask\":"<<parent_verified<<",\"ancestor\":"
       <<(ancestor?individual_json(*ancestor,parent_lane):"null")<<'}';
      transitions<<x.birth<<','<<TASKS[t]<<','<<x.id<<','<<x.parent<<','<<parent_verified<<','<<admitted<<'\n';transitions.flush();
      std::cout<<"VERIFIED "<<name<<" birth="<<x.birth<<" parent_verified_mask="<<parent_verified<<" inputs=65536\n"<<std::flush;
    }
    if(failed){if(validation_failures.size()>=8192)validation_failures.clear();validation_failures.insert(std::move(key));}
  }
  void snapshot(uint64_t birth,bool complete=false){
    auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::unordered_set<std::string> behaviors;unsigned max_depth=0,reused=0,block_bearers=0;
    for(auto& x:pop){behaviors.insert(x.behavior.signature);auto e=expand(x.code);max_depth=std::max(max_depth,e.depth);reused+=e.reused?1:0;block_bearers+=!x.code.blocks.empty()?1:0;}
    unsigned tasks=0;for(unsigned i=0;i<10;++i)tasks+=(known>>i)&1u;
    progress<<birth<<','<<tasks<<','<<history.size()<<','<<behaviors.size()<<','<<accepted<<','<<invalid<<','<<block_bearers<<','<<max_depth<<','<<reused<<','<<elapsed<<'\n';progress.flush();lineage.flush();
    // Unique immutable filenames avoid Windows replacement semantics and torn reads.
    auto serial=++sequence;std::ostringstream filename;filename<<"state-"<<std::setw(12)<<std::setfill('0')<<serial<<".json";
    auto tmp=filename.str()+".tmp";std::ofstream o;open(o,tmp);
    o<<"{\"schema\":1,\"version\":"<<json_quote(VERSION)<<",\"revision\":"<<json_quote(DRIFTVM_REVISION)
      <<",\"seed\":"<<c.seed<<",\"birth\":"<<birth<<",\"total\":"<<c.births<<",\"complete\":"<<(complete?"true":"false")
      <<",\"elapsed\":"<<elapsed<<",\"accepted\":"<<accepted<<",\"invalid\":"<<invalid
      <<",\"admitted_behaviors\":"<<history.size()<<",\"live_behaviors\":"<<behaviors.size()
      <<",\"semantic_admitted\":"<<semantic<<",\"block_changes_admitted\":"<<block_changes<<",\"factored_admitted\":"<<factored
      <<",\"module_mode\":"<<(c.modules?"true":"false")<<",\"drift_mode\":"<<(c.drift?"true":"false")
      <<",\"block_bearers\":"<<block_bearers<<",\"max_block_depth\":"<<max_depth<<",\"reuse_bearers\":"<<reused
      <<",\"verification_inputs\":"<<verify_inputs<<",\"tasks\":[";
    for(std::size_t t=0;t<10;++t){if(t)o<<',';o<<"{\"name\":"<<json_quote(TASKS[t])<<",\"verified\":"<<((known&(1u<<t))?"true":"false")
      <<",\"first_birth\":"<<((known&(1u<<t))?std::to_string(first[t]):"null")<<",\"carriers\":"<<live[t]
      <<",\"failed_certifications\":"<<failures[t]<<",\"final_verified\":"<<(complete?std::to_string(final_verified[t]):"null")<<'}';}
    o<<"],\"organisms\":[";for(std::size_t i=0;i<pop.size();++i){if(i)o<<',';o<<individual_json(pop[i],lane(i));}o<<"]}";o.close();
    fs::rename(path(tmp),path(filename.str()));
    if(serial>2){std::ostringstream old;old<<"state-"<<std::setw(12)<<std::setfill('0')<<(serial-2)<<".json";std::error_code ec;fs::remove(path(old.str()),ec);}
    std::cout<<"birth="<<birth<<" verified="<<tasks<<"/10 blocks_in="<<block_bearers<<" reuse_in="<<reused
      <<" depth="<<max_depth<<" accepted="<<accepted<<" invalid="<<invalid<<'\n'<<std::flush;
  }
 public:
  explicit Lab(Settings cfg):c(std::move(cfg)),rng(c.seed),training(probes(c.world_seed,32)),screen(probes(c.world_seed^0x517cc1b727220a95ULL,128)){
    fs::path root(c.out);
    if(!root.parent_path().empty())fs::create_directories(root.parent_path());
    if(!fs::create_directory(root))throw std::runtime_error("output exists; choose a new directory");
    fs::create_directory(path("discoveries"));
    auto n=c.population/8,d=c.drift?c.population/8:0;bounds={0,c.population-n-d,c.population-d,c.population};
    open(lineage,"lineage-modules.tsv");lineage<<"id\tparent\tbirth\tdepth\treplaced\treason\trecord\n";
    open(progress,"progress.csv");progress<<"birth,verified_tasks,admitted_behaviors,live_behaviors,accepted,invalid,block_bearers,max_block_depth,reuse_bearers,elapsed_seconds\n";
    open(transitions,"transitions.csv");transitions<<"birth,task,id,parent,parent_verified_mask,admitted\n";
    std::ofstream conf;open(conf,"config.txt");conf<<"version="<<VERSION<<"\nrevision="<<DRIFTVM_REVISION<<"\nseed="<<c.seed<<"\nworld_seed="<<c.world_seed
      <<"\nbirths="<<c.births<<"\npopulation="<<c.population<<"\nmodules="<<c.modules<<"\ndrift="<<c.drift
      <<"\nmodule_rate="<<c.module_rate<<"\nsemantic_rate="<<c.semantic_rate<<"\nexpanded_limit=96\nmax_modules=16\nmax_depth=8\n";
  }
  void run(){
    std::cout<<"DriftVM "<<VERSION<<" revision="<<DRIFTVM_REVISION<<'\n';pop.reserve(c.population);
    for(std::size_t i=0;i<c.population;++i){Individual x;x.id=i+1;x.code.base=founder(rng);x.change="random founder";
      x.behavior=evaluate(x.code.base,training,screen);certify(x,nullptr,lane(i),true);counts(x.behavior,1);++history[x.behavior.signature];
      lineage<<x.id<<"\t0\t0\t0\t0\tfounder\tG "<<pack_code(x.code)<<'\n';pop.push_back(std::move(x));}
    snapshot(0);
    for(uint64_t birth=1;birth<=c.births;++birth){
      auto victim=rng.index(pop.size()),l=lane(victim),p=parent(l);Individual child=pop[p];
      child.parent=child.id;child.id=uint64_t(c.population)+birth;child.birth=birth;++child.depth;
      auto m=mutate_code(child.code,rng,c.semantic_rate,c.module_rate,c.modules);child.change=m.description;
      // Consume the tie coin also for rejected, over-budget mutations.
      bool tie=rng.index(2)==0;
      if(!m.valid)++invalid;else{
        auto e=expand(child.code);child.behavior=evaluate(e.flat,training,screen);
        auto cs=score(child.behavior,l),vs=score(pop[victim].behavior,l);
        bool keep=l==2||cs>vs||(cs==vs&&tie);
        certify(child,&pop[p],l,keep,lane(p));
        if(keep){
          const bool inherited=child.code.base.language==pop[p].code.base.language&&child.code.blocks==pop[p].code.blocks;
          // Full language/module changes; compact complete top-level sequence otherwise.
          auto record=inherited?"P "+bytes_hex(child.code.base.program):"G "+pack_code(child.code);
          lineage<<child.id<<'\t'<<child.parent<<'\t'<<birth<<'\t'<<child.depth<<'\t'<<pop[victim].id<<'\t'
           <<lane_name(l)<<'_'<<(l==2?"random":cs>vs?"better":"tie")<<'\t'<<record<<'\n';
          ++accepted;++lane_accept[l];semantic+=m.semantic?1:0;block_changes+=m.module?1:0;factored+=m.factored?1:0;
          counts(pop[victim].behavior,-1);counts(child.behavior,1);++history[child.behavior.signature];pop[victim]=std::move(child);
        }
      }
      if(birth%c.report==0)snapshot(birth);
    }
    std::ofstream final;open(final,"final-verification.csv");final<<"id,task,pass,checked_inputs,a,b,actual,expected\n";
    for(auto& x:pop){auto e=expand(x.code);for(std::size_t t=0;t<10;++t)if(x.behavior.candidates&(1u<<t)){
      auto v=verify(e.flat,t);verify_inputs+=v.checked;final_verified[t]+=v.pass?1:0;
      final<<x.id<<','<<TASKS[t]<<','<<v.pass<<','<<v.checked<<','<<v.a<<','<<v.b<<','<<v.actual<<','<<v.expected<<'\n';}}
    final.close();snapshot(c.births,true);
    std::ofstream summary;open(summary,"summary.txt");summary<<"DriftVM "<<VERSION<<"\nbirths="<<c.births<<"\naccepted="<<accepted<<"\ninvalid="<<invalid<<"\nfactored_admitted="<<factored
      <<"\nblock_changes_admitted="<<block_changes<<"\nsemantic_admitted="<<semantic<<"\n";
    summary<<"task,verified_ever,first_birth,final_candidates,final_verified\n";
    for(std::size_t t=0;t<10;++t)summary<<TASKS[t]<<','<<bool(known&(1u<<t))<<','<<((known&(1u<<t))?std::to_string(first[t]):"NA")<<','<<live[t]<<','<<final_verified[t]<<'\n';
    summary<<"\nBlock depth and probe diversity are not proofs of intelligence or novelty.\n";
    std::cout<<"Completed. Results: "<<c.out<<'\n';
  }
};
} // namespace
int main(int argc,char** argv){try{
  auto c=driftvm::modules::parse(argc,argv);
  if(!c.inspect.empty()){
    auto code=driftvm::modules::load_code(c.inspect);auto e=driftvm::modules::expand(code);
    std::cout<<"encoded="<<code.base.program.size()<<" expanded="<<e.flat.program.size()<<" blocks="<<code.blocks.size()<<" depth="<<e.depth<<'\n';
    driftvm::disassemble(e.flat,std::cout);for(std::size_t t=0;t<10;++t){auto v=driftvm::verify(e.flat,t);std::cout<<driftvm::TASKS[t]<<' '<<(v.pass?"VERIFIED":"NO_MATCH")<<" checked="<<v.checked<<'\n';}return 0;
  }
  driftvm::modules::Lab lab(c);lab.run();return 0;
}catch(const std::exception& e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}}
