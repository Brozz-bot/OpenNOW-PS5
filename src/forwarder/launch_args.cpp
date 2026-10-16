// SPDX-License-Identifier: GPL-3.0-or-later
#include "launch_args.hpp"
#include <cstring>

namespace opennow::forwarder {
LaunchArgs parseLaunchArgs(int argc,const char* const argv[]) noexcept {
    LaunchArgs out;
    const auto invalid=[] {LaunchArgs result;result.status=LaunchArgsStatus::invalid;return result;};
    if(argc<0||argc>5||(argc&&!argv))return invalid();
    if(!argc)return out;
    for(int i=0;i<argc;++i)if(!argv[i])return invalid();
    int at=argv[0][0]=='-'?0:1;
    bool hasId=false,hasStore=false;
    while(at<argc) {
        const char* key=argv[at++];
        if(at==argc)return invalid();
        const char* value=argv[at++];
        if(!std::strcmp(key,"--gfn-app-id")) {
            if(hasId||!*value||std::strlen(value)>=sizeof(out.appId))return invalid();
            for(const char* p=value;*p;++p)if(*p<'0'||*p>'9')return invalid();
            std::memcpy(out.appId,value,std::strlen(value)+1);hasId=true;
        } else if(!std::strcmp(key,"--store")) {
            if(hasStore||!*value||std::strlen(value)>=sizeof(out.store))return invalid();
            for(const char* p=value;*p;++p)if(static_cast<unsigned char>(*p)<32||*p==127)return invalid();
            std::memcpy(out.store,value,std::strlen(value)+1);hasStore=true;
        } else return invalid();
    }
    if(!hasId)return hasStore?invalid():out;
    out.status=LaunchArgsStatus::valid;
    return out;
}
}
