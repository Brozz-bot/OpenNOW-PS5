// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "tile_store.hpp"
#include <array>
#include <cerrno>
#include <climits>
#include <cstring>

namespace opennow::forwarder::installer::wire {
inline constexpr int ioTimeoutUs=5'000'000;
inline constexpr std::size_t headerSize=16,requestSize=88,resultSize=40,entrySize=656;
inline constexpr unsigned maxEntries=700;
static_assert(sizeof(Entry{}.titleId)==10&&sizeof(Entry{}.game.id)==96&&sizeof(Entry{}.game.title)==160&&
    sizeof(Entry{}.game.store)==48&&sizeof(Entry{}.game.art)==160&&sizeof(Entry{}.game.hero)==160);
enum class Operation : std::uint32_t { install=1,list=2,remove=3 };
enum class Kind : std::uint16_t { request=1,result=2,entry=3 };
struct Request {
    Operation operation=Operation::list;
    std::uint32_t pid=0,id=0;
    char argument[64]{};
};
struct Io {
    void* context;
    std::int64_t (*read)(void*,void*,std::size_t) noexcept;
    std::int64_t (*write)(void*,const void*,std::size_t) noexcept;
};
struct Frame {
    std::array<std::uint8_t,entrySize> bytes{};
};
inline std::uint32_t get32(const std::uint8_t* p) noexcept {
    return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);
}
inline void put32(std::uint8_t* p,std::uint32_t v) noexcept {
    for(unsigned i=0;i<4;++i)p[i]=static_cast<std::uint8_t>(v>>(8*i));
}
inline std::uint16_t get16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0]|(unsigned(p[1])<<8));
}
inline void put16(std::uint8_t* p,std::uint16_t v) noexcept {
    p[0]=static_cast<std::uint8_t>(v);p[1]=static_cast<std::uint8_t>(v>>8);
}
inline bool zero(const std::uint8_t* p,std::size_t size) noexcept {
    for(std::size_t i=0;i<size;++i)if(p[i])return false;
    return true;
}
inline bool text(const char* p,std::size_t size,bool required=false) noexcept {
    const auto* end=static_cast<const char*>(std::memchr(p,0,size));
    return end&&(!required||end!=p)&&zero(reinterpret_cast<const std::uint8_t*>(end),size-static_cast<std::size_t>(end-p));
}
inline bool validStageName(const char* value) noexcept {
    if(!value)return false;
    std::size_t n=0;
    for(;n<64&&value[n];++n) {
        const char c=value[n];
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;
    }
    return n>0&&n<64;
}
inline bool validTitleId(const char* value) noexcept {
    return value&&strnlen(value,10)==9&&store::validTileId(value);
}
inline bool valid(const Request& request) noexcept {
    if(!request.pid||request.pid>INT32_MAX||!request.id||!text(request.argument,sizeof(request.argument)))return false;
    switch(request.operation) {
    case Operation::install:return validStageName(request.argument);
    case Operation::list:return request.argument[0]==0;
    case Operation::remove:return validTitleId(request.argument);
    }
    return false;
}
inline std::size_t size(Kind kind) noexcept {
    switch(kind) {
    case Kind::request:return requestSize;
    case Kind::result:return resultSize;
    case Kind::entry:return entrySize;
    }
    return 0;
}
inline Kind kind(const Frame& frame) noexcept {return static_cast<Kind>(get16(frame.bytes.data()+6));}
inline std::uint32_t id(const Frame& frame) noexcept {return get32(frame.bytes.data()+12);}
inline bool validHeader(const Frame& frame) noexcept {
    const auto* p=frame.bytes.data();
    const auto count=size(kind(frame));
    return !std::memcmp(p,"ONTI",4)&&get16(p+4)==1&&id(frame)&&count&&get32(p+8)==count;
}
inline void header(Frame& frame,Kind kind,std::uint32_t id) noexcept {
    frame={};
    auto* p=frame.bytes.data();
    std::memcpy(p,"ONTI",4);put16(p+4,1);put16(p+6,static_cast<std::uint16_t>(kind));
    put32(p+8,static_cast<std::uint32_t>(size(kind)));put32(p+12,id);
}
template<class Byte,class Transfer>
bool transfer(Byte* bytes,std::size_t size,Transfer operation) noexcept {
    while(size) {
        const auto n=operation(bytes,size);
        if(n<=0||static_cast<std::uint64_t>(n)>size)return false;
        bytes+=n;size-=static_cast<std::size_t>(n);
    }
    return true;
}
inline bool receive(const Io& io,Frame& frame) noexcept {
    frame={};
    const auto read=[&](void* bytes,std::size_t count){return io.read(io.context,bytes,count);};
    auto* p=frame.bytes.data();
    if(!transfer(p,headerSize,read)||!validHeader(frame))return false;
    const auto count=size(kind(frame));
    return transfer(p+headerSize,count-headerSize,read);
}
inline bool send(const Io& io,const Frame& frame) noexcept {
    return validHeader(frame)&&transfer(frame.bytes.data(),size(kind(frame)),[&](const void* bytes,std::size_t count){return io.write(io.context,bytes,count);});
}
inline bool encode(const Request& request,Frame& frame) noexcept {
    if(!valid(request))return false;
    header(frame,Kind::request,request.id);
    put32(frame.bytes.data()+16,static_cast<std::uint32_t>(request.operation));
    put32(frame.bytes.data()+20,request.pid);
    std::memcpy(frame.bytes.data()+24,request.argument,sizeof(request.argument));
    return true;
}
inline bool decode(const Frame& frame,Request& request) noexcept {
    if(!validHeader(frame)||kind(frame)!=Kind::request)return false;
    request={};request.id=id(frame);
    request.operation=static_cast<Operation>(get32(frame.bytes.data()+16));
    request.pid=get32(frame.bytes.data()+20);
    std::memcpy(request.argument,frame.bytes.data()+24,sizeof(request.argument));
    return valid(request);
}
inline constexpr Code codes[]={Code::created,Code::alreadyExists,Code::removed,Code::notFound,Code::listed,
    Code::available,Code::unavailable,Code::invalidGame,Code::noFreeTitleId,Code::conflict,Code::cancelled,
    Code::artworkError,Code::templateError,Code::ioError};
inline bool valid(const Result& result) noexcept {
    return result.error>=0&&result.count<=maxEntries&&text(result.titleId,sizeof(result.titleId))&&
        (!result.titleId[0]||validTitleId(result.titleId));
}
inline bool encode(const Result& result,std::uint32_t id,Frame& frame) noexcept {
    if(!id||!valid(result))return false;
    unsigned index=0;
    while(index<std::size(codes)&&codes[index]!=result.code)++index;
    if(index==std::size(codes))return false;
    header(frame,Kind::result,id);auto* p=frame.bytes.data();
    put32(p+16,index);put32(p+20,static_cast<std::uint32_t>(result.error));put32(p+24,result.count);
    std::memcpy(p+28,result.titleId,sizeof(result.titleId));
    return true;
}
inline bool decode(const Frame& frame,Result& result) noexcept {
    const auto* p=frame.bytes.data();const auto index=get32(p+16);
    if(!validHeader(frame)||kind(frame)!=Kind::result||index>=std::size(codes)||get32(p+20)>INT_MAX||!zero(p+38,2))return false;
    result={};result.code=codes[index];result.error=static_cast<int>(get32(p+20));result.count=get32(p+24);
    std::memcpy(result.titleId,p+28,sizeof(result.titleId));
    return valid(result);
}
inline bool valid(const Entry& entry) noexcept {
    return text(entry.titleId,sizeof(entry.titleId),true)&&validTitleId(entry.titleId)&&
        text(entry.game.id,sizeof(entry.game.id),true)&&text(entry.game.title,sizeof(entry.game.title),true)&&
        text(entry.game.store,sizeof(entry.game.store))&&text(entry.game.art,sizeof(entry.game.art))&&
        text(entry.game.hero,sizeof(entry.game.hero))&&store::validGame(entry.game);
}
inline bool encode(const Entry& entry,std::uint32_t id,Frame& frame) noexcept {
    if(!id||!valid(entry))return false;
    header(frame,Kind::entry,id);auto* p=frame.bytes.data()+16;
    const auto field=[&](const auto& value){std::memcpy(p,value,sizeof(value));p+=sizeof(value);};
    field(entry.titleId);field(entry.game.id);field(entry.game.title);field(entry.game.store);field(entry.game.art);field(entry.game.hero);
    *p++=entry.game.owned?1:0;*p=entry.game.supportsPersistence?1:0;
    return true;
}
inline bool decode(const Frame& frame,Entry& entry) noexcept {
    if(!validHeader(frame)||kind(frame)!=Kind::entry)return false;
    const auto* p=frame.bytes.data()+16;entry={};
    const auto field=[&](auto& value){std::memcpy(value,p,sizeof(value));p+=sizeof(value);};
    field(entry.titleId);field(entry.game.id);field(entry.game.title);field(entry.game.store);field(entry.game.art);field(entry.game.hero);
    if(p[0]>1||p[1]>1||!zero(p+2,4))return false;
    entry.game.owned=p[0]!=0;entry.game.supportsPersistence=p[1]!=0;
    return valid(entry);
}
inline Result failure(int error=EPROTO) noexcept {Result r;r.code=Code::ioError;r.error=error;return r;}
inline Result exchange(const Io& io,const Request& request,Visit visit=nullptr,void* context=nullptr) noexcept {
    Frame frame;
    if(!encode(request,frame))return failure(EINVAL);
    if(!send(io,frame))return failure(EIO);
    unsigned count=0,delivered=0;bool cancelled=false;
    std::array<std::array<char,10>,maxEntries> titles{};
    for(;;) {
        if(!receive(io,frame)||id(frame)!=request.id)return failure();
        if(kind(frame)==Kind::entry) {
            Entry entry;
            if(request.operation!=Operation::list||count==maxEntries||!decode(frame,entry))return failure();
            for(unsigned i=0;i<count;++i)if(!std::strcmp(titles[i].data(),entry.titleId))return failure();
            std::memcpy(titles[count++].data(),entry.titleId,sizeof(entry.titleId));
            if(!cancelled) {
                ++delivered;
                if(visit&&!visit(context,entry))cancelled=true;
            }
            continue;
        }
        Result result;
        if(!decode(frame,result))return failure();
        if(request.operation==Operation::list) {
            if(result.count!=count||result.titleId[0])return failure();
        } else if(result.count)return failure();
        if(result.ok()) {
            if(request.operation==Operation::list) {
                if(result.code!=Code::listed)return failure();
            } else if(request.operation==Operation::install) {
                if((result.code!=Code::created&&result.code!=Code::alreadyExists)||!result.titleId[0]||result.count)return failure();
            } else if((result.code!=Code::removed&&result.code!=Code::notFound)||std::strcmp(result.titleId,request.argument)||result.count)return failure();
        }
        if(cancelled&&result.code==Code::listed){result.code=Code::cancelled;result.count=delivered;}
        return result;
    }
}
}
