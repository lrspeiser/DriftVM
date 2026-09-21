#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace driftvm {
constexpr std::size_t OPCODES=32, SLOTS=4, MIN_LEN=4, MAX_LEN=96;
enum Op : uint8_t { NOP, LOAD_A, LOAD_B, LOAD_IMM, MOV, ADD, SUB, XOR, AND, OR,
  SHL1, SHR1, INC, DEC, LOAD_MEM, STORE_MEM, CMP_EQ, CMP_LT,
  SKIP_IF_ZERO, SKIP_IF_NONZERO, EMIT, OP_COUNT };
constexpr std::array<const char*, OP_COUNT> OP_NAMES={"NOP","LOAD_A","LOAD_B","LOAD_IMM",
  "MOV","ADD","SUB","XOR","AND","OR","SHL1","SHR1","INC","DEC","LOAD_MEM",
  "STORE_MEM","CMP_EQ","CMP_LT","SKIP_IF_ZERO","SKIP_IF_NONZERO","EMIT"};
constexpr std::array<const char*,10> TASKS={"XOR","ADD","SUB","AND","OR","MAX","MIN","EQ","PARITY","ROTATE_XOR"};
constexpr std::array<double,10> BASE={8,8,8,7,7,9,9,11,13,15};
using Mask=uint16_t;
using Counts=std::array<uint64_t,10>;
struct Genome { std::vector<uint8_t> program; std::array<std::array<uint8_t,SLOTS>,OPCODES> language{}; };
struct Probe { uint8_t a,b; };
struct Rng {
  std::mt19937_64 gen;
  explicit Rng(uint64_t seed):gen(seed){}
  // Explicit sampling, rather than implementation-dependent standard distributions.
  std::size_t index(std::size_t n) {
    if(!n) throw std::runtime_error("empty random range");
    const auto bound=static_cast<uint64_t>(n), threshold=(uint64_t(0)-bound)%bound;
    uint64_t x; do{x=gen();}while(x<threshold); return static_cast<std::size_t>(x%bound);
  }
  double unit(){return static_cast<double>(gen()>>11)*0x1.0p-53;}
};
inline uint8_t target(std::size_t task,uint8_t a,uint8_t b) {
  switch(task){
    case 0:return a^b; case 1:return static_cast<uint8_t>(a+b);
    case 2:return static_cast<uint8_t>(a-b); case 3:return a&b; case 4:return a|b;
    case 5:return std::max(a,b); case 6:return std::min(a,b); case 7:return a==b?1:0;
    case 8:{unsigned x=unsigned(a^b); x^=x>>4; x^=x>>2; x^=x>>1; return uint8_t(x&1);}
    case 9:return uint8_t(uint8_t((unsigned(a)<<1)|(a>>7))^b);
    default:throw std::runtime_error("invalid task");
  }
}
inline uint8_t execute(const Genome& g,Probe p) {
  uint8_t x=0,y=0,last=0; bool emitted=false; std::array<uint8_t,32> mem{};
  // Same forward-only, bounded bytecode semantics as Cambrian-0.
  for(std::size_t pc=0;pc<g.program.size();){
    auto code=g.program[pc]; bool skip=false;
    for(auto op:g.language[code]) switch(op){
      case NOP:break; case LOAD_A:x=p.a;break; case LOAD_B:x=p.b;break;
      case LOAD_IMM:x=code;break; case MOV:y=x;break;
      case ADD:x=uint8_t(x+y);break; case SUB:x=uint8_t(x-y);break;
      case XOR:x^=y;break; case AND:x&=y;break; case OR:x|=y;break;
      case SHL1:x=uint8_t(x<<1);break; case SHR1:x=uint8_t(x>>1);break;
      case INC:++x;break; case DEC:--x;break;
      case LOAD_MEM:x=mem[y%32];break; case STORE_MEM:mem[y%32]=x;break;
      case CMP_EQ:x=x==y?1:0;break; case CMP_LT:x=x<y?1:0;break;
      case SKIP_IF_ZERO:if(x==0)skip=true;break;
      case SKIP_IF_NONZERO:if(x!=0)skip=true;break;
      case EMIT:last=x;emitted=true;break;
      default:throw std::runtime_error("invalid micro-op");
    }
    pc+=skip?2:1;
  }
  return emitted?last:x; // Preserve Cambrian-0's implicit final-register output.
}
inline std::vector<Probe> probes(uint64_t seed,std::size_t n) {
  Rng r(seed); std::vector<Probe> ps; ps.reserve(n);
  for(std::size_t i=0;i<n;++i) ps.push_back({uint8_t(r.index(256)),uint8_t(r.index(256))});
  if(n>=8){ps[0]={0,0};ps[1]={0,255};ps[2]={255,0};ps[3]={255,255};
    ps[4]={127,128};ps[5]={128,127};ps[6]={85,85};ps[7]={170,170};}
  return ps;
}
struct Behavior {std::string signature; Mask candidates=0;};
inline Behavior evaluate(const Genome& g,const std::vector<Probe>& ps,const std::vector<Probe>& screen) {
  Behavior b; b.candidates=1023;
  for(auto p:ps){auto out=execute(g,p);b.signature.push_back(char(out));
    for(std::size_t t=0;t<TASKS.size();++t)
      if(out!=target(t,p.a,p.b))b.candidates&=Mask(~(1u<<t));}
  // Fixed independent screening inputs. These improve selection, not certify discoveries.
  for(auto p:screen){if(!b.candidates)break;auto out=execute(g,p);
    for(std::size_t t=0;t<TASKS.size();++t)
      if((b.candidates&(1u<<t))&&out!=target(t,p.a,p.b))b.candidates&=Mask(~(1u<<t));}
  return b;
}
inline double quality(Mask mask){double q=0;for(std::size_t t=0;t<10;++t)if(mask&(1u<<t))q+=BASE[t];return q;}
inline double ecological_score(Mask mask,const Counts& live){
  double q=0;for(std::size_t t=0;t<10;++t)if(mask&(1u<<t))q+=BASE[t]/(1.0+double(live[t]));return q;
}
struct Verification {bool pass;uint32_t checked;unsigned a,b,actual,expected;};
inline Verification verify(const Genome& g,std::size_t t){
  uint32_t checked=0;for(unsigned a=0;a<256;++a)for(unsigned b=0;b<256;++b){
    auto out=execute(g,{uint8_t(a),uint8_t(b)}), expected=target(t,uint8_t(a),uint8_t(b));++checked;
    if(out!=expected)return {false,checked,a,b,out,expected};}
  return {true,checked,0,0,0,0};
}
inline std::string hex(const std::string& bytes){
  static const char* digits="0123456789abcdef";std::string out;out.reserve(bytes.size()*2);
  for(unsigned char b:bytes){out+=digits[b>>4];out+=digits[b&15];}return out;
}
inline std::string unhex(const std::string& s){
  if(s.size()%2)throw std::runtime_error("odd hex length");
  std::string out;
  auto digit=[](char c)->unsigned{if(c>='0'&&c<='9')return unsigned(c-'0');
    if(c>='a'&&c<='f')return unsigned(c-'a'+10);
    throw std::runtime_error("invalid hex");};
  for(std::size_t i=0;i<s.size();i+=2)out+=char(digit(s[i])*16+digit(s[i+1]));
  return out;
}
inline std::string pack(const Genome& g){
  std::string p(g.program.begin(),g.program.end()),l;
  for(const auto& sem:g.language)for(auto op:sem)l+=char(op);
  return hex(p)+":"+hex(l);
}
inline Genome unpack(const std::string& s){
  auto colon=s.find(':');if(colon==std::string::npos)throw std::runtime_error("missing genome separator");
  auto p=unhex(s.substr(0,colon)),l=unhex(s.substr(colon+1));
  if(p.size()<MIN_LEN||p.size()>MAX_LEN||l.size()!=OPCODES*SLOTS)throw std::runtime_error("invalid genome length");
  Genome g;for(unsigned char c:p){if(c>=OPCODES)throw std::runtime_error("invalid opcode");g.program.push_back(c);}
  for(std::size_t i=0;i<l.size();++i){auto c=uint8_t(l[i]);if(c>=OP_COUNT)throw std::runtime_error("invalid micro-op");g.language[i/SLOTS][i%SLOTS]=c;}return g;
}
inline Genome founder(Rng& r){
  Genome g;g.program.resize(24);for(auto& x:g.program)x=uint8_t(r.index(OPCODES));
  for(auto& sem:g.language)for(auto& op:sem)op=uint8_t(r.index(OP_COUNT));
  return g;
}
struct Mutation {std::string delta;bool semantic=false;};
inline Mutation mutate(Genome& g,Rng& r,double semantic_rate){
  Mutation m;std::ostringstream d;
  const auto edits=1+r.index(3);
  for(std::size_t e=0;e<edits;++e){auto kind=r.index(3);
    if(kind==1&&g.program.size()<MAX_LEN){auto pos=r.index(g.program.size()+1),v=r.index(OPCODES);
      d<<"I "<<pos<<' '<<v<<';';g.program.insert(g.program.begin()+std::ptrdiff_t(pos),uint8_t(v));}
    else if(kind==2&&g.program.size()>MIN_LEN){auto pos=r.index(g.program.size());
      d<<"D "<<pos<<' '<<unsigned(g.program[pos])<<';';g.program.erase(g.program.begin()+std::ptrdiff_t(pos));}
    else{auto pos=r.index(g.program.size()),v=r.index(OPCODES);
      d<<"P "<<pos<<' '<<unsigned(g.program[pos])<<' '<<v<<';';g.program[pos]=uint8_t(v);}}
  // Consume draws even when semantic mutations are disabled, to ease paired-seed comparisons.
  auto coin=r.unit();auto code=r.index(OPCODES),slot=r.index(SLOTS),v=r.index(OP_COUNT);
  if(coin<semantic_rate){m.semantic=g.language[code][slot]!=v;
    d<<"S "<<code<<' '<<slot<<' '<<unsigned(g.language[code][slot])<<' '<<v<<';';g.language[code][slot]=uint8_t(v);}
  m.delta=d.str();return m;
}
inline void save_genome(const Genome& g,const std::string& file){
  std::ofstream out;out.exceptions(std::ios::failbit|std::ios::badbit);out.open(file);
  out<<"DRIFTVM_GENOME_1\n"<<pack(g)<<'\n';
}
inline Genome load_genome(const std::string& file){
  std::ifstream in(file);std::string magic,data;if(!std::getline(in,magic)||magic!="DRIFTVM_GENOME_1"||!std::getline(in,data))
    throw std::runtime_error("invalid genome file: "+file);
  return unpack(data);
}
inline void disassemble(const Genome& g,std::ostream& out){
  out<<"program:";for(auto x:g.program)out<<' '<<unsigned(x);out<<'\n';
  for(std::size_t i=0;i<OPCODES;++i){out<<"opcode "<<i<<':';for(auto x:g.language[i])out<<' '<<OP_NAMES[x];out<<'\n';}
}
} // namespace driftvm
