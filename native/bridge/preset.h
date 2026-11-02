// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// FST is a container, not a native state stream. Never load code while parsing it.
namespace FST {
constexpr size_t MaxFileSize=256u*1024u*1024u;
constexpr size_t MaxNativeSize=128u*1024u*1024u;
constexpr size_t MaxEvents=100000;
inline uint32_t le32(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline void appendUtf8(std::string&out,uint32_t c){
 if(c<0x80)out+=char(c);else if(c<0x800){out+=char(0xc0|(c>>6));out+=char(0x80|(c&63));}
 else if(c<0x10000){out+=char(0xe0|(c>>12));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}
 else{out+=char(0xf0|(c>>18));out+=char(0x80|((c>>12)&63));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}
}
inline bool validUtf8(const std::string&s){
 for(size_t i=0;i<s.size();){uint32_t c=uint8_t(s[i++]);if(c<0x80){if(c<32||c==127)return false;continue;}
  unsigned n=0;uint32_t min=0;if(c>=0xc2&&c<=0xdf){n=1;min=0x80;c&=31;}else if(c>=0xe0&&c<=0xef){n=2;min=0x800;c&=15;}else if(c>=0xf0&&c<=0xf4){n=3;min=0x10000;c&=7;}else return false;
  if(n>s.size()-i)return false;while(n--){uint8_t b=uint8_t(s[i++]);if((b&0xc0)!=0x80)return false;c=(c<<6)|(b&63);}if(c<min||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return false;
 }return true;
}
inline bool identity(const uint8_t*p,size_t n,std::string&out){
 out.clear();if(n<2||n>1024||p[n-1]!=0)return false;
 if(n>=4&&n%2==0&&p[n-2]==0){
  for(size_t i=0;i<n-2;i+=2){uint32_t c=p[i]|uint32_t(p[i+1])<<8;if(c<32||c==127)return false;
   if(c>=0xd800&&c<=0xdbff){if(i+4>=n)return false;uint32_t low=p[i+2]|uint32_t(p[i+3])<<8;if(low<0xdc00||low>0xdfff)return false;c=0x10000+((c-0xd800)<<10)+(low-0xdc00);i+=2;}
   else if(c>=0xdc00&&c<=0xdfff)return false;appendUtf8(out,c);
  }
 }else out.assign(reinterpret_cast<const char*>(p),n-1);
 return !out.empty()&&validUtf8(out);
}
inline bool sameName(const std::string&a,const std::string&b){
 if(a.size()!=b.size())return false;for(size_t i=0;i<a.size();++i){auto fold=[](unsigned char c){return c>='A'&&c<='Z'?c+32:c;};if(fold(uint8_t(a[i]))!=fold(uint8_t(b[i])))return false;}return true;
}
// Byte entry point also permits tests to validate malformed containers without I/O.
inline bool parseBytes(const uint8_t*data,size_t size,const std::string&expectedPlugin,std::vector<uint8_t>&native,std::string&error){
 native.clear();error.clear();auto fail=[&](const char*s){error=s;return false;};
 if(expectedPlugin.empty()||!validUtf8(expectedPlugin))return fail("Missing or invalid expected plugin identity");
 if(!data||size<22||size>MaxFileSize)return fail("Invalid or oversized FST file");
 if(memcmp(data,"FLhd",4)||le32(data+4)!=6||(data[8]!=0x30&&data[8]!=0x20)||data[9]!=0||memcmp(data+14,"FLdt",4)||le32(data+18)!=size-22)return fail("Invalid FST container header");
 size_t pos=22,count=0,stateAt=0,stateSize=0;bool gotName=false,gotState=false;std::string name;
 while(pos<size){
  if(++count>MaxEvents)return fail("Too many FST events");uint8_t id=data[pos++];size_t length=0;
  if(id<192)length=size_t(1)<<(id/64);
  else{
   uint32_t value=0;bool complete=false;
   for(unsigned byte=0;byte<5;++byte){if(pos==size)return fail("Truncated FST event length");uint8_t b=data[pos++];if(byte==4&&(b&0xf0))return fail("Overflow in FST event length");value|=uint32_t(b&127)<<(7*byte);if(!(b&128)){if(byte&&!(b&127))return fail("Noncanonical FST event length");complete=true;break;}}
   if(!complete)return fail("Invalid FST event length");length=value;
  }
  if(length>size-pos)return fail("Truncated FST event payload");
  if(id==201){if(gotName||!identity(data+pos,length,name))return fail("Invalid or duplicate FST plugin identity");gotName=true;}
  if(id==213){if(gotState||!length||length>MaxNativeSize)return fail("Invalid, oversized or duplicate FST native state");gotState=true;stateAt=pos;stateSize=length;}
  pos+=length;
 }
 if(!gotName||!gotState)return fail("FST plugin identity or native state is missing");
 if(!sameName(name,expectedPlugin))return fail("This preset belongs to a different plugin");
 native.assign(data+stateAt,data+stateAt+stateSize);return true;
}
inline bool parseFst(const wchar_t*path,const std::string&expectedPlugin,std::vector<uint8_t>&native,std::string&error){
 native.clear();error.clear();if(!path||!*path){error="Missing preset path";return false;}
#ifdef _WIN32
 FILE*f=_wfopen(path,L"rb");
#else
 // This branch is only for portable parser tests; production is Windows x64.
 std::string filename;for(const wchar_t*p=path;*p;++p){if(uint32_t(*p)>0x10ffff){error="Invalid preset path";return false;}appendUtf8(filename,uint32_t(*p));}FILE*f=fopen(filename.c_str(),"rb");
#endif
 if(!f){error="Cannot open preset";return false;}
 if(fseek(f,0,SEEK_END)){fclose(f);error="Cannot measure preset";return false;}long length=ftell(f);
 if(length<22||size_t(length)>MaxFileSize||fseek(f,0,SEEK_SET)){fclose(f);error="Invalid or oversized preset";return false;}
 auto data=std::vector<uint8_t>(size_t(length));size_t got=fread(data.data(),1,data.size(),f);int extra=fgetc(f);bool ok=got==data.size()&&extra==EOF&&!ferror(f);fclose(f);
 if(!ok){error="Cannot read complete preset";return false;}
 return parseBytes(data.data(),data.size(),expectedPlugin,native,error);
}
} // namespace FST
