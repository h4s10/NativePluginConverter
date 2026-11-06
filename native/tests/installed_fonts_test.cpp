// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#if defined(NPC_FONT_OWNER)
#include "../bridge/installed_fonts.h"
static InstalledFonts fonts;
extern "C" __declspec(dllexport) bool FontAttach(HMODULE module,const wchar_t* root){return fonts.attach(module,root);}
extern "C" __declspec(dllexport) void FontDetach(){fonts.detach();}
#elif defined(NPC_FONT_FIXTURE)
// Keep a real fixture return address for the module-scoped callback (no tail call).
extern "C" __declspec(dllexport) DWORD FontAttrs(const wchar_t* path){volatile DWORD result=GetFileAttributesW(path);return result;}
extern "C" __declspec(dllexport) DWORD FontRead(const wchar_t* path){
 HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(f==INVALID_HANDLE_VALUE)return MAXDWORD;
 DWORD size=GetFileSize(f,nullptr);CloseHandle(f);return size;
}
extern "C" __declspec(dllexport) bool FontWrite(const wchar_t* path){
 HANDLE f=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(f==INVALID_HANDLE_VALUE)return false;CloseHandle(f);return true;
}
#else
#include <filesystem>
#include <fstream>
#include <cstdio>
namespace fs=std::filesystem;
static int checks=0;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s (line %d)\n",checks,#x,__LINE__);fflush(stderr);TerminateProcess(GetCurrentProcess(),1);return 1;}}while(0)
int main(){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);setvbuf(stdout,nullptr,_IONBF,0);
 wchar_t tmp[MAX_PATH]{},name[MAX_PATH]{},cwd[32768]{};GetCurrentDirectoryW(32768,cwd);GetTempPathW(MAX_PATH,tmp);GetTempFileNameW(tmp,L"npf",0,name);DeleteFileW(name);fs::path base=name;fs::create_directories(base/L"empty");
 struct Cleanup{fs::path base;std::wstring cwd;~Cleanup(){SetCurrentDirectoryW(cwd.c_str());std::error_code e;fs::remove_all(base,e);}}cleanup{base,cwd};
 auto root=base/L"FL Unicode \u0422\u0435\u0441\u0442",root2=base/L"Other";
 for(auto r:{root,root2}){fs::create_directories(r/L"Shared/Artwork/Fonts");std::ofstream(r/L"Shared/Artwork/Fonts/ILGlyphsEx.ilfont")<<"glyphs";}
 auto aPath=fs::path(cwd)/L"FontOwnerA.dll",bPath=fs::path(cwd)/L"FontOwnerB.dll";
 HMODULE fixture=LoadLibraryW((fs::path(cwd)/L"FontFixture.dll").c_str());CHECK(fixture);
 HMODULE a=LoadLibraryW(aPath.c_str()),b=LoadLibraryW(bPath.c_str());CHECK(a&&b&&a!=b);
 auto attachA=reinterpret_cast<bool(*)(HMODULE,const wchar_t*)>(GetProcAddress(a,"FontAttach")),attachB=reinterpret_cast<bool(*)(HMODULE,const wchar_t*)>(GetProcAddress(b,"FontAttach"));
 auto detachA=reinterpret_cast<void(*)()>(GetProcAddress(a,"FontDetach")),detachB=reinterpret_cast<void(*)()>(GetProcAddress(b,"FontDetach"));
 auto attrs=reinterpret_cast<DWORD(*)(const wchar_t*)>(GetProcAddress(fixture,"FontAttrs")),read=reinterpret_cast<DWORD(*)(const wchar_t*)>(GetProcAddress(fixture,"FontRead"));
 auto write=reinterpret_cast<bool(*)(const wchar_t*)>(GetProcAddress(fixture,"FontWrite"));CHECK(attachA&&attachB&&detachA&&detachB&&attrs&&read&&write);
 CHECK(SetCurrentDirectoryW((base/L"empty").c_str()));const wchar_t* request=L"Shared\\Artwork\\Fonts\\ILGlyphsEx.ilfont";
 CHECK(attrs(request)==INVALID_FILE_ATTRIBUTES);CHECK(read(request)==MAXDWORD);
 CHECK(!attachA(fixture,L"relative"));CHECK(!attachA(fixture,(base/L"missing").c_str()));
 CHECK(attachA(fixture,root.c_str()));CHECK(attrs(request)!=INVALID_FILE_ATTRIBUTES);CHECK(read(request)==6);
 CHECK(read(L"shared/artwork/fonts/ilglyphsex.ilfont")==6);CHECK(read(L"ILGlyphsEx.ilfont")==6);
 CHECK(!write(request));CHECK(GetFileAttributesW(request)==INVALID_FILE_ATTRIBUTES); // bridge imports never patched
 CHECK(read(L"Shared\\Artwork\\Fonts\\Other.ilfont")==MAXDWORD);
 CHECK(read(L"..\\empty\\Shared\\Artwork\\Fonts\\ILGlyphsEx.ilfont")==MAXDWORD);
 fs::create_directories(base/L"custom");std::ofstream(base/L"custom/ILGlyphsEx.ilfont")<<"original-file";
 CHECK(read((base/L"custom/ILGlyphsEx.ilfont").c_str())==13); // preserve successful original read
 CHECK(read((base/L"missing/ILGlyphsEx.ilfont").c_str())==MAXDWORD); // unrelated absolute paths
 CHECK(!attachB(fixture,root2.c_str()));CHECK(read(request)==6);
 CHECK(attachB(fixture,root.c_str()));detachA();CHECK(FreeLibrary(a));a=nullptr;
 CHECK(GetModuleHandleW(aPath.c_str())!=nullptr);CHECK(read(request)==6); // peer keeps callback owner alive
 detachB();CHECK(attrs(request)==INVALID_FILE_ATTRIBUTES);CHECK(read(request)==MAXDWORD);
 CHECK(GetModuleHandleW(aPath.c_str())==nullptr); // final release restores hooks and releases owner
 CHECK(attachB(fixture,root.c_str()));CHECK(read(request)==6); // fresh owner can acquire same module
 detachB();detachB();CHECK(read(request)==MAXDWORD);
 CHECK(FreeLibrary(b));CHECK(FreeLibrary(fixture));
 // No real font binary is needed or distributed: fixture reads six text bytes.
 printf("Installed font lookup: %d checks passed\n",checks);return 0;
}
#endif
