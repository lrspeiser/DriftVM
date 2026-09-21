#include "core.hpp"
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <set>
#include <tuple>
namespace fs=std::filesystem;
using namespace ds;
struct Individual {uint64_t id=0,parent=0,birth=0;Genome g;Evaluation e;};
struct Settings {uint64_t births=1000000,seed=1,report=100000;bool modules=true,drift=true;std::string out,resume,feedback;};
bool better(const Evaluation& a,const Evaluation& b){return std::tie(a.errors,a.cost,a.depth)<std::tie(b.errors,b.cost,b.depth);}
Individual read_individual(std::istream& in){
  std::string line;if(!std::getline(in,line))throw std::runtime_error("truncated checkpoint");
  std::istringstream s(line);Individual x;if(!(s>>x.id>>x.parent>>x.birth))throw std::runtime_error("checkpoint individual");
  std::string rest;std::getline(s,rest);x.g=unpack(rest);x.e=evaluate(flatten(x.g));return x;
}
void write_individual(std::ostream& out,const Individual& x){out<<x.id<<' '<<x.parent<<' '<<x.birth<<' '<<pack(x.g)<<'\n';}
class Search {
  Settings c;Rng r;uint64_t birth=0,next=1,accepted=0,invalid=0,correct=0;
  std::vector<Individual> pop,archive,native;
  std::set<std::string> archive_keys;
  std::map<uint64_t,uint64_t> recent;
  std::ofstream progress;
  static constexpr std::size_t POP=256,ARCHIVE=64;
  fs::path path(const char* n){return fs::path(c.out)/n;}
  void add_archive(const Individual& x){
    if(x.e.errors)return;
    if(archive.size()==ARCHIVE && better(archive.back().e,x.e))return;
    auto key=flat_key(flatten(x.g));if(archive_keys.count(key))return;archive_keys.insert(key);
    archive.push_back(x);std::stable_sort(archive.begin(),archive.end(),[](const auto& a,const auto& b){return better(a.e,b.e);});
    if(archive.size()>ARCHIVE){archive_keys.erase(flat_key(flatten(archive.back().g)));archive.pop_back();}
  }
  void load(){
    std::ifstream f(c.resume);std::string magic;if(!std::getline(f,magic)||magic!="DRIFTSORT_CHECKPOINT_1")throw std::runtime_error("bad checkpoint");
    uint64_t seed;bool modules,drift;
    if(!(f>>birth>>next>>accepted>>invalid>>correct>>seed>>modules>>drift)||seed!=c.seed||modules!=c.modules||drift!=c.drift)
      throw std::runtime_error("checkpoint configuration mismatch");
    if(!(f>>r.engine))throw std::runtime_error("checkpoint random state");
    f.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
    auto count=[&](unsigned limit){std::string line;if(!std::getline(f,line))throw std::runtime_error("truncated count");auto n=integer(line);if(n>limit)throw std::runtime_error("checkpoint count limit");return n;};
    auto n=count(POP);if(n!=POP)throw std::runtime_error("checkpoint population");while(n--)pop.push_back(read_individual(f));
    n=count(ARCHIVE);while(n--){archive.push_back(read_individual(f));archive_keys.insert(flat_key(flatten(archive.back().g)));}
    n=count(16);while(n--)native.push_back(read_individual(f));
    n=count(100000);while(n--){std::string line;if(!std::getline(f,line))throw std::runtime_error("checkpoint history");std::istringstream s(line);uint64_t k,v;if(!(s>>k>>v))throw std::runtime_error("checkpoint history");recent[k]=v;}
    if(next!=birth+POP+1)throw std::runtime_error("checkpoint ID inconsistency");
  }
  void save(){
    std::ofstream f(path("checkpoint.txt"));f.exceptions(std::ios::badbit|std::ios::failbit);
    f<<"DRIFTSORT_CHECKPOINT_1\n"<<birth<<' '<<next<<' '<<accepted<<' '<<invalid<<' '<<correct<<' '<<c.seed<<' '<<c.modules<<' '<<c.drift<<'\n'<<r.engine<<'\n';
    f<<pop.size()<<'\n';for(auto& x:pop)write_individual(f,x);f<<archive.size()<<'\n';for(auto& x:archive)write_individual(f,x);
    f<<native.size()<<'\n';for(auto& x:native)write_individual(f,x);f<<recent.size()<<'\n';for(auto [k,v]:recent)f<<k<<' '<<v<<'\n';
  }
  void report(){
    unsigned best=2048,blocks=0;for(const auto& x:pop){best=std::min(best,x.e.errors);blocks+=!x.g.blocks.empty();}
    auto cost=archive.empty()?0:archive.front().e.cost;
    std::cout<<"birth="<<birth<<" certified_candidates="<<correct<<" archive="<<archive.size()<<" best_cost_proxy="<<cost
      <<" live_with_blocks="<<blocks<<" invalid="<<invalid<<" (native speed measured after this search phase)\n"<<std::flush;
    progress<<birth<<','<<correct<<','<<archive.size()<<','<<cost<<','<<best<<','<<blocks<<','<<accepted<<','<<invalid<<'\n';progress.flush();
  }
 public:
  explicit Search(Settings cfg):c(std::move(cfg)),r(c.seed){
    if(c.out.empty())throw std::runtime_error("--out NEW_DIRECTORY is required");
    auto root=fs::path(c.out);if(!root.parent_path().empty())fs::create_directories(root.parent_path());
    if(!fs::create_directory(root))throw std::runtime_error("output exists; refusing overwrite");
    progress.open(path("progress.csv"));progress.exceptions(std::ios::failbit|std::ios::badbit);
    progress<<"birth,certified_candidate_evaluations,archive_size,best_cost_proxy,best_errors,live_with_blocks,accepted,invalid\n";
    if(!c.resume.empty())load();else{
      // Warm start is explicitly an existing human 28-comparator insertion network.
      // The stronger 19-comparator reference is NOT seeded into this search.
      for(unsigned i=0;i<POP;++i){Individual x;x.id=next++;x.g=i<32?insertion_seed():random_genome(r);x.e=evaluate(flatten(x.g));pop.push_back(x);add_archive(x);++recent[x.e.signature];}
    }
    if(!c.feedback.empty()){
      std::ifstream f(c.feedback);if(!f)throw std::runtime_error("missing native feedback");native.clear();std::string s;
      while(std::getline(f,s)){if(s.empty())continue;std::istringstream line(s);auto x=read_individual(line);if(x.e.errors)throw std::runtime_error("native feedback not correct");native.push_back(x);if(native.size()>16)throw std::runtime_error("native feedback limit");}
    }
    if(c.births>100000000000ULL||birth>std::numeric_limits<uint64_t>::max()-c.births-POP-1)throw std::runtime_error("birth limit");
  }
  void run(){
    const auto end=birth+c.births;
    for(;birth<end;){++birth;auto victim=r.index(POP);unsigned lane=victim<192?0:victim<224?1:(c.drift?2:0);
      auto pick=[&](){if(r.index(10)==0)return r.index(POP);if(lane==1)return std::size_t(192+r.index(32));
        if(lane==2)return std::size_t(224+r.index(32));
        if(c.drift)return r.index(192);
        auto idx=r.index(224);return idx<192?idx:idx+32;};
      Individual p;
      if(lane==0&&!native.empty()&&r.index(5)==0)p=native[r.index(native.size())];
      else{auto a=pick(),b=pick();if(lane==2)p=pop[a];else if(lane==1)p=recent[pop[a].e.signature]<=recent[pop[b].e.signature]?pop[a]:pop[b];
        else p=better(pop[a].e,pop[b].e)?pop[a]:pop[b];}
      Individual x=p;x.parent=p.id;x.id=next++;x.birth=birth;std::string mutation;
      bool valid=mutate(x.g,r,c.modules,mutation);bool coin=r.index(2)==0;
      if(!valid){++invalid;}else{
        x.e=evaluate(flatten(x.g));if(!x.e.errors){++correct;add_archive(x);}
        bool keep=lane==2;
        if(lane==0)keep=better(x.e,pop[victim].e)||(!better(pop[victim].e,x.e)&&coin);
        if(lane==1){auto a=recent[x.e.signature],b=recent[pop[victim].e.signature];keep=a<b||(a==b&&coin);}
        if(keep){pop[victim]=std::move(x);++accepted;++recent[pop[victim].e.signature];}
        // Bounded recent-history novelty, deliberately not an unbounded count of ideas.
        if(recent.size()>100000)recent.clear();
      }
      if(birth%c.report==0)report();
    }
    report();save();
    std::ofstream out(path("candidates.tsv"));out.exceptions(std::ios::badbit|std::ios::failbit);
    out<<"id\tparent\tbirth\tcost_proxy\tdepth_proxy\tblocks\tgenome\n";
    for(auto& x:archive)out<<x.id<<'\t'<<x.parent<<'\t'<<x.birth<<'\t'<<x.e.cost<<'\t'<<x.e.depth<<'\t'<<x.g.blocks.size()<<'\t'<<pack(x.g)<<'\n';
    std::ofstream summary(path("summary.txt"));summary<<"DriftSort-0.3\ncompleted_births="<<birth<<"\nseed="<<c.seed
      <<"\nmodules="<<c.modules<<"\ndrift="<<c.drift<<"\ncertified_candidate_evaluations="<<correct<<"\narchive_size="<<archive.size()
      <<"\nCorrectness: all 256 binary vectors, including output multiplicities.\nCost is a search proxy; no native speed claim is made here.\n";
  }
};
void self_test(){
  auto require=[](bool b,const char* m){if(!b)throw std::runtime_error(m);};
  auto seed=insertion_seed(),ref=network19();require(ref.program.size()==19,"reference length");
  require(evaluate(seed.program).errors==0&&evaluate(ref.program).errors==0,"positive controls");
  Genome bad;bad.program={op(3,0,1)};require(evaluate(bad.program).errors>0,"copy multiplicity negative control");
  auto g=seed;Seq b(g.program.begin(),g.program.begin()+4);g.blocks.push_back(b);g.program.erase(g.program.begin(),g.program.begin()+4);g.program.insert(g.program.begin(),REF);
  require(flatten(g)==seed.program,"factoring semantics");require(pack(unpack(pack(g)))==pack(g),"serialization");
  bool caught=false;try{g.blocks[0][0]=REF;flatten(g);}catch(...){caught=true;}require(caught,"cycle rejection");
  Rng rng(42);
  for(unsigned i=0;i<200;++i){auto random=random_genome(rng);auto flat=flatten(random);unsigned errors=0;
    for(unsigned bits=0;bits<256;++bits){Row row;for(unsigned w=0;w<8;++w)row[w]=(bits>>w)&1;auto expected=row;std::sort(expected.begin(),expected.end());auto actual=execute(flat,row);
      for(unsigned w=0;w<8;++w)errors+=actual[w]!=expected[w];}require(errors==evaluate(flat).errors,"scalar/bit-parallel equivalence");}
  for(unsigned i=0;i<10000;++i){Row row;for(auto& x:row)x=uint32_t(rng.engine());auto expected=row;std::sort(expected.begin(),expected.end());require(execute(ref.program,row)==expected,"reference u32");}
  for(auto s:{"0 0","1 999 0","1 0 1 2 256 0","1 -1 0","1 0 0 junk"}){bool rejected=false;try{unpack(s);}catch(...){rejected=true;}require(rejected,"malformed input");}
  std::cout<<"sort-core: PASS (binary proof checks, scalar cross-checks, controls, bounds, round trips)\n";
}
int main(int argc,char** argv){try{Settings c;for(int i=1;i<argc;++i){std::string a=argv[i];auto val=[&](){if(++i>=argc)throw std::runtime_error("missing value");return std::string(argv[i]);};
  if(a=="--self-test"){self_test();return 0;}else if(a=="--births")c.births=integer(val());else if(a=="--seed")c.seed=integer(val());
  else if(a=="--report-every")c.report=integer(val());else if(a=="--out")c.out=val();else if(a=="--resume")c.resume=val();
  else if(a=="--feedback")c.feedback=val();else if(a=="--no-modules")c.modules=false;else if(a=="--no-drift")c.drift=false;
  else throw std::runtime_error("unknown argument: "+a);}
  if(!c.report)throw std::runtime_error("report interval must be positive");
  Search(c).run();return 0;
}catch(const std::exception& e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}}
