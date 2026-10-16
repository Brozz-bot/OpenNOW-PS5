// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace opennow {
inline bool publicCloudIpv4(const unsigned char* ip) noexcept {
    return ip[0]!=0&&ip[0]!=10&&ip[0]!=127&&ip[0]<224&&
        !(ip[0]==169&&ip[1]==254)&&!(ip[0]==172&&ip[1]>=16&&ip[1]<=31)&&
        !(ip[0]==192&&ip[1]==168)&&!(ip[0]==100&&ip[1]>=64&&ip[1]<=127);
}
inline bool cloudControlOrigin(const char* url,char* out,std::size_t capacity) noexcept {
    if(!url||!out||!capacity)return false;
    out[0]=0;
    if(std::strncmp(url,"https://",8))return false;
    for(const auto* p=url;*p;++p)
        if(static_cast<unsigned char>(*p)<=32||static_cast<unsigned char>(*p)>=127||*p=='\\'||*p=='@'||*p=='#')return false;
    const char* start=url+8;const char* end=start+std::strcspn(start,"/?");
    if(start==end)return false;
    char host[256]{};
    const bool ipv6=*start=='[';
    const char* hostStart=ipv6?start+1:start;
    const char* hostEnd=ipv6?static_cast<const char*>(std::memchr(hostStart,']',end-hostStart)):
        static_cast<const char*>(std::memchr(hostStart,':',end-hostStart));
    if(ipv6&&!hostEnd)return false;
    if(!hostEnd)hostEnd=end;
    const char* port=ipv6?hostEnd+1:hostEnd;
    if(port!=end&&(end-port!=4||std::strncmp(port,":443",4)))return false;
    auto length=static_cast<std::size_t>(hostEnd-hostStart);
    if(!ipv6&&length&&hostStart[length-1]=='.')--length;
    if(!length||length>=sizeof(host))return false;
    for(std::size_t i=0;i<length;++i) {
        const unsigned char ch=hostStart[i];host[i]=ch>='A'&&ch<='Z'?ch+('a'-'A'):ch;
    }
    unsigned char address[16]{};
    if(ipv6) {
        if(inet_pton(AF_INET6,host,address)!=1)return false;
        static constexpr unsigned char mapped[]={0,0,0,0,0,0,0,0,0,0,255,255};
        if(!std::memcmp(address,mapped,sizeof(mapped))) {if(!publicCloudIpv4(address+12))return false;}
        else {
            bool zero=true;for(unsigned i=0;i<15;++i)zero&=address[i]==0;
            if((zero&&address[15]<=1)||address[0]==255||(address[0]&254)==252||
               (address[0]==254&&(address[1]&192)==128))return false;
        }
        char normalized[INET6_ADDRSTRLEN];
        if(!inet_ntop(AF_INET6,address,normalized,sizeof(normalized)))return false;
        std::snprintf(host,sizeof(host),"%s",normalized);
    } else if(inet_pton(AF_INET,host,address)==1) {
        if(!publicCloudIpv4(address))return false;
    } else {
        bool onlyNumbers=true,hasDot=false;
        for(std::size_t i=0;i<length;++i) {
            const char ch=host[i];hasDot|=ch=='.';onlyNumbers&=(ch>='0'&&ch<='9')||ch=='.';
            if(!((ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='-'||ch=='.'))return false;
            if(ch=='.'&&(i==0||i+1==length||host[i-1]=='.'))return false;
        }
        const char* last=std::strrchr(host,'.');
        bool numericTail=last&&last[1];
        if(last)for(const char* p=last+1;*p;++p)numericTail&=*p>='0'&&*p<='9';
        if(onlyNumbers||numericTail||!hasDot||(length>=6&&!std::strcmp(host+length-6,".local"))||
           (last&&last[1]=='0'&&last[2]=='x'))return false;
    }
    const int n=std::snprintf(out,capacity,ipv6?"https://[%s]/":"https://%s/",host);
    if(n<=0||static_cast<std::size_t>(n)>=capacity){out[0]=0;return false;}
    return true;
}
inline bool publicCloudControlUrl(const char* url) noexcept {
    char origin[272];return cloudControlOrigin(url,origin,sizeof(origin));
}
inline bool sameCloudControlOrigin(const char* left,const char* right) noexcept {
    char a[272],b[272];
    return cloudControlOrigin(left,a,sizeof(a))&&cloudControlOrigin(right,b,sizeof(b))&&!std::strcmp(a,b);
}
}
