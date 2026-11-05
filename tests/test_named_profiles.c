/* SPDX-License-Identifier: GPL-3.0-or-later. Deterministic tests only. */
#include "remap_profile.h"
#include "hubcard.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
static int checks,fail;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);fail++;}}while(0)
static void put(const char *p,const char *s){FILE*f=fopen(p,"wb");if(!f)exit(2);fwrite(s,1,strlen(s),f);fclose(f);}
int main(void){
 char dir[]="/tmp/mc-named-XXXXXX",app[256],file[300],text[REMAP_TEXT_MAX+1];
 const char *err;uint16_t vid=0,pid=0;remap_info before,after;
 CHECK(mkdtemp(dir)!=NULL);snprintf(app,sizeof app,"%s/omni",dir);CHECK(!mkdir(app,0700));snprintf(file,sizeof file,"%s/media.map",app);
 put(file,"device = 045E_0B13\ncross = square\nl1 = r1\n");
 CHECK(remap_profile_load(dir,"omni","media",&vid,&pid,text,&err));CHECK(vid==0x45e&&pid==0xb13);CHECK(!strcmp(text,"cross = square\nl1 = r1\n"));
 remap_init(NULL);CHECK(remap_set(vid,pid,text,&err,NULL));CHECK(remap_get(0,&before));
 CHECK(!remap_profile_load(dir,"../omni","media",&vid,&pid,text,&err));CHECK(!strcmp(err,"profile-id"));
 CHECK(!remap_profile_load(dir,"omni","../media",&vid,&pid,text,&err));
 CHECK(!remap_profile_load(dir,"omni","media.map",&vid,&pid,text,&err));
 CHECK(!remap_profile_load(dir,"omni","",&vid,&pid,text,&err));
 CHECK(!remap_profile_load(dir,"other","media",&vid,&pid,text,&err));
 CHECK(!remap_profile_load(dir,"omni","missing",&vid,&pid,text,&err));CHECK(!strcmp(err,"missing"));
 put(file,"device = 045E_0B13\ncross = invented\n");CHECK(!remap_profile_load(dir,"omni","media",&vid,&pid,text,&err));CHECK(!strcmp(err,"profile-map"));
 put(file,"device = z45E_0B13\ncross = square\n");CHECK(!remap_profile_load(dir,"omni","media",&vid,&pid,text,&err));
 put(file,"device = 045E_0B13\n");CHECK(remap_profile_load(dir,"omni","media",&vid,&pid,text,&err));CHECK(!*text);
 {FILE*f=fopen(file,"wb");fputs("device = 045E_0B13\n",f);fputc(0,f);fputs("cross = square",f);fclose(f);}CHECK(!remap_profile_load(dir,"omni","media",&vid,&pid,text,&err));
 {FILE*f=fopen(file,"wb");for(int i=0;i<5000;i++)fputc('x',f);fclose(f);}CHECK(!remap_profile_load(dir,"omni","media",&vid,&pid,text,&err));
 {char link[300];snprintf(link,sizeof link,"%s/link.map",app);CHECK(!symlink("media.map",link));CHECK(!remap_profile_load(dir,"omni","link",&vid,&pid,text,&err));snprintf(link,sizeof link,"%s/alias",dir);CHECK(!symlink("omni",link));CHECK(!remap_profile_load(dir,"alias","media",&vid,&pid,text,&err));snprintf(link,sizeof link,"%s/pipe.map",app);CHECK(!mkfifo(link,0600));CHECK(!remap_profile_load(dir,"omni","pipe",&vid,&pid,text,&err));}
 CHECK(remap_get(0,&after));CHECK(!memcmp(&before.map,&after.map,sizeof before.map));
 {const char*j="{\"args\":{\"profile\":\"a\",\"pro\\u0066ile\":\"b\"}}";CHECK(hj_valid(j,strlen(j)));CHECK(hj_count(j,strlen(j),NULL,"args")==1);CHECK(hj_count(j,strlen(j),"args","profile")==2);}
 printf("%d deterministic named-profile checks, %d failures\n",checks,fail);return fail?1:0;
}
