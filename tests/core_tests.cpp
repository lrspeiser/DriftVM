#include "../src/core.hpp"
#include <iostream>
using namespace driftvm;
void require(bool x,const char* msg){if(!x)throw std::runtime_error(msg);}
Genome sequence(std::vector<uint8_t> ops){
  Genome g;while(ops.size()%SLOTS)ops.push_back(NOP);
  for(std::size_t i=0;i<ops.size()/SLOTS;++i){g.program.push_back(uint8_t(i));for(std::size_t j=0;j<SLOTS;++j)g.language[i][j]=ops[i*SLOTS+j];}
  while(g.program.size()<MIN_LEN)g.program.push_back(31);
  return g;
}
Genome rotate(){Genome g;g.program={0,1,2,3};
  g.language[0]={LOAD_A,SHR1,SHR1,SHR1};g.language[1]={SHR1,SHR1,SHR1,SHR1};
  g.language[2]={MOV,LOAD_A,SHL1,OR};g.language[3]={MOV,LOAD_B,XOR,EMIT};return g;}
int main(){try{
  unsigned checks=0;
  for(std::size_t t=0;t<10;++t){Genome g;
    if(t==0)g=sequence({LOAD_A,MOV,LOAD_B,XOR,EMIT});
    if(t==1)g=sequence({LOAD_A,MOV,LOAD_B,ADD,EMIT});
    if(t==2)g=sequence({LOAD_B,MOV,LOAD_A,SUB,EMIT});
    if(t==3)g=sequence({LOAD_A,MOV,LOAD_B,AND,EMIT});
    if(t==4)g=sequence({LOAD_A,MOV,LOAD_B,OR,EMIT});
    if(t==5||t==6){g.program={0,1,2,3,4,5};g.language[0]={LOAD_A,MOV,LOAD_B,CMP_LT};
      g.language[1]={uint8_t(t==5?SKIP_IF_ZERO:SKIP_IF_NONZERO),NOP,NOP,NOP};
      g.language[2]={LOAD_A,EMIT,NOP,NOP};g.language[3]=g.language[0];
      g.language[4]={uint8_t(t==5?SKIP_IF_NONZERO:SKIP_IF_ZERO),NOP,NOP,NOP};g.language[5]={LOAD_B,EMIT,NOP,NOP};}
    if(t==7)g=sequence({LOAD_A,MOV,LOAD_B,CMP_EQ,EMIT});
    if(t==8){std::vector<uint8_t> ops={LOAD_A,MOV,LOAD_B,XOR,MOV,SHR1,XOR,MOV,SHR1,SHR1,XOR,MOV,SHR1,SHR1,SHR1,SHR1,XOR};
      for(int i=0;i<7;++i)ops.push_back(SHL1);
      for(int i=0;i<7;++i)ops.push_back(SHR1);
      ops.push_back(EMIT);g=sequence(ops);}
    if(t==9)g=rotate();
    auto v=verify(g,t);require(v.pass&&v.checked==65536,"positive control failed exhaustive verification");
    auto measured=evaluate(g,probes(1,32),probes(2,128));require((measured.candidates&(1u<<t))!=0,"screen rejected correct control");
    require(pack(unpack(pack(g)))==pack(g),"genome roundtrip");++checks;
  }
  auto zero=sequence({NOP});auto no=verify(zero,9);require(!no.pass&&no.checked<65536,"false solution passed");++checks;
  // Independent parity oracle: count bits instead of using the target's XOR folding.
  for(unsigned a=0;a<256;++a)for(unsigned b=0;b<256;++b){unsigned ones=0,x=a^b;
    for(unsigned i=0;i<8;++i)ones+=(x>>i)&1;
    require(target(8,uint8_t(a),uint8_t(b))==(ones%2),"parity oracle");}
  ++checks;
  Counts live{};live[0]=200;double crowded=ecological_score(1,live);
  require(crowded==ecological_score(1,live),"identical incumbents and children differ");
  live[0]=0;require(ecological_score(1,live)>crowded,"reward does not recover after extinction");
  require(quality(1)==8&&ecological_score(0,live)==0,"novelty contaminates capability");++checks;
  Rng r(12);auto g=founder(r);auto original_language=g.language;
  for(int i=0;i<10000;++i){mutate(g,r,0);require(g.language==original_language,"fixed-language ablation mutated semantics");
    require(g.program.size()>=MIN_LEN&&g.program.size()<=MAX_LEN,"mutation length bound");}
  ++checks;
  for(int i=0;i<1000;++i){mutate(g,r,1);require(pack(unpack(pack(g)))==pack(g),"mutant serialization");
    (void)execute(g,{uint8_t(i),uint8_t(i>>2)});}++checks;
  bool threw=false;try{unpack("ff:00");}catch(const std::exception&){threw=true;}require(threw,"corrupt genome accepted");++checks;
  // The VM intentionally has implicit final output, and no branch can run backwards.
  auto implicit=sequence({LOAD_A});require(execute(implicit,{73,0})==73,"implicit output changed");++checks;
  std::cout<<"PASS "<<checks<<" core groups; 10 exhaustive positive controls; negative, mutation, scoring, serialization tests\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
