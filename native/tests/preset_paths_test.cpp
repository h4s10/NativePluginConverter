// SPDX-License-Identifier: MIT
#include "../bridge/preset_paths.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using Bytes=std::vector<uint8_t>;
static int checks=0;
static void check(bool b,const char*s){checks++;if(!b)throw std::runtime_error(s);}
static Bytes state(const std::string&p,uint32_t v=14){Bytes s(21,0);for(int i=0;i<4;i++)s[i]=v>>(i*8);s[20]=1;size_t n=p.size();do{uint8_t b=n&127;n>>=7;s.push_back(b|(n?128:0));}while(n);s.insert(s.end(),p.begin(),p.end());s.insert(s.end(),{7,6,5,4});return s;}
int wmain(int argc,wchar_t**argv){try{if(argc<2)return 2;std::filesystem::path temp=argv[1];std::filesystem::create_directories(temp/"Data/Patches/Impulses/Test");std::ofstream(temp/"Data/Patches/Impulses/Test/a.wv")<<"a";std::string error,text;size_t end=0;
 auto good=state("%FLStudioData%\\Patches\\Impulses\\Test\\a.wv");
 for(uint32_t v:{11u,14u,0xe0003u,0xf0003u}){auto b=state("%FLStudioData%\\Patches\\Impulses\\Test\\a.wv",v);check(PresetPaths::relocateConvolver(b,temp.wstring(),error),"known layout");check(b[b.size()-4]==7&&b.back()==4,"tail preserved");check(PresetPaths::convolverPath(b,end,text,error),"rewritten parse");check(text.find("%FLStudioData%")==std::string::npos,"unexpanded path");}
 auto invalid=state("%FLStudioData%\\Patches\\Impulses\\Test\\a.wv",13);check(!PresetPaths::relocateConvolver(invalid,temp.wstring(),error),"unknown version accepted");
 for(auto p:{"%FLStudioData%\\..\\secret.wv","%FLStudioFactoryData%\\..\\secret.wv","%USERPROFILE%\\..\\a.wv","%FLStudioData%\\C:\\secret.wv","%FLStudioData%\\Patches\\Impulses\\missing.wv","%OTHER%\\a.wv"}){auto b=state(p);check(!PresetPaths::relocateConvolver(b,temp.wstring(),error),"unsafe or missing path accepted");}
 for(size_t n=0;n<good.size()-4;n++){auto b=Bytes(good.begin(),good.begin()+n);check(!PresetPaths::convolverPath(b,end,text,error),"truncated path accepted");}
 for(auto suffix:{Bytes{0x80},Bytes{0x81,0},Bytes{0xff,0xff,0xff,0xff,0x1f}}){Bytes b(good.begin(),good.begin()+21);b.insert(b.end(),suffix.begin(),suffix.end());check(!PresetPaths::convolverPath(b,end,text,error),"invalid length accepted");}
 auto legacy=state("%USERPROFILE%\\Dropbox\\Old\\a.wv");check(PresetPaths::relocateConvolver(legacy,temp.wstring(),error),"unique legacy failed");std::filesystem::create_directories(temp/"Data/Patches/Impulses/Other");std::ofstream(temp/"Data/Patches/Impulses/Other/a.wv")<<"other";legacy=state("%USERPROFILE%\\Dropbox\\Old\\a.wv");check(!PresetPaths::relocateConvolver(legacy,temp.wstring(),error),"ambiguous legacy accepted");
 if(argc>2){std::filesystem::path source=argv[2];size_t count=0;for(auto&f:std::filesystem::recursive_directory_iterator(source/"Data/Patches/Plugin presets/Effects/Fruity Convolver")){if(f.path().extension()!=L".fst")continue;Bytes native;check(FST::parseFst(f.path().c_str(),"Fruity Convolver",native,error),"factory FST parse");if(!PresetPaths::relocateConvolver(native,source.wstring(),error)){std::cerr<<error<<"\n";throw std::runtime_error("factory path unresolved");}count++;}check(count==261,"factory count");std::cout<<"Factory paths resolved: "<<count<<"\n";}
 std::cout<<"PASS "<<checks<<" schema, bounds, traversal, missing and ambiguous path checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
