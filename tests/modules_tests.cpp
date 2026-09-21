#include "../src/modules.hpp"
#include <iostream>
using namespace driftvm;
using namespace driftvm::modules;
void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool bad=false;try{f();}catch(const std::exception&){bad=true;}require(bad,"invalid genome accepted");}
int main(int argc,char**argv){try{
  Rng r(79);
  if(argc>1&&std::string(argv[1])=="--vectors"){
    for(int i=0;i<2000;++i){OrganismCode c;c.base=founder(r);for(int j=0;j<8;++j)(void)mutate_code(c,r,.1,.8,true);
      auto flat=expand(c).flat;auto a=uint8_t(r.index(256)),b=uint8_t(r.index(256));
      std::cout<<pack_code(c)<<'\t'<<unsigned(a)<<'\t'<<unsigned(b)<<'\t'<<unsigned(execute(flat,{a,b}))<<'\n';}return 0;
  }
  for(int n=0;n<500;++n){OrganismCode c;c.base=founder(r);auto before=expand(c).flat;
    require(factor(c,0,4),"factoring failed");require(expand(c).flat.program==before.program,"factoring changed flat code");
    require(factor(c,0,4),"nested factoring failed");require(expand(c).depth==2,"missing nesting");
    for(int i=0;i<12;++i){Probe p{uint8_t(r.index(256)),uint8_t(r.index(256))};require(execute(expand(c).flat,p)==execute(before,p),"factor changed output");}
    require(pack_code(unpack_code(pack_code(c)))==pack_code(c),"roundtrip");
  }
  OrganismCode c;c.base=founder(r);factor(c,0,3);auto bad=c;bad.blocks[0][0]=32;rejects([&]{expand(bad);});
  bad=c;bad.base.program[0]=255;rejects([&]{expand(bad);});
  bad=c;bad.blocks[0]={0};rejects([&]{expand(bad);});
  bad=c;bad.base.language[0][0]=255;rejects([&]{expand(bad);});
  bad=c;bad.base.program.assign(40,32);rejects([&]{expand(bad);});
  auto old=expand(c).flat.program;c.blocks.push_back({0,1});prune(c);require(c.blocks.size()==1&&expand(c).flat.program==old,"prune changed code");
  c.base.program={32,32,2,3};require(expand(c).reused==1,"reuse not counted");
  auto frozen=c.base.language;
  for(int i=0;i<5000;++i){auto before=pack_code(c);auto m=mutate_code(c,r,0,.2,true);
    if(!m.valid)require(pack_code(c)==before,"invalid mutation not rolled back");
    require(c.base.language==frozen,"semantic rate zero changed dialect");(void)expand(c);
    require(pack_code(unpack_code(pack_code(c)))==pack_code(c),"mutated roundtrip");}
  c={};c.base=founder(r);for(int i=0;i<2000;++i){(void)mutate_code(c,r,0,1,false);require(c.blocks.empty(),"control gained a block");}
  // Read the four recorded specimens without bringing a JSON library into the VM.
  if(argc>1){std::ifstream f(argv[1]);std::string data((std::istreambuf_iterator<char>(f)),{});std::string key="\"genome\": \"";std::size_t pos=0;int count=0;
    while((pos=data.find(key,pos))!=std::string::npos){pos+=key.size();auto end=data.find('"',pos);auto specimen=unpack_code(data.substr(pos,end-pos));
      auto flat=expand(specimen).flat;auto task=count%2==0?6u:5u;require(verify(flat,task).pass,"recorded MIN/MAX proof failed");
      auto before=flat;require(factor(specimen,0,4),"recorded specimen factor failed");require(expand(specimen).flat.program==before.program,"recorded factor changed semantics");
      require(verify(expand(specimen).flat,task).pass,"factored specimen exhaustive failure");++count;pos=end;}
    require(count==4,"missing case-study specimens");
  }
  std::cout<<"PASS: factoring, nesting, skips, primitive immediate values, bounded expansion, pruning, mutation, serialization, and exhaustive recorded MAX/MIN checks\n";
  return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
