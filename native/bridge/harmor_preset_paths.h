// SPDX-License-Identifier: MIT
#pragma once
#include "preset_paths.h"

namespace PresetPaths {
// Audited Harmor factory states: two LE32-sized part blocks begin at byte 8.
// A referenced resynthesis sample is the final UTF-8, byte-sized field in a
// part. Replacing it requires updating BOTH its one-byte length and the part byte count.
inline bool relocateHarmor(std::vector<uint8_t>&state,const std::wstring&root,std::string&error){
 error.clear();auto fail=[&](const char*s){error=s;return false;};
 if(state.size()<16)return fail("Harmor preset state is truncated");
 uint32_t version=FST::le32(state.data());
 if((version!=11&&version!=12&&version!=16&&version!=17&&version!=0x130003)||FST::le32(state.data()+4)!=7)return fail("Unsupported Harmor preset state layout");
 std::vector<uint8_t>next(state.begin(),state.begin()+8);size_t cursor=8;
 for(int part=0;part<2;part++){
  if(state.size()-cursor<4)return fail("Truncated Harmor part size");
  uint32_t length=FST::le32(state.data()+cursor);cursor+=4;
  if(length>state.size()-cursor)return fail("Truncated Harmor part");
  const size_t end=cursor+length;size_t pathAt=end,prefixAt=end;std::string path;
  // Search only exact FL factory tokens; binary audio may contain arbitrary '%'.
  for(size_t i=cursor;i<end;i++){
   const char*token=nullptr;for(const char*t:{"%FLStudioData%","%FLStudioFactoryData%"}){size_t n=strlen(t);if(n<=end-i&&!memcmp(state.data()+i,t,n)){token=t;break;}}
   if(!token)continue;
   if(end-i>32768)return fail("Oversized Harmor factory path");
   std::string candidate(reinterpret_cast<const char*>(state.data()+i),end-i);
   if(!FST::validUtf8(candidate))return fail("Invalid Harmor factory path");
   if(i==cursor||end-i>255||state[i-1]!=end-i)return fail("Invalid Harmor factory path length");
   prefixAt=i-1;
   pathAt=i;path=std::move(candidate);break;
  }
  std::vector<uint8_t>body;
  if(pathAt==end)body.assign(state.begin()+cursor,state.begin()+end);
  else{
   const bool data=path.rfind("%FLStudioData%",0)==0;size_t token=data?strlen("%FLStudioData%"):strlen("%FLStudioFactoryData%");
   std::wstring tail=BridgeConfig::wide(path.substr(token));while(!tail.empty()&&(tail[0]==L'/'||tail[0]==L'\\'))tail.erase(0,1);
   if(!BridgeConfig::relative(tail))return fail("Unsafe Harmor factory path");
   std::wstring destination=root+(data?L"\\Data\\":L"\\")+tail;
   int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,destination.data(),int(destination.size()),nullptr,0,nullptr,nullptr);
   if(n<=0||n>255)return fail("Harmor sample path exceeds its native 255-byte limit. Install the VST3 in a shorter folder.");
   if(!file(destination)){error="Harmor preset sample is not included: "+path;return false;}
   std::string resolved(n,0);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,destination.data(),int(destination.size()),resolved.data(),n,nullptr,nullptr);
   body.assign(state.begin()+cursor,state.begin()+prefixAt);body.push_back(uint8_t(n));body.insert(body.end(),resolved.begin(),resolved.end());
  }
  uint32_t n=uint32_t(body.size());for(int i=0;i<4;i++)next.push_back(uint8_t(n>>(i*8)));next.insert(next.end(),body.begin(),body.end());cursor=end;
 }
 next.insert(next.end(),state.begin()+cursor,state.end());state.swap(next);return true;
}
}
