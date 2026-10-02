#include "input_target_owner.h"
#include <cstdio>
int main(){int owned=7,destroys=0,creates=0;bool double_destroy=false;
 auto destroy=[&](int h){++destroys;if(h!=7&&h!=9)double_destroy=true;};
 auto fail=[&](int& out){++creates;out=123;return false;};
 if(hvinput::ReplaceTargetView(owned,destroy,fail)||owned!=0){std::puts("FAIL failed create retained a destroyed/undefined handle");return 1;}
 auto success=[&](int& out){++creates;out=9;return true;};
 if(!hvinput::ReplaceTargetView(owned,destroy,success)||owned!=9||destroys!=1)return 2;
 hvinput::ReplaceTargetView(owned,destroy,fail);
 if(owned||destroys!=2||creates!=3||double_destroy)return 3;
 std::puts("PASS failed create, retry and retirement preserve unique ownership");return 0;}
