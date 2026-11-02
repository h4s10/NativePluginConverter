// SPDX-License-Identifier: MIT
#include "../bridge/config.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;
static int count=0;
#define CHECK(x) do{++count;if(!(x))throw std::runtime_error("line "+std::to_string(__LINE__)+": " #x);}while(0)
static std::string utf8(const std::wstring&s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string r(n,'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),r.data(),n,nullptr,nullptr);return r;}
int main(){
 wchar_t temp[MAX_PATH]{},name[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);GetTempFileNameW(temp,L"npc",0,name);DeleteFileW(name);fs::path base=name;fs::create_directories(base);
 auto old=InstalledFL::environment(L"NPC_FL_STUDIO_ROOT");
 struct Cleanup{fs::path p;std::wstring old;~Cleanup(){SetEnvironmentVariableW(L"NPC_FL_STUDIO_ROOT",old.empty()?nullptr:old.c_str());std::error_code e;fs::remove_all(p,e);}} cleanup{base,old};
 try{
  auto one=base/L"FL Studio \u0422\u0435\u0441\u0442",two=base/L"FL Studio 2";std::wstring dll=L"Plugins/Fruity/Generators/Test/Test_x64.dll",dep=L"Shared/helper.dll";
  for(auto r:{one,two}){fs::create_directories((r/dll).parent_path());fs::create_directories(r/L"Shared");std::ofstream(r/dll)<<"test";std::ofstream(r/dep)<<"dep";}
  auto ini=base/L"bridge.ini";
  auto write=[&](const std::wstring&root,const std::string&module="Plugins/Fruity/Generators/Test/Test_x64.dll",const std::string&mode="installed",const std::string&dependency="Shared/helper.dll"){
   std::ofstream(ini)<<"[plugin]\nname=Test\nkind=instrument\nuid=123456789ABCDEF0123456789ABCDEF0\nsource="<<mode<<"\ndll="<<module<<"\n[installation]\nroot="<<utf8(root)<<"\n[dependencies]\ncount=1\ndll0="<<dependency<<"\n";
  };
  write(one.wstring());auto c=BridgeConfig::load(ini.c_str());CHECK(c.valid&&c.installed);CHECK(c.dllPath.empty());CHECK(c.resolveInstallation());CHECK(c.dependencyPaths.size()==1);CHECK(c.dllPath==InstalledFL::canonical((one/dll).wstring()));CHECK(c.rootPath==InstalledFL::canonical(one.wstring()));
  write((base/L"missing").wstring());c=BridgeConfig::load(ini.c_str());CHECK(c.valid);CHECK(!c.resolveInstallation());CHECK(!c.error.empty());
  write(L"relative");CHECK(!BridgeConfig::load(ini.c_str()).valid);
  write(one.wstring(),"../outside.dll");CHECK(!BridgeConfig::load(ini.c_str()).valid);
  write(one.wstring(),"Shared/helper.dll");CHECK(!BridgeConfig::load(ini.c_str()).valid);
  write(one.wstring(),"Plugins/Fruity/Generators/Test/Test_x64.dll","unknown");CHECK(!BridgeConfig::load(ini.c_str()).valid);
  write(one.wstring(),"Plugins/Fruity/Generators/Test/Test_x64.dll","installed","../outside.dll");CHECK(!BridgeConfig::load(ini.c_str()).valid);
  SetEnvironmentVariableW(L"NPC_FL_STUDIO_ROOT",two.c_str());write(L"");c=BridgeConfig::load(ini.c_str());CHECK(c.valid);CHECK(c.resolveInstallation());CHECK(c.rootPath==InstalledFL::canonical(two.wstring()));
  write(one.wstring());c=BridgeConfig::load(ini.c_str());CHECK(c.resolveInstallation());CHECK(c.rootPath==InstalledFL::canonical(one.wstring()));
  fs::remove(two/dep);write(L"");c=BridgeConfig::load(ini.c_str());CHECK(c.valid);CHECK(!c.resolveInstallation()); // invalid selection must not fall back
  std::wstring root,native;std::vector<std::wstring>paths;std::string error;
  CHECK(InstalledFL::choose({one.wstring(),one.wstring()},dll,{dep},root,native,paths,error));
  CHECK(InstalledFL::choose({one.wstring(),two.wstring()},dll,{dep},root,native,paths,error)); // only one complete installation
  std::ofstream(two/dep)<<"dep";
  CHECK(!InstalledFL::choose({one.wstring(),two.wstring()},dll,{dep},root,native,paths,error));CHECK(error.find("Multiple")!=std::string::npos);CHECK(root.empty()&&native.empty()&&paths.empty());
  CHECK(!InstalledFL::choose({},dll,{dep},root,native,paths,error));CHECK(error.find("No suitable")!=std::string::npos);
  CHECK(!InstalledFL::inside(L"C:\\FL",L"C:\\FL-other\\plugin.dll"));CHECK(InstalledFL::inside(L"C:\\FL",L"c:\\fl\\plugin.dll"));
  std::ofstream(ini)<<"[plugin]\nname=Old\nkind=effect\nuid=123456789ABCDEF0123456789ABCDEF0\ndll=FL/test.dll\nroot=FL\n[dependencies]\ncount=0\n";
  c=BridgeConfig::load(ini.c_str());CHECK(c.valid&&!c.installed);CHECK(c.resolveInstallation());
  std::cout<<"PASS "<<count<<" installed-root/config checks\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}
}
