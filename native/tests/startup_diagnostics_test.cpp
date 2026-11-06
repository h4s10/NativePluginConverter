// SPDX-License-Identifier: MIT
#include "../bridge/startup_diagnostics.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;
static int count=0;
#define CHECK(x) do{++count;if(!(x))throw std::runtime_error("line "+std::to_string(__LINE__)+": " #x);}while(0)
static std::string read(const fs::path& file){std::ifstream f(file,std::ios::binary);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
int main(){
 wchar_t temp[MAX_PATH]{},file[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);GetTempFileNameW(temp,L"npc",0,file);DeleteFileW(file);fs::path root=file;
 struct Cleanup {fs::path path;~Cleanup(){std::error_code e;fs::remove_all(path,e);}} cleanup{root};
 try {
  BridgeConfig cfg;cfg.nameUtf8="Edison";cfg.uid[0]=char(0xff);cfg.installed=true;cfg.dllRelative=L"Plugins/Fruity/Effects/Edison/Edison_x64.dll";cfg.iniPath=L"C:\\VST3\\Edison\\bridge.ini";
  SetLastError(ERROR_ACCESS_DENIED);
  auto path=StartupDiagnostics::writeAt((root/L"Logs").wstring(),cfg,"Unsupported FL Studio product-check service");
  CHECK(GetLastError()==ERROR_ACCESS_DENIED);CHECK(!path.empty());CHECK(fs::exists(path));
  auto text=read(path);CHECK(text.find("Plugin: Edison")!=std::string::npos);CHECK(text.find("product-check service")!=std::string::npos);CHECK(text.find("Edison_x64.dll")!=std::string::npos);
  CHECK(fs::path(path).filename().wstring().find(L"bridge-ff")==0);
  cfg.nameUtf8="../../outside\r\nFake: entry";
  CHECK(StartupDiagnostics::writeAt((root/L"Logs").wstring(),cfg,"short")==path);
  text=read(path);CHECK(text.find("Reason: short\r\n")!=std::string::npos);CHECK(text.find("product-check service")==std::string::npos);CHECK(text.find("\r\nFake:")==std::string::npos);
  CHECK(StartupDiagnostics::writeAt((root/L"Logs").wstring(),cfg,std::string(100000,'x'))==path);CHECK(fs::file_size(path)<20000);
  auto occupied=root/L"occupied";std::ofstream(occupied)<<"keep";
  CHECK(StartupDiagnostics::writeAt(occupied.wstring(),cfg,"error").empty());CHECK(read(occupied)=="keep");
  CHECK(StartupDiagnostics::writeAt(L"",cfg,"error").empty());
  std::cout<<"PASS "<<count<<" bounded startup report, identity, overwrite and failure handling checks\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
