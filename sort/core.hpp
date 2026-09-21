#pragma once
// DriftSort: a bounded, task-specific min/max/copy language, not native assembly.
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace ds {
constexpr unsigned W=8, REF=256, MAX_BLOCKS=16, LIMIT=64;
using Row=std::array<uint32_t,W>;
using Seq=std::vector<unsigned>;
struct Genome { Seq program; std::vector<Seq> blocks; };
struct Rng {
  std::mt19937_64 engine;
  explicit Rng(uint64_t s):engine(s){}
  std::size_t index(std::size_t n) {
    if(!n) throw std::runtime_error("empty random range");
    const uint64_t b=n, t=(uint64_t(0)-b)%b; uint64_t x;
    do{x=engine();}while(x<t);return std::size_t(x%b);
  }
};
// Operation 0 exchanges min/max; 1 writes min; 2 writes max; 3 copies source.
inline unsigned op(unsigned kind,unsigned a,unsigned b){return kind*64+a*8+b;}
inline void step(Row& r,unsigned v){
  if(v>=REF)throw std::runtime_error("unexpanded operation");
  auto a=(v/8)%8,b=v%8; auto x=r[a],y=r[b];
  switch(v/64){case 0:r[a]=std::min(x,y);r[b]=std::max(x,y);break;
    case 1:r[a]=std::min(x,y);break;case 2:r[a]=std::max(x,y);break;case 3:r[a]=y;break;}
}
inline Seq flatten(const Genome& g) {
  if(g.program.empty()||g.program.size()>LIMIT||g.blocks.size()>MAX_BLOCKS)
    throw std::runtime_error("genome size limit");
  for(std::size_t i=0;i<g.blocks.size();++i){
    if(g.blocks[i].size()<2||g.blocks[i].size()>8)throw std::runtime_error("block size limit");
    for(auto v:g.blocks[i])if(v>=REF+i)throw std::runtime_error("cyclic or forward block reference");
  }
  Seq flat;
  std::function<void(const Seq&,unsigned)> walk=[&](const Seq& s,unsigned depth){
    if(depth>8)throw std::runtime_error("nesting limit");
    for(auto v:s){if(v<REF){flat.push_back(v);if(flat.size()>LIMIT)throw std::runtime_error("expanded limit");}
      else{if(v-REF>=g.blocks.size())throw std::runtime_error("missing block");walk(g.blocks[v-REF],depth+1);}}
  };walk(g.program,0);return flat;
}
inline void prune(Genome& g){
  std::vector<bool> used(g.blocks.size());
  std::function<void(const Seq&)> walk=[&](const Seq& s){for(auto v:s)if(v>=REF){
    auto b=v-REF;if(b>=g.blocks.size())throw std::runtime_error("missing block");
    if(!used[b]){used[b]=true;walk(g.blocks[b]);}}};walk(g.program);
  std::vector<unsigned> remap(used.size()); std::vector<Seq> out;
  for(std::size_t i=0;i<used.size();++i)if(used[i]){remap[i]=unsigned(out.size());out.push_back(g.blocks[i]);}
  auto fix=[&](Seq& s){for(auto& v:s)if(v>=REF)v=REF+remap[v-REF];};
  fix(g.program);for(auto& s:out)fix(s);g.blocks=std::move(out);
}
inline std::string pack(const Genome& g){
  std::ostringstream o;o<<g.program.size();for(auto x:g.program)o<<' '<<x;o<<' '<<g.blocks.size();
  for(auto& s:g.blocks){o<<' '<<s.size();for(auto x:s)o<<' '<<x;}return o.str();
}
inline Genome unpack(const std::string& text){
  std::istringstream s(text);Genome g;
  auto read=[&](){std::string token;if(!(s>>token)||token.empty()||token.find_first_not_of("0123456789")!=std::string::npos)
      throw std::runtime_error("invalid genome integer");
    auto v=std::stoull(token);if(v>4096)throw std::runtime_error("genome integer limit");return unsigned(v);};
  auto n=read();if(!n||n>LIMIT)throw std::runtime_error("program length");
  while(n--){g.program.push_back(read());}
  n=read();if(n>MAX_BLOCKS)throw std::runtime_error("block count");
  while(n--){auto k=read();if(k<2||k>8)throw std::runtime_error("block length");Seq block;while(k--)block.push_back(read());g.blocks.push_back(block);}
  std::string extra;if(s>>extra)throw std::runtime_error("trailing genome data");flatten(g);return g;
}
inline std::string flat_key(const Seq& f){std::string s;for(auto v:f)s+=char(v);return s;}
inline Row execute(const Seq& f,Row row){for(auto v:f)step(row,v);return row;}
using Bits=std::array<uint64_t,4>;
struct Evaluation {unsigned errors=0,cost=0,depth=0;uint64_t signature=0;};
// All 256 binary inputs simultaneously. Output is compared to the *whole sorted
// vector*, not merely sortedness: MIN/MAX/COPY could otherwise lose multiplicity.
inline Evaluation evaluate(const Seq& flat){
  static const auto input=[](){std::array<Bits,8> t{};for(unsigned x=0;x<256;++x){for(unsigned w=0;w<8;++w){
    if((x>>w)&1)t[w][x/64]|=uint64_t(1)<<(x%64);}}return t;}();
  static const auto expected=[](){std::array<Bits,8> t{};for(unsigned x=0;x<256;++x){for(unsigned w=0;w<8;++w){
    if(w>=8-unsigned(std::popcount(x)))t[w][x/64]|=uint64_t(1)<<(x%64);}}return t;}();
  auto rows=input;std::array<unsigned,8> depths{};Evaluation e;
  for(auto v:flat){auto a=(v/8)%8,b=v%8,k=v/64;auto d=1+std::max(depths[a],depths[b]);
    e.cost+=k==0?2:1;depths[a]=d;if(k==0)depths[b]=d;
    for(unsigned z=0;z<4;++z){auto x=rows[a][z],y=rows[b][z];
      if(k==0){rows[a][z]=x&y;rows[b][z]=x|y;}else if(k==1)rows[a][z]=x&y;
      else if(k==2)rows[a][z]=x|y;else rows[a][z]=y;}}
  e.signature=14695981039346656037ULL;
  for(unsigned w=0;w<8;++w)for(unsigned z=0;z<4;++z){e.errors+=unsigned(std::popcount(rows[w][z]^expected[w][z]));
    e.signature^=rows[w][z];e.signature*=1099511628211ULL;}
  e.depth=*std::max_element(depths.begin(),depths.end());return e;
}
inline Genome insertion_seed(){Genome g;for(unsigned i=1;i<8;++i)for(unsigned j=i;j>0;--j)g.program.push_back(op(0,j-1,j));return g;}
// Independently generated Batcher odd-even merge reference; 19 exchanges on 8 wires.
inline Genome network19(){Genome g;
  std::function<void(unsigned,unsigned,unsigned)> merge=[&](unsigned lo,unsigned n,unsigned r){auto m=r*2;
    if(m<n){merge(lo,n,m);merge(lo+r,n,m);for(auto i=lo+r;i+r<lo+n;i+=m)g.program.push_back(op(0,i,i+r));}
    else g.program.push_back(op(0,lo,lo+r));};
  std::function<void(unsigned,unsigned)> sort=[&](unsigned lo,unsigned n){if(n>1){sort(lo,n/2);sort(lo+n/2,n/2);merge(lo,n,1);}};
  sort(0,8);return g;
}
inline unsigned random_primitive(Rng& r){auto a=unsigned(r.index(8)),b=unsigned(r.index(7));if(b>=a)++b;
  auto k=r.index(8);return op(k<5?0:unsigned(k-4),a,b);}
inline Genome random_genome(Rng& r){Genome g;for(unsigned i=0,n=16+unsigned(r.index(25));i<n;++i)g.program.push_back(random_primitive(r));return g;}
inline bool mutate(Genome& g,Rng& r,bool modules,std::string& description){
  auto mode=r.index(100);description="program";
  if(modules&&mode<4&&g.blocks.size()<MAX_BLOCKS&&g.program.size()>=2){
    auto n=2+r.index(std::min<std::size_t>(6,g.program.size())-1),p=r.index(g.program.size()-n+1);
    Seq b(g.program.begin()+std::ptrdiff_t(p),g.program.begin()+std::ptrdiff_t(p+n));auto token=REF+unsigned(g.blocks.size());g.blocks.push_back(b);
    g.program.erase(g.program.begin()+std::ptrdiff_t(p),g.program.begin()+std::ptrdiff_t(p+n));g.program.insert(g.program.begin()+std::ptrdiff_t(p),token);description="factor";
  }else if(modules&&mode<9&&!g.blocks.empty()){
    auto bi=r.index(g.blocks.size());auto& b=g.blocks[bi];
    b[r.index(b.size())]=(bi&&r.index(4)==0)?REF+unsigned(r.index(bi)):random_primitive(r);description="dialect_body";
  }else{
    auto token=[&](){return (modules&&!g.blocks.empty()&&r.index(4)==0)?REF+unsigned(r.index(g.blocks.size())):random_primitive(r);};
    auto kind=r.index(5),p=r.index(g.program.size());
    if(kind==0&&g.program.size()>1){g.program.erase(g.program.begin()+std::ptrdiff_t(p));description="delete";}
    else if(kind==1&&g.program.size()<LIMIT){g.program.insert(g.program.begin()+std::ptrdiff_t(p),token());description="insert";}
    else if(kind==2){auto q=r.index(g.program.size());std::swap(g.program[p],g.program[q]);description="reorder";}
    else if(kind==3&&g.program.size()<LIMIT){auto v=g.program[p];g.program.insert(g.program.begin()+std::ptrdiff_t(p),v);description="duplicate";}
    else g.program[p]=token();
  }
  try{flatten(g);prune(g);return true;}catch(const std::runtime_error&){return false;}
}
inline uint64_t integer(const std::string& s) {
  if(s.empty()||s.find_first_not_of("0123456789")!=std::string::npos) {
    throw std::runtime_error("expected nonnegative integer");
  }
  return std::stoull(s);
}
} // namespace ds
