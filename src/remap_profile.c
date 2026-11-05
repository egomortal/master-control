/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "remap_profile.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
static int identifier(const char *s) {
    size_t n=0;
    if (!s || !*s) return 0;
    for (;s[n];n++) {
        unsigned char c=(unsigned char)s[n];
        if(n>=63 || !((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
            (c>='0'&&c<='9')||c=='-'||c=='_')) return 0;
    }
    return 1;
}
static int hex(unsigned char c) {
    if(c>='0'&&c<='9')return c-'0';
    if(c>='A'&&c<='F')return c-'A'+10;
    if(c>='a'&&c<='f')return c-'a'+10;
    return -1;
}
int remap_profile_load(const char *root,const char *app,const char *profile,
                       uint16_t *vid,uint16_t *pid,char text[REMAP_TEXT_MAX+1],
                       const char **error) {
    int rd=-1,ad=-1,fd=-1,ok=0;size_t n=0;ssize_t got;
    char file[68],buf[REMAP_TEXT_MAX+20];struct stat st;
    remap_map map;int bad=0;unsigned v=0,p=0;
    *error="profile-id";
    if(!root||!*root||!identifier(app)||!identifier(profile))return 0;
    *error="missing";
    rd=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
    if(rd<0)goto done;
    ad=openat(rd,app,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
    if(ad<0)goto done;
    snprintf(file,sizeof file,"%s.map",profile);
    fd=openat(ad,file,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0)goto done;
    *error="profile-file";
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<19||
       st.st_size>REMAP_TEXT_MAX+19)goto done;
    while(n<sizeof buf){got=read(fd,buf+n,sizeof buf-n);if(got<0){if(errno==EINTR)continue;goto done;}if(!got)break;n+=(size_t)got;}
    if(n<19||n>REMAP_TEXT_MAX+19||memchr(buf,0,n))goto done;
    if(memcmp(buf,"device = ",9)||buf[13]!='_'||buf[18]!='\n')goto done;
    for(int i=0;i<9;i++){if(i==4)continue;int d=hex((unsigned char)buf[9+i]);if(d<0)goto done;if(i<4)v=(v<<4)|(unsigned)d;else p=(p<<4)|(unsigned)d;}
    memcpy(text,buf+19,n-19);text[n-19]=0;
    remap_default(&map);*error="profile-map";
    if(!remap_parse(text,&map,NULL,0,&bad))goto done;
    *vid=(uint16_t)v;*pid=(uint16_t)p;*error="";ok=1;
done:
    if(fd>=0)close(fd);
    if(ad>=0)close(ad);
    if(rd>=0)close(rd);
    return ok;
}
