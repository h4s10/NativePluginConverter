// SPDX-License-Identifier: MIT
#include "../bridge/preset_menu.h"
#include <cstdio>
#define CHECK(x) do{if(!(x)){printf("FAIL line%d %s\n",__LINE__,#x);return 1;}}while(0)
int main(){PresetMenu menu;CHECK(menu.handle());CHECK(menu.add(1,L"Default"));CHECK(menu.add(2,L"Filter/Smooth"));CHECK(menu.add(3,L"Filter/Bright",true));CHECK(menu.add(4,L"Distortion/Heavy/Soft clip"));CHECK(menu.add(5,L"Filter/A&B"));CHECK(menu.add(6,L"Ритм/Удар"));CHECK(GetMenuItemCount(menu.handle())==4);
 auto filter=GetSubMenu(menu.handle(),1);CHECK(filter&&GetMenuItemCount(filter)==3);CHECK(GetMenuItemID(filter,0)==2&&GetMenuItemID(filter,1)==3&&GetMenuItemID(filter,2)==5);
 CHECK(GetMenuState(filter,3,MF_BYCOMMAND)&MF_CHECKED);
 auto distortion=GetSubMenu(menu.handle(),2),heavy=GetSubMenu(distortion,0);CHECK(heavy&&GetMenuItemID(heavy,0)==4);
 wchar_t name[128]{};CHECK(GetMenuStringW(filter,2,name,128,MF_BYPOSITION)>0);CHECK(std::wstring(name)==L"A&&B");CHECK(GetMenuItemID(GetSubMenu(menu.handle(),3),0)==6);
 puts("PASS preset folders share submenus, recursive groups, original Engine IDs, Unicode and ampersand captions");return 0;}
