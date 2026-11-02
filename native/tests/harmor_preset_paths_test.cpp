// SPDX-License-Identifier: MIT
#include "../bridge/harmor_preset_paths.h"
#include <filesystem>
#include <cstdio>
int wmain(int argc,wchar_t**argv){if(argc<2)return 2;std::filesystem::path root=argv[1];size_t count=0,changed=0;for(auto&f:std::filesystem::recursive_directory_iterator(root/L"Data/Patches/Plugin presets/Generators/Harmor")){if(f.path().extension()!=L".fst")continue;std::vector<uint8_t>state;std::string error;if(!FST::parseFst(f.path().c_str(),"Harmor",state,error))return 10;auto original=state;if(!PresetPaths::relocateHarmor(state,root.wstring(),error)){fprintf(stderr,"FAIL %s\n",error.c_str());return 11;}count++;changed+=original!=state;}printf("PASS %zu Harmor states, %zu relocated factory presets\n",count,changed);
std::vector<uint8_t>native;std::string error;auto preset=root/L"Data/Patches/Plugin presets/Generators/Harmor/Pad/Chorus ensemble.fst";if(!FST::parseFst(preset.c_str(),"Harmor",native,error))return 13;
int bad=0;for(int kind=0;kind<5;kind++){auto test=native;std::wstring location=root.wstring();if(kind==0)test[0]=0;if(kind==1)test.resize(10);if(kind==2)test[8]=test[9]=test[10]=test[11]=255;if(kind==3)location+=L"\\missing";if(kind==4)location+=std::wstring(300,L'x');auto copy=test;if(PresetPaths::relocateHarmor(test,location,error)||test!=copy)return 14;if(kind==4&&error.find("255-byte")==std::string::npos)return 15;bad++;}
printf("PASS %d rejected malformed/missing states without mutation\n",bad);return count==325&&changed==24?0:12;}
