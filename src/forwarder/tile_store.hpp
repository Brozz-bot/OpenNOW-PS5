// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "forwarder.hpp"
#include <cstring>

namespace opennow::forwarder::store {
inline bool validGame(const Game& game) noexcept {
    if(!std::memchr(game.id,0,sizeof(game.id))||!std::memchr(game.store,0,sizeof(game.store))||
       !std::memchr(game.title,0,sizeof(game.title))||!game.id[0]||!game.title[0])return false;
    std::uint64_t id=0;
    for(const char* p=game.id;*p;++p) {
        if(*p<'0'||*p>'9')return false;
        id=id*10+static_cast<unsigned>(*p-'0');
        if(id>2147483647U)return false;
    }
    if(!id)return false;
    for(const char* p=game.store;*p;++p)if(static_cast<unsigned char>(*p)<32||*p==127)return false;
    return true;
}
inline bool validTileId(const char* id) noexcept {
    if(!id||std::strlen(id)!=9||(std::strncmp(id,"PPSA",4)&&
       std::strncmp(id,"CUSA",4)&&std::strncmp(id,"LAPY",4)))return false;
    for(unsigned i=4;i<9;++i)if(id[i]<'0'||id[i]>'9')return false;
    const char* reserved[]={targetTitleId,"PPSA99100","PPSA99008","PPSA99764","PPSA99203","PPSA50011","PPSA00000"};
    for(const char* value:reserved)if(!std::strcmp(id,value))return false;
    return true;
}
bool forwarderJson(const Game& game,char* out,std::size_t size) noexcept;
bool paramJson(const Game& game,const char* titleId,char* out,std::size_t size) noexcept;
void initialTitleId(const Game& game,char (&titleId)[10]) noexcept;
bool verifyTemplate(const char* root) noexcept;
Result install(const char* stagedFolder,const Paths& paths={}) noexcept;
Result list(Visit visit,void* context,const Paths& paths={}) noexcept;
Result remove(const char* titleId,const Paths& paths={}) noexcept;
}
