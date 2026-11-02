// SPDX-License-Identifier: MIT
#include "preset.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using Bytes=std::vector<uint8_t>;
static int checks=0;
static void check(bool b,const char*label){++checks;if(!b)throw std::runtime_error(label);}
static void number(Bytes&v,uint32_t n){for(int i=0;i<4;++i)v.push_back(uint8_t(n>>(8*i)));}
static void event(Bytes&v,uint8_t id,const Bytes&b){v.push_back(id);size_t n=b.size();do{uint8_t x=n&127;n>>=7;v.push_back(x|(n?128:0));}while(n);v.insert(v.end(),b.begin(),b.end());}
static Bytes ascii(const std::string&s){Bytes b(s.begin(),s.end());b.push_back(0);return b;}
static Bytes utf16(const std::string&s){Bytes b;for(char c:s){b.push_back(c);b.push_back(0);}b.push_back(0);b.push_back(0);return b;}
static Bytes wrap(const Bytes&events){Bytes b{'F','L','h','d'};number(b,6);b.insert(b.end(),{0x30,0,1,0,96,0,'F','L','d','t'});number(b,uint32_t(events.size()));b.insert(b.end(),events.begin(),events.end());return b;}
static Bytes events(const Bytes&name){Bytes b;event(b,199,ascii("8.0.2"));event(b,201,name);event(b,212,Bytes(52,0));event(b,213,Bytes{4,0,0,0,1,2,3});return b;}
static bool parse(const Bytes&b,const std::string&name="Gross Beat",Bytes*out=nullptr){Bytes native{99};std::string error="stale";bool ok=FST::parseBytes(b.data(),b.size(),name,native,error);check(ok?error.empty():!error.empty(),"error contract");if(!ok)check(native.empty(),"failed parser leaked state");if(out)*out=native;return ok;}
int main(int argc,char**argv){try{
 Bytes native;auto good=wrap(events(ascii("Gross Beat")));check(parse(good,"Gross Beat",&native)&&native==Bytes({4,0,0,0,1,2,3}),"native payload differs");
 check(parse(wrap(events(utf16("Gross Beat")))),"UTF16 identity");check(parse(wrap(events(Bytes{0xc9,0,'c',0,'h',0,'o',0,0,0})),"\xc3\x89" "cho"),"non-ASCII UTF16 identity");check(parse(wrap(events(Bytes{0x3d,0xd8,0x0a,0xde,0,0})),"\xf0\x9f\x98\x8a"),"UTF16 surrogate pair");check(parse(good,"gross beat"),"case-insensitive identity");check(!parse(good,"Sytrus"),"mismatched plugin");check(!parse(good,""),"empty expected identity");
 auto legacy=good;legacy[8]=0x20;check(parse(legacy),"legacy preset type");
 for(size_t n=0;n<good.size();++n)check(!parse(Bytes(good.begin(),good.begin()+n)),"truncated container accepted");
 for(size_t at:{size_t(0),size_t(4),size_t(8),size_t(9),size_t(14),size_t(18)}){auto b=good;b[at]^=0x80;check(!parse(b),"bad header accepted");}
 auto e=events(ascii("Gross Beat"));event(e,201,ascii("Gross Beat"));check(!parse(wrap(e)),"duplicate identity");e=events(ascii("Gross Beat"));event(e,213,Bytes{1});check(!parse(wrap(e)),"duplicate native state");
 e.clear();event(e,213,Bytes{1});check(!parse(wrap(e)),"missing identity");e.clear();event(e,201,ascii("Gross Beat"));check(!parse(wrap(e)),"missing state");event(e,213,{});check(!parse(wrap(e)),"empty state");
 check(!parse(wrap(events(Bytes{'G',0,'x',0}))),"embedded nul identity");check(!parse(wrap(events(Bytes{'G',0,0,0xd8,0,0}))),"unpaired UTF16 surrogate");check(!parse(wrap(events(Bytes{0xc0,0xaf,0}))),"invalid UTF8 identity");check(!parse(wrap(events(Bytes{'G','r','o','s','s'}))),"unterminated identity");
 for(const Bytes&suffix:{Bytes{213,0x80},Bytes{213,0xff,0xff,0xff,0xff,0x1f},Bytes{213,0x81,0},Bytes{213,0xff,0xff,0xff,0xff,0x0f}}){e=events(ascii("Gross Beat"));e.insert(e.end(),suffix.begin(),suffix.end());check(!parse(wrap(e)),"bad varint/payload");}
 e=events(ascii("Gross Beat"));e.insert(e.end(),{0,17,64,1,2,128,1,2,3,4});check(parse(wrap(e)),"valid unknown fixed events");
 e=events(ascii("Gross Beat"));for(size_t i=0;i<FST::MaxEvents;++i)e.insert(e.end(),{0,0});check(!parse(wrap(e)),"event cap");
 std::string error;check(!FST::parseBytes(good.data(),FST::MaxFileSize+1,"Gross Beat",native,error),"file cap");check(!FST::parseFst(nullptr,"Gross Beat",native,error)&&native.empty(),"null path");check(!FST::parseFst(L"missing-preset-xyz.fst","Gross Beat",native,error),"missing file");
 if(argc>1){size_t count=0;for(const auto&f:std::filesystem::directory_iterator(argv[1]))if(f.path().extension()==".fst"){
  check(FST::parseFst(f.path().wstring().c_str(),"Gross Beat",native,error),"real factory preset failed");check(native.size()>6000,"real native state too small");check(!FST::parseFst(f.path().wstring().c_str(),"Sytrus",native,error),"real wrong plugin accepted");++count;}
  check(count==9,"expected nine Gross Beat factory presets");std::cout<<"9 real factory presets accepted; mismatched target rejected\n";}
 std::cout<<"PASS "<<checks<<" parser checks\n";return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
