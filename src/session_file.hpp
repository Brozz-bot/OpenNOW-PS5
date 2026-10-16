// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "account_file.hpp"
#include "cloud.hpp"
#include <array>
#include <limits>

namespace opennow::sessionFile {
enum class Status { missing, loaded, ownerMismatch, corrupt, unreadable };
struct Saved {
    char owner[1024]{},sessionId[128]{};
    StreamSettings settings{};
    unsigned audioChannels=2;
    bool resumeAllowed=true;
};
using Record=std::array<unsigned char,1224>;
inline std::uint32_t read32(const unsigned char* bytes) noexcept {
    return std::uint32_t(bytes[0])|(std::uint32_t(bytes[1])<<8)|
           (std::uint32_t(bytes[2])<<16)|(std::uint32_t(bytes[3])<<24);
}
inline void write32(unsigned char* bytes,std::uint32_t value) noexcept {
    for(unsigned i=0;i<4;++i)bytes[i]=static_cast<unsigned char>(value>>(8*i));
}
inline std::uint32_t checksum(const unsigned char* bytes,std::size_t size) noexcept {
    std::uint32_t hash=2166136261U;
    for(std::size_t i=0;i<size;++i){hash^=bytes[i];hash*=16777619U;}
    return hash;
}
inline int openParent(const char* path) noexcept {
    if(!path||!*path){errno=EINVAL;return -1;}
    char parent[1024];
    const auto size=strnlen(path,sizeof(parent));
    if(size>=sizeof(parent)-4){errno=ENAMETOOLONG;return -1;}
    if(path[size-1]=='/'){errno=EINVAL;return -1;}
    std::memcpy(parent,path,size+1);
    auto* slash=std::strrchr(parent,'/');
    if(!slash)std::snprintf(parent,sizeof(parent),".");
    else if(slash==parent)slash[1]=0;
    else *slash=0;
    for(auto* at=parent;;++at) {
        if(*at!='/'&&*at)continue;
        if(at==parent&&*at)continue;
        const char saved=*at;
        *at=0;
        const int fd=accountFile::open(parent,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
        *at=saved;
        if(fd<0)return -1;
        if(!saved)return fd;
        if(accountFile::close(fd)!=0)return -1;
    }
}
inline Status load(const char* path,const char* owner,Saved& out) noexcept {
    out=Saved{};
    if(!owner||!*owner||strnlen(owner,sizeof(out.owner))>=sizeof(out.owner)) {
        errno=EINVAL;return Status::unreadable;
    }
    const int parent=openParent(path);
    if(parent<0)return errno==ENOENT?Status::missing:Status::unreadable;
    if(accountFile::close(parent)!=0)return Status::unreadable;
    const int fd=accountFile::open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0)return errno==ENOENT?Status::missing:Status::unreadable;
    std::array<unsigned char,Record{}.size()+1> record{};
    std::size_t offset=0;
    int error=0;
    while(offset<record.size()) {
        const auto n=accountFile::read(fd,record.data()+offset,record.size()-offset);
        if(n<0&&errno==EINTR)continue;
        if(n<0){error=errno;break;}
        if(n==0)break;
        offset+=static_cast<std::size_t>(n);
    }
    if(accountFile::close(fd)!=0&&!error)error=errno;
    if(error){errno=error;return Status::unreadable;}
    if(offset!=Record{}.size()||std::memcmp(record.data(),"ONSP",4)!=0||read32(record.data()+4)!=1||
       read32(record.data()+1220)!=checksum(record.data(),1220))return Status::corrupt;
    if(!record[8]||!record[1032]||!std::memchr(record.data()+8,0,1024)||
       !std::memchr(record.data()+1032,0,128))return Status::corrupt;
    const auto* fields=record.data()+1160;
    for(unsigned field:{0u,1u,2u,3u,8u,9u,10u,11u})
        if(read32(fields+field*4)>static_cast<std::uint32_t>(std::numeric_limits<int>::max()))return Status::corrupt;
    if(read32(fields+16)>static_cast<unsigned>(VideoMode::hevcMain10HdrHardware)||
       read32(fields+20)>static_cast<unsigned>(QualityMode::clarity)||
       read32(fields+24)>static_cast<unsigned>(NetworkPolicy::fixed)||
       read32(fields+28)>static_cast<unsigned>(audio::Mode::surround71)||
       read32(fields+48)>1||read32(fields+56)>1)return Status::corrupt;
    Saved saved;
    saved.settings={static_cast<int>(read32(fields)),static_cast<int>(read32(fields+4)),
        static_cast<int>(read32(fields+8)),static_cast<int>(read32(fields+12)),
        static_cast<VideoMode>(read32(fields+16)),static_cast<QualityMode>(read32(fields+20)),
        static_cast<NetworkPolicy>(read32(fields+24)),static_cast<audio::Mode>(read32(fields+28)),
        static_cast<int>(read32(fields+32)),static_cast<int>(read32(fields+36)),
        static_cast<int>(read32(fields+40)),static_cast<int>(read32(fields+44)),read32(fields+48)==1};
    saved.audioChannels=read32(fields+52);
    saved.resumeAllowed=read32(fields+56)==1;
    if(validateSettings(saved.settings)!=SettingsError::none||
       (saved.audioChannels!=2&&saved.audioChannels!=6&&saved.audioChannels!=8))return Status::corrupt;
    if(std::strcmp(reinterpret_cast<const char*>(record.data()+8),owner)!=0)return Status::ownerMismatch;
    std::memcpy(saved.owner,record.data()+8,sizeof(saved.owner));
    std::memcpy(saved.sessionId,record.data()+1032,sizeof(saved.sessionId));
    out=saved;
    return Status::loaded;
}
inline int save(const char* path,const char* owner,const Session& session,bool resumeAllowed=true) noexcept {
    if(!owner||!*owner||strnlen(owner,1024)>=1024||!session.id[0]||
       !std::memchr(session.id,0,sizeof(session.id))||validateSettings(session.settings)!=SettingsError::none||
       (session.audioChannels!=2&&session.audioChannels!=6&&session.audioChannels!=8))return EINVAL;
    const int parent=openParent(path);
    if(parent<0)return errno;
    const int existing=accountFile::open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    int error=existing<0&&errno!=ENOENT?errno:0;
    if(existing>=0&&accountFile::close(existing)!=0)error=errno;
    if(error){accountFile::close(parent);return error;}
    Record record{'O','N','S','P'};
    write32(record.data()+4,1);
    std::memcpy(record.data()+8,owner,std::strlen(owner));
    std::memcpy(record.data()+1032,session.id,std::strlen(session.id));
    const auto& s=session.settings;
    const std::array<std::uint32_t,15> fields{
        static_cast<std::uint32_t>(s.width),static_cast<std::uint32_t>(s.height),
        static_cast<std::uint32_t>(s.fps),static_cast<std::uint32_t>(s.bitrate_kbps),
        static_cast<std::uint32_t>(s.mode),static_cast<std::uint32_t>(s.quality),
        static_cast<std::uint32_t>(s.network),static_cast<std::uint32_t>(s.audio_mode),
        static_cast<std::uint32_t>(s.controller_left_deadzone),static_cast<std::uint32_t>(s.controller_right_deadzone),
        static_cast<std::uint32_t>(s.vibration_intensity),static_cast<std::uint32_t>(s.mouse_sensitivity),
        s.persist_game_settings?1u:0u,session.audioChannels,resumeAllowed?1u:0u};
    for(unsigned i=0;i<fields.size();++i)write32(record.data()+1160+i*4,fields[i]);
    write32(record.data()+1220,checksum(record.data(),1220));
    char temporary[1024];
    std::snprintf(temporary,sizeof(temporary),"%s.tmp",path);
    const int fd=accountFile::open(temporary,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);
    if(fd<0){error=errno;accountFile::close(parent);return error;}
    if(accountFile::permissions(fd,0600)!=0)error=errno;
    std::size_t offset=0;
    while(!error&&offset<record.size()) {
        const auto n=accountFile::write(fd,record.data()+offset,record.size()-offset);
        if(n<0&&errno==EINTR)continue;
        if(n<=0)error=n<0?errno:EIO;
        else offset+=static_cast<std::size_t>(n);
    }
    if(!error&&accountFile::sync(fd)!=0)error=errno;
    if(accountFile::close(fd)!=0&&!error)error=errno;
    if(!error&&accountFile::rename(temporary,path)!=0)error=errno;
    if(error)accountFile::remove(temporary);
    else if(accountFile::sync(parent)!=0)error=errno;
    if(accountFile::close(parent)!=0&&!error)error=errno;
    return error;
}
inline int remove(const char* path) noexcept {
    const int parent=openParent(path);
    if(parent<0)return errno==ENOENT?0:errno;
    const int fd=accountFile::open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    int error=fd<0&&errno!=ENOENT?errno:0;
    if(fd>=0&&accountFile::close(fd)!=0)error=errno;
    if(!error&&fd>=0&&accountFile::remove(path)!=0&&errno!=ENOENT)error=errno;
    if(!error&&accountFile::sync(parent)!=0)error=errno;
    if(accountFile::close(parent)!=0&&!error)error=errno;
    return error;
}
}
