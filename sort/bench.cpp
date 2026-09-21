// One native harness for C++ and Rust. Every implementation gets identical input
// arrays, a batch-level ABI boundary, output checks, and randomized timing order.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif
using Row=std::array<uint32_t,8>;
static_assert(sizeof(Row)==8*sizeof(uint32_t));
using Batch=void(*)(Row*,std::size_t);
struct Variant{std::string name;Batch fn;};
#include "generated.hpp"
struct Library{
#ifdef _WIN32
  HMODULE h=nullptr;
  explicit Library(const std::string& file){if(!file.empty()){h=LoadLibraryW(std::filesystem::path(file).c_str());if(!h)throw std::runtime_error("Rust DLL load failed");}}
  Batch symbol(const std::string& name){auto p=GetProcAddress(h,name.c_str());if(!p)throw std::runtime_error("missing Rust symbol: "+name);return reinterpret_cast<Batch>(p);}
  ~Library(){if(h)FreeLibrary(h);}
#else
  void* h=nullptr;
  explicit Library(const std::string& file){if(!file.empty()){h=dlopen(file.c_str(),RTLD_NOW|RTLD_LOCAL);if(!h)throw std::runtime_error(dlerror());}}
  Batch symbol(const std::string& name){auto p=dlsym(h,name.c_str());if(!p)throw std::runtime_error("missing Rust symbol: "+name);return reinterpret_cast<Batch>(p);}
  ~Library(){if(h)dlclose(h);}
#endif
};
uint64_t number(const std::string& s){if(s.empty()||s.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("invalid unsigned integer");return std::stoull(s);}
uint32_t next32(uint64_t& x){x^=x<<13;x^=x>>7;x^=x<<17;return uint32_t(x>>16);}
std::vector<Row> data(std::size_t n,unsigned mode,uint64_t seed){std::vector<Row> rows(n);uint64_t r=seed?seed:1;
  for(auto& row:rows){for(auto& x:row)x=next32(r);if(mode==3)for(auto& x:row)x%=4;
    if(mode==1||mode==2||mode==4){std::sort(row.begin(),row.end());if(mode==2)std::reverse(row.begin(),row.end());
      if(mode==4){auto a=next32(r)%8,b=next32(r)%8;std::swap(row[a],row[b]);}}}return rows;}
uint64_t digest(const std::vector<Row>& rows){uint64_t h=0x123456789ULL;for(auto& row:rows)for(auto x:row){h^=x;h*=1099511628211ULL;}return h;}
int main(int argc,char** argv){try{
  std::string out,rust,only;uint64_t seed=1001;unsigned trials=15;std::size_t rows=32768;
  for(int i=1;i<argc;++i){std::string a=argv[i];auto value=[&](){if(++i>=argc)throw std::runtime_error("missing argument");return std::string(argv[i]);};
    if(a=="--out")out=value();else if(a=="--rustlib")rust=value();else if(a=="--only")only=value();
    else if(a=="--seed")seed=number(value());else if(a=="--trials")trials=unsigned(number(value()));else if(a=="--rows")rows=std::size_t(number(value()));
    else throw std::runtime_error("unknown argument: "+a);}
  if(out.empty()||std::filesystem::exists(out)||rows<256||rows>1048576||trials<1||trials>101)throw std::runtime_error("invalid benchmark options or existing output");
  auto variants=cpp_variants();Library lib(rust);
  if(!rust.empty())for(auto& name:rust_names())variants.push_back({name,lib.symbol(name)});
  if(!only.empty()){bool found=false;for(auto& v:variants)found|=v.name==only;if(!found)throw std::runtime_error("selected variant missing");
    variants.erase(std::remove_if(variants.begin(),variants.end(),[&](auto& v){return v.name.find("candidate_")!=std::string::npos&&v.name!=only;}),variants.end());}
  // Independent native code audit, including full binary inputs and UINT32_MAX.
  auto audit=data(4352,0,seed^0xabcdULL);for(unsigned bits=0;bits<256;++bits)for(unsigned w=0;w<8;++w)audit[bits][w]=(bits>>w)&1;
  audit[256]={0,UINT32_MAX,0,UINT32_MAX,1,2147483648u,2147483647u,2};
  auto expected=audit;for(auto& row:expected)std::sort(row.begin(),row.end());
  for(auto& v:variants){auto actual=audit;v.fn(actual.data(),actual.size());if(actual!=expected)throw std::runtime_error("native correctness failure: "+v.name);}
  std::ofstream f(out);f.exceptions(std::ios::failbit|std::ios::badbit);
  f<<"workload,trial,name,nanoseconds_per_sort,checksum,rows,seed\n"<<std::setprecision(12);
  const std::array<const char*,5> names={"random","sorted","reverse","duplicates","nearly_sorted"};
  std::mt19937_64 order_rng(seed^0x927abcdeULL);std::vector<std::size_t> order(variants.size());std::iota(order.begin(),order.end(),0);
  for(unsigned mode=0;mode<5;++mode){
    auto source=data(rows,mode,seed+uint64_t(mode)*0x100019ULL);auto truth=source;for(auto& row:truth)std::sort(row.begin(),row.end());
    std::vector<Row> work(rows);
    // Warm-up uses fresh copies too; no timing of repeatedly sorted work buffers.
    for(auto& v:variants){work=source;v.fn(work.data(),rows);}
    for(unsigned t=0;t<trials;++t){std::shuffle(order.begin(),order.end(),order_rng);
      for(auto i:order){auto& v=variants[i];work=source;
        const auto start=std::chrono::steady_clock::now();v.fn(work.data(),rows);const auto end=std::chrono::steady_clock::now();
        const double ns=std::chrono::duration<double,std::nano>(end-start).count()/double(rows);
        if(work!=truth)throw std::runtime_error("timed output failure: "+v.name);
        f<<names[mode]<<','<<t<<','<<v.name<<','<<ns<<','<<digest(work)<<','<<rows<<','<<seed<<'\n';}
    }f.flush();std::cout<<"benchmarked "<<names[mode]<<" variants="<<variants.size()<<" trials="<<trials<<'\n'<<std::flush;
  }
  std::ofstream meta(out+".compiler.txt");
#ifdef _MSC_VER
  meta<<"MSVC="<<_MSC_VER<<'\n';
#else
  meta<<"compiler="<<__VERSION__<<'\n';
#endif
#ifdef _GLIBCXX_RELEASE
  meta<<"libstdc++="<<_GLIBCXX_RELEASE<<'\n';
#endif
#ifdef _LIBCPP_VERSION
  meta<<"libc++="<<_LIBCPP_VERSION<<'\n';
#endif
  meta<<"sizeof_uint32="<<sizeof(uint32_t)<<"\nrows="<<rows<<"\ntrials="<<trials<<"\nmeasurement=batched-throughput-copy-and-validation-excluded\n";
  return 0;
}catch(const std::exception& e){std::cerr<<"benchmark error: "<<e.what()<<'\n';return 1;}}
