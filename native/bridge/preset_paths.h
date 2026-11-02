// SPDX-License-Identifier: MIT
#pragma once
#include "preset.h"
#include "config.h"
#include <algorithm>

namespace PresetPaths {
// Audited factory layouts: 261 states all have encoding byte 1 at offset20,
// followed by a varint length and UTF-8 IR path. Only these observed versions
// are accepted; unknown layouts never reach the native file reader.
inline bool convolverPath(const std::vector<uint8_t>&state,size_t&end,std::string&text,std::string&error){
 auto fail=[&](const char*s){error=s;return false;};
 if(state.size()<23)return fail("Convolver preset state is truncated");
 uint32_t version=FST::le32(state.data());
 if(version!=11&&version!=14&&version!=0xe0003&&version!=0xf0003)return fail("Unsupported Convolver preset state version");
 if(state[20]!=1)return fail("Unsupported Convolver preset path encoding");
 size_t pos=21;uint32_t length=0;bool complete=false;
 for(unsigned i=0;i<5;i++){
  if(pos>=state.size())return fail("Truncated Convolver preset path length");uint8_t b=state[pos++];
  if(i==4&&(b&0xf0))return fail("Invalid Convolver preset path length");length|=uint32_t(b&127)<<(i*7);
  if(!(b&128)){if(i&&!(b&127))return fail("Noncanonical Convolver preset path length");complete=true;break;}
 }
 if(!complete||!length||length>32768||length>state.size()-pos)return fail("Invalid Convolver preset path length");
 text.assign(reinterpret_cast<const char*>(state.data()+pos),length);
 if(!FST::validUtf8(text))return fail("Invalid Convolver preset path");
 end=pos+length;return true;
}
inline bool file(const std::wstring&path){DWORD a=GetFileAttributesW(path.c_str());return a!=INVALID_FILE_ATTRIBUTES&&!(a&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));}
inline void matchImpulse(const std::wstring&folder,const std::wstring&name,std::vector<std::wstring>&matches,int depth=0){
 if(depth>16||matches.size()>1)return;
 WIN32_FIND_DATAW f{};HANDLE h=FindFirstFileW((folder+L"\\*").c_str(),&f);if(h==INVALID_HANDLE_VALUE)return;
 do{if(f.cFileName[0]==L'.'||(f.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))continue;std::wstring p=folder+L"\\"+f.cFileName;
  if(f.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)matchImpulse(p,name,matches,depth+1);
  else if(!_wcsicmp(f.cFileName,name.c_str()))matches.push_back(p);
 }while(matches.size()<2&&FindNextFileW(h,&f));FindClose(h);
}
inline bool relocateConvolver(std::vector<uint8_t>&state,const std::wstring&root,std::string&error){
 error.clear();size_t end=0;std::string path;
 if(!convolverPath(state,end,path,error))return false;
 auto starts=[&](const char*s){size_t n=strlen(s);return path.size()>=n&&FST::sameName(path.substr(0,n),s);};
 std::wstring destination;
 auto relative=[&](size_t prefix,std::wstring&out){out=BridgeConfig::wide(path.substr(prefix));while(!out.empty()&&(out[0]==L'/'||out[0]==L'\\'))out.erase(0,1);return BridgeConfig::relative(out);};
 std::wstring tail;
 if(starts("%FLStudioData%")){
  if(!relative(strlen("%FLStudioData%"),tail)){error="Unsafe Convolver preset path";return false;}destination=root+L"\\Data\\"+tail;
 }else if(starts("%FLStudioFactoryData%")){
  if(!relative(strlen("%FLStudioFactoryData%"),tail)){error="Unsafe Convolver preset path";return false;}destination=root+L"\\"+tail;
 }else if(starts("%USERPROFILE%")){
  if(!relative(strlen("%USERPROFILE%"),tail)){error="Unsafe Convolver preset path";return false;}
  auto slash=tail.find_last_of(L"\\/");std::wstring name=tail.substr(slash==std::wstring::npos?0:slash+1);std::vector<std::wstring>matches;
  matchImpulse(root+L"\\Data\\Patches\\Impulses",name,matches);
  if(matches.size()!=1){error="Convolver preset impulse is missing or ambiguous: "+path;return false;}destination=matches[0];
 }else{error="Unsupported Convolver preset impulse location: "+path;return false;}
 if(!file(destination)){error="Convolver preset impulse is not included: "+path;return false;}
 int length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,destination.data(),int(destination.size()),nullptr,0,nullptr,nullptr);
 if(length<=0||length>32768){error="Invalid resolved Convolver preset path";return false;}
 std::string resolved(length,0);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,destination.data(),int(destination.size()),resolved.data(),length,nullptr,nullptr);
 std::vector<uint8_t>next(state.begin(),state.begin()+21);uint32_t n=uint32_t(resolved.size());do{uint8_t b=n&127;n>>=7;next.push_back(b|(n?128:0));}while(n);
 next.insert(next.end(),resolved.begin(),resolved.end());next.insert(next.end(),state.begin()+end,state.end());state.swap(next);return true;
}
}
