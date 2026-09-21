#pragma once
#include "core.hpp"
#include <functional>
#include <set>

namespace driftvm::modules {
// A representation experiment, not extra computing power. Expansion is bounded
// by the original VM's 96 instructions and preserves skips across call boundaries.
constexpr std::size_t MAX_MODULES=16, MAX_BODY=8, MAX_DEPTH=8;
struct OrganismCode {
  Genome base;
  std::vector<std::vector<uint8_t>> blocks;
};
struct Expansion {
  Genome flat;
  std::vector<unsigned> uses;
  unsigned depth=0;
  unsigned reused=0;
};
inline Expansion expand(const OrganismCode& c) {
  if(c.blocks.size()>MAX_MODULES || c.base.program.empty() || c.base.program.size()>MAX_LEN)
    throw std::runtime_error("invalid encoded program size");
  for(std::size_t i=0;i<c.blocks.size();++i){
    if(c.blocks[i].size()<2 || c.blocks[i].size()>MAX_BODY) throw std::runtime_error("invalid module body size");
    for(auto t:c.blocks[i]) if(t>=OPCODES+i) throw std::runtime_error("module cycle or forward reference");
  }
  for(auto& ops:c.base.language) for(auto op:ops) if(op>=OP_COUNT) throw std::runtime_error("invalid micro-op");
  Expansion e;e.flat.language=c.base.language;e.uses.resize(c.blocks.size());
  std::function<void(uint8_t,unsigned)> visit=[&](uint8_t token,unsigned depth){
    if(token<OPCODES){
      if(e.flat.program.size()>=MAX_LEN) throw std::runtime_error("expanded instruction budget exceeded");
      e.flat.program.push_back(token);return;
    }
    auto i=std::size_t(token-OPCODES);
    if(i>=c.blocks.size() || depth>=MAX_DEPTH) throw std::runtime_error("invalid module reference or depth");
    ++e.uses[i];e.depth=std::max(e.depth,depth+1);
    for(auto child:c.blocks[i]) visit(child,depth+1);
  };
  for(auto t:c.base.program)visit(t,0);
  if(e.flat.program.size()<MIN_LEN) throw std::runtime_error("expanded program below baseline minimum");
  for(auto n:e.uses) if(n>1)++e.reused;
  return e;
}
inline std::string bytes_hex(const std::vector<uint8_t>& v){return hex(std::string(v.begin(),v.end()));}
inline std::vector<uint8_t> hex_bytes(const std::string& s){auto b=unhex(s);return {b.begin(),b.end()};}
inline std::string pack_code(const OrganismCode& c){
  std::string s=pack(c.base);
  for(auto& b:c.blocks)s+="|"+bytes_hex(b);
  return s;
}
inline OrganismCode unpack_code(const std::string& s){
  OrganismCode c;auto end=s.find('|');auto base=s.substr(0,end);auto sep=base.find(':');
  if(sep==std::string::npos)throw std::runtime_error("missing dialect");
  c.base.program=hex_bytes(base.substr(0,sep));auto lang=hex_bytes(base.substr(sep+1));
  if(lang.size()!=OPCODES*SLOTS)throw std::runtime_error("invalid dialect length");
  for(std::size_t i=0;i<lang.size();++i)c.base.language[i/SLOTS][i%SLOTS]=lang[i];
  while(end!=std::string::npos){auto start=end+1;end=s.find('|',start);c.blocks.push_back(hex_bytes(s.substr(start,end-start)));}
  (void)expand(c);return c;
}
inline OrganismCode load_code(const std::string& file){
  std::ifstream in(file);std::string magic,data;
  if(!std::getline(in,magic)||!std::getline(in,data))throw std::runtime_error("cannot read genome: "+file);
  if(!magic.empty()&&magic.back()=='\r')magic.pop_back();
  if(!data.empty()&&data.back()=='\r')data.pop_back();
  if(magic!="DRIFTVM_MODULES_1"&&magic!="DRIFTVM_GENOME_1")throw std::runtime_error("unsupported genome version");
  return unpack_code(data);
}
inline void save_code(const OrganismCode& c,const std::string& file){
  std::ofstream o;o.exceptions(std::ios::failbit|std::ios::badbit);o.open(file);o<<"DRIFTVM_MODULES_1\n"<<pack_code(c)<<'\n';
}
// Factoring is exact, including SKIP_* and LOAD_IMM, because execution always
// traverses the expanded primitive stream, never a macro-as-one-instruction VM.
inline bool factor(OrganismCode& c,std::size_t pos,std::size_t length){
  if(c.blocks.size()>=MAX_MODULES||length<2||length>MAX_BODY||pos+length>c.base.program.size())return false;
  auto candidate=c;auto& p=candidate.base.program;
  candidate.blocks.emplace_back(p.begin()+std::ptrdiff_t(pos),p.begin()+std::ptrdiff_t(pos+length));
  p.erase(p.begin()+std::ptrdiff_t(pos),p.begin()+std::ptrdiff_t(pos+length));
  p.insert(p.begin()+std::ptrdiff_t(pos),uint8_t(OPCODES+c.blocks.size()));
  try{if(expand(candidate).flat.program!=expand(c).flat.program)return false;}catch(const std::exception&){return false;}
  c=std::move(candidate);return true;
}
// Remove unreachable definitions, retaining order and safely renumbering calls.
inline void prune(OrganismCode& c){
  std::vector<bool> used(c.blocks.size());
  std::function<void(uint8_t)> mark=[&](uint8_t t){if(t<OPCODES)return;auto i=t-OPCODES;
    if(i>=used.size())throw std::runtime_error("invalid module reference");
    if(used[i])return;
    used[i]=true;for(auto x:c.blocks[i])mark(x);};
  for(auto t:c.base.program)mark(t);
  std::vector<uint8_t> mapping(c.blocks.size());std::vector<std::vector<uint8_t>> keep;
  auto remap=[&](uint8_t t){return t<OPCODES?t:mapping[t-OPCODES];};
  for(std::size_t i=0;i<c.blocks.size();++i)if(used[i]){mapping[i]=uint8_t(OPCODES+keep.size());auto b=c.blocks[i];for(auto& t:b)t=remap(t);keep.push_back(b);}
  for(auto& t:c.base.program)t=remap(t);
  c.blocks=std::move(keep);
}
struct Change {bool valid=true,semantic=false,module=false,factored=false;std::string description;};
inline Change mutate_code(OrganismCode& c,Rng& r,double semantic_rate,double module_rate,bool enabled){
  Change m;auto before=c;
  // Mode draws are consumed even in the no-modules control. No claims of an
  // identical subsequent trajectory are made once populations diverge.
  auto coin=r.unit();auto pos=r.index(c.base.program.size());auto span=2+r.index(MAX_BODY-1);
  auto block_idx=r.index(std::max<std::size_t>(1,c.blocks.size()));auto body_pos=r.index(MAX_BODY);
  if(enabled && coin<module_rate/2 && !c.blocks.empty()){
    auto& body=c.blocks[block_idx];body_pos%=body.size();auto value=uint8_t(r.index(OPCODES+block_idx));
    m.module=body[body_pos]!=value;body[body_pos]=value;
    m.description="edit M"+std::to_string(block_idx)+" token "+std::to_string(body_pos);
  }else if(enabled && coin<module_rate){
    auto length=std::min(span,c.base.program.size()-pos);
    m.factored=factor(c,pos,length);m.module=m.factored;
    m.description=m.factored?"factor "+std::to_string(length)+" tokens at "+std::to_string(pos)+" into a block":"factor attempt: no valid block";
  }else{
    auto kind=r.index(4);auto n=OPCODES+(enabled?c.blocks.size():0);auto value=uint8_t(r.index(n));
    if(kind==0){c.base.program[pos]=value;m.description="replace token at "+std::to_string(pos);}
    else if(kind==1&&c.base.program.size()<MAX_LEN){c.base.program.insert(c.base.program.begin()+std::ptrdiff_t(pos),value);m.description="insert token at "+std::to_string(pos);}
    else if(kind==2&&c.base.program.size()>1){c.base.program.erase(c.base.program.begin()+std::ptrdiff_t(pos));m.description="delete token at "+std::to_string(pos);}
    else if(c.base.program.size()<MAX_LEN){auto t=c.base.program[pos];c.base.program.insert(c.base.program.begin()+std::ptrdiff_t(pos),t);m.description="duplicate token at "+std::to_string(pos);}
    else m.description="bounded no-op";
  }
  auto semcoin=r.unit();auto code=r.index(OPCODES),slot=r.index(SLOTS),value=r.index(OP_COUNT);
  if(semcoin<semantic_rate){m.semantic=c.base.language[code][slot]!=value;c.base.language[code][slot]=uint8_t(value);
    if(m.semantic)m.description+="; change meaning of opcode "+std::to_string(code);}
  try{(void)expand(c);prune(c);(void)expand(c);}catch(const std::exception&){m.valid=false;c=std::move(before);}
  return m;
}
inline std::string json_quote(const std::string& s){std::ostringstream o;o<<'"';
  for(unsigned char c:s){if(c=='"'||c=='\\')o<<'\\'<<char(c);else if(c<32)o<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<unsigned(c)<<std::dec;else o<<char(c);}o<<'"';return o.str();}
} // namespace driftvm::modules
