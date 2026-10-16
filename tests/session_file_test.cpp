// SPDX-License-Identifier: GPL-3.0-or-later
#include "session_file.hpp"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef OPENNOW_PS5
namespace {
int writeError=0,readError=0,renameError=0,fileSyncError=0,parentSyncError=0,permissionError=0;
bool shortIO=false,interruptRead=false,interruptWrite=false,zeroWrite=false;
unsigned fileSyncs=0,parentSyncs=0;
std::int64_t native(std::int64_t value){return value<0?static_cast<std::int32_t>(0x80020000U|errno):value;}
}
extern "C" int sceKernelOpen(const char* path,int flags,unsigned mode) {
    assert(flags&O_NOFOLLOW);
    if(flags&O_CREAT)assert(mode==0600);
    return static_cast<int>(native(::open(path,flags,mode)));
}
extern "C" std::int64_t sceKernelRead(int fd,void* data,std::size_t size) {
    if(interruptRead){interruptRead=false;errno=EINTR;return native(-1);}
    if(readError){errno=readError;return native(-1);}
    return native(::read(fd,data,shortIO?std::min(size,std::size_t(19)):size));
}
extern "C" std::int64_t sceKernelWrite(int fd,const void* data,std::size_t size) {
    if(interruptWrite){interruptWrite=false;errno=EINTR;return native(-1);}
    if(writeError){errno=writeError;return native(-1);}
    if(zeroWrite)return 0;
    return native(::write(fd,data,shortIO?std::min(size,std::size_t(23)):size));
}
extern "C" int sceKernelClose(int fd){return static_cast<int>(native(::close(fd)));}
extern "C" int sceKernelFsync(int fd) {
    struct stat info{};assert(::fstat(fd,&info)==0);
    const bool parent=S_ISDIR(info.st_mode);
    if(parent)++parentSyncs;else ++fileSyncs;
    const int error=parent?parentSyncError:fileSyncError;
    if(error){errno=error;return static_cast<int>(native(-1));}
    return static_cast<int>(native(::fsync(fd)));
}
extern "C" int sceKernelFchmod(int fd,unsigned mode) {
    assert(mode==0600);
    if(permissionError){errno=permissionError;return static_cast<int>(native(-1));}
    return static_cast<int>(native(::fchmod(fd,mode)));
}
extern "C" int sceKernelRename(const char* from,const char* to) {
    if(renameError){errno=renameError;return static_cast<int>(native(-1));}
    return static_cast<int>(native(::rename(from,to)));
}
extern "C" int sceKernelUnlink(const char* path){return static_cast<int>(native(::unlink(path)));}
#endif

using namespace opennow;
namespace {
using Bytes=std::vector<unsigned char>;
constexpr const char* owner="nvidia:stable-subject";
void writeRaw(const std::string& path,const Bytes& bytes) {
    const int fd=::open(path.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);assert(fd>=0);
    assert(::write(fd,bytes.data(),bytes.size())==static_cast<ssize_t>(bytes.size()));
    assert(::close(fd)==0);
}
Bytes readRaw(const std::string& path) {
    const int fd=::open(path.c_str(),O_RDONLY);assert(fd>=0);
    Bytes bytes(2048);
    const auto size=::read(fd,bytes.data(),bytes.size());assert(size>=0);
    bytes.resize(static_cast<std::size_t>(size));assert(::close(fd)==0);return bytes;
}
void set32(Bytes& bytes,unsigned offset,std::uint32_t value) {
    for(unsigned i=0;i<4;++i)bytes.at(offset+i)=static_cast<unsigned char>(value>>(8*i));
}
void sign(Bytes& bytes) {
    std::uint32_t hash=2166136261U;
    for(unsigned i=0;i<1220;++i)hash=(hash^bytes.at(i))*16777619U;
    set32(bytes,1220,hash);
}
void assertEmpty(const sessionFile::Saved& saved) {
    assert(!saved.owner[0]&&!saved.sessionId[0]&&saved.settings==StreamSettings{}&&
           saved.audioChannels==2&&saved.resumeAllowed);
}
Session sample() {
    Session session;
    std::strcpy(session.id,"remote-webrtc-session");
    std::strcpy(session.signaling,"not-persisted-signaling");
    std::strcpy(session.mediaIp,"not-persisted-endpoint");
    session.settings=settingsFor(StreamProfile::native_hdr120);
    session.settings.quality=QualityMode::clarity;
    session.settings.audio_mode=audio::Mode::surround71;
    session.settings.controller_left_deadzone=3;
    session.settings.controller_right_deadzone=42;
    session.settings.vibration_intensity=71;
    session.settings.mouse_sensitivity=183;
    session.settings.persist_game_settings=false;
    session.audioChannels=8;
    return session;
}
void roundtrip(const std::string& path,const Session& session,bool allowed=true,const char* who=owner) {
    assert(sessionFile::save(path.c_str(),who,session,allowed)==0);
    sessionFile::Saved saved;
    assert(sessionFile::load(path.c_str(),who,saved)==sessionFile::Status::loaded);
    assert(std::strcmp(saved.owner,who)==0&&std::strcmp(saved.sessionId,session.id)==0);
    assert(saved.settings==session.settings&&saved.audioChannels==session.audioChannels&&saved.resumeAllowed==allowed);
    struct stat info{};assert(::stat(path.c_str(),&info)==0&&(info.st_mode&0777)==0600&&info.st_size==1224);
    assert(::lstat((path+".tmp").c_str(),&info)==-1&&errno==ENOENT);
    const auto bytes=readRaw(path);
    const std::string text(bytes.begin(),bytes.end());
    assert(text.find(session.signaling)==std::string::npos&&text.find(session.mediaIp)==std::string::npos);
}
void corrupt(const std::string& path,const Bytes& bytes) {
    writeRaw(path,bytes);
    sessionFile::Saved saved;
    std::strcpy(saved.sessionId,"stale-result");
    assert(sessionFile::load(path.c_str(),owner,saved)==sessionFile::Status::corrupt);
    assertEmpty(saved);
    assert(readRaw(path)==bytes);
}
}

int main() {
    char root[]="/tmp/opennow-session-XXXXXX";assert(mkdtemp(root));
    const std::string path=std::string(root)+"/session.bin",target=std::string(root)+"/unrelated.bin";
    const auto session=sample();
    sessionFile::Saved saved;
    assert(sessionFile::load(path.c_str(),owner,saved)==sessionFile::Status::missing);assertEmpty(saved);
    roundtrip(path,session);
    auto expected=readRaw(path);
    assert(expected[4]==1&&std::memcmp(expected.data(),"ONSP",4)==0);
    Bytes manual(1224);std::memcpy(manual.data(),"ONSP",4);set32(manual,4,1);
    std::memcpy(manual.data()+8,owner,std::strlen(owner));
    std::memcpy(manual.data()+1032,session.id,std::strlen(session.id));
    unsigned offset=1160;
    for(unsigned field:{3840,2160,120,100000,3,2,1,3,3,42,71,183,0,8,1}){set32(manual,offset,field);offset+=4;}
    sign(manual);assert(expected==manual);
    saved.settings={};
    assert(sessionFile::load(path.c_str(),owner,saved)==sessionFile::Status::loaded);
    assert(saved.settings==session.settings);
    assert(sessionFile::load(path.c_str(),"NVIDIA:stable-subject",saved)==sessionFile::Status::ownerMismatch);assertEmpty(saved);
    assert(sessionFile::load(path.c_str(),"nvidia:stable-subject-other",saved)==sessionFile::Status::ownerMismatch);assertEmpty(saved);
    assert(readRaw(path)==expected);
    for(auto mode:{VideoMode::h264Software,VideoMode::h264Hardware,VideoMode::hevcMain10SdrHardware,VideoMode::hevcMain10HdrHardware})
        for(unsigned channels:{2u,6u,8u})for(bool allowed:{true,false}) {
            auto next=session;next.settings.mode=mode;next.audioChannels=channels;
            if(mode==VideoMode::h264Software){next.settings.width=1920;next.settings.height=1080;next.settings.fps=60;}
            roundtrip(path,next,allowed);
        }
    for(auto profile:{StreamProfile::native_hdr90,StreamProfile::native_hdr60,StreamProfile::native_4k120,StreamProfile::native_4k90}) {
        auto next=session;next.settings=settingsFor(profile);roundtrip(path,next);
    }
    auto longest=session;std::memset(longest.id,'s',sizeof(longest.id)-1);longest.id[sizeof(longest.id)-1]=0;
    const std::string longOwner(1023,'o');roundtrip(path,longest,false,longOwner.c_str());

    for(std::size_t length=0;length<expected.size();++length)corrupt(path,Bytes(expected.begin(),expected.begin()+length));
    auto bytes=expected;bytes.push_back(0);corrupt(path,bytes);
    bytes.resize(2048);corrupt(path,bytes);
    for(std::size_t i=0;i<expected.size();++i){bytes=expected;bytes[i]^=1;corrupt(path,bytes);}
    for(unsigned version:{0u,2u,0xffffffffu}){bytes=expected;set32(bytes,4,version);sign(bytes);corrupt(path,bytes);}
    bytes=expected;bytes[0]='X';sign(bytes);corrupt(path,bytes);
    for(auto field:{std::pair<unsigned,unsigned>{1160,0},{1164,179},{1168,121},{1172,3999},{1176,4},
                    {1180,3},{1184,2},{1188,4},{1192,51},{1196,51},{1200,101},{1204,9},{1204,301},
                    {1208,2},{1212,0},{1212,1},{1212,7},{1212,9},{1216,2},{1160,0xffffffffu},{1192,0xffffffffu}}) {
        bytes=expected;set32(bytes,field.first,field.second);sign(bytes);corrupt(path,bytes);
    }
    for(auto field:{std::pair<unsigned,unsigned>{8,1024},{1032,128}}) {
        bytes=expected;std::memset(bytes.data()+field.first,'x',field.second);sign(bytes);corrupt(path,bytes);
        bytes=expected;bytes[field.first]=0;sign(bytes);corrupt(path,bytes);
    }
    roundtrip(path,session);
    const std::string invalidOwner(1024,'x');
    for(const char* bad:{static_cast<const char*>(nullptr),"",invalidOwner.c_str()}) {
        assert(sessionFile::load(path.c_str(),bad,saved)==sessionFile::Status::unreadable);assertEmpty(saved);
        assert(sessionFile::save(path.c_str(),bad,session)==EINVAL);
    }
    auto bad=session;bad.id[0]=0;assert(sessionFile::save(path.c_str(),owner,bad)==EINVAL);
    bad=session;std::memset(bad.id,'x',sizeof(bad.id));assert(sessionFile::save(path.c_str(),owner,bad)==EINVAL);
    bad=session;bad.settings.mouse_sensitivity=0;assert(sessionFile::save(path.c_str(),owner,bad)==EINVAL);
    bad=session;bad.audioChannels=3;assert(sessionFile::save(path.c_str(),owner,bad)==EINVAL);
    assert(readRaw(path)==expected);
    assert(sessionFile::save(nullptr,owner,session)==EINVAL);
    assert(sessionFile::remove("")==EINVAL);
    assert(sessionFile::save(std::string(1024,'p').c_str(),owner,session)==ENAMETOOLONG);
    const auto missing=std::string(root)+"/missing/session.bin";
    assert(sessionFile::save(missing.c_str(),owner,session)==ENOENT);
    assert(sessionFile::remove(missing.c_str())==0);

    writeRaw(target,{'u','n','r','e','l','a','t','e','d'});const auto unrelated=readRaw(target);
    assert(::symlink(target.c_str(),(path+".tmp").c_str())==0);
    assert(sessionFile::save(path.c_str(),owner,session)==ELOOP);
    assert(readRaw(path)==expected&&readRaw(target)==unrelated);
    assert(::unlink((path+".tmp").c_str())==0);
    writeRaw(path+".tmp",{'s','t','a','l','e'});assert(::chmod((path+".tmp").c_str(),0666)==0);
    roundtrip(path,session);
    assert(::unlink(path.c_str())==0&&::symlink(target.c_str(),path.c_str())==0);
    assert(sessionFile::load(path.c_str(),owner,saved)==sessionFile::Status::unreadable);assertEmpty(saved);
    assert(sessionFile::save(path.c_str(),owner,session)==ELOOP);
    assert(sessionFile::remove(path.c_str())==ELOOP);
    assert(readRaw(target)==unrelated&&::unlink(path.c_str())==0);
    const auto link=std::string(root)+"/link";
    assert(::symlink(root,link.c_str())==0);
    const auto linked=link+"/session.bin";
    assert(sessionFile::load(linked.c_str(),owner,saved)==sessionFile::Status::unreadable);assertEmpty(saved);
    assert(sessionFile::save(linked.c_str(),owner,session)!=0&&sessionFile::remove(linked.c_str())!=0);
    assert(::unlink(link.c_str())==0);
    assert(::mkdir(path.c_str(),0700)==0);
    assert(sessionFile::save(path.c_str(),owner,session)==EISDIR);
    struct stat info{};assert(::lstat((path+".tmp").c_str(),&info)==-1&&errno==ENOENT);
    assert(::rmdir(path.c_str())==0);

#ifdef OPENNOW_PS5
    fileSyncs=parentSyncs=0;
    shortIO=interruptRead=interruptWrite=true;roundtrip(path,session,false);shortIO=false;
    assert(fileSyncs==1&&parentSyncs==1);
    const auto before=readRaw(path);
    for(int* error:{&writeError,&renameError,&fileSyncError,&permissionError}) {
        *error=EIO;assert(sessionFile::save(path.c_str(),owner,session)==EIO);*error=0;
        assert(readRaw(path)==before);
        assert(::lstat((path+".tmp").c_str(),&info)==-1&&errno==ENOENT);
    }
    zeroWrite=true;assert(sessionFile::save(path.c_str(),owner,session)==EIO);zeroWrite=false;
    assert(readRaw(path)==before);
    readError=EIO;assert(sessionFile::load(path.c_str(),owner,saved)==sessionFile::Status::unreadable);readError=0;assertEmpty(saved);
    parentSyncError=EIO;assert(sessionFile::save(path.c_str(),owner,session)==EIO);parentSyncError=0;
    assert(sessionFile::load(path.c_str(),owner,saved)==sessionFile::Status::loaded&&saved.resumeAllowed);
    parentSyncError=EIO;assert(sessionFile::remove(path.c_str())==EIO);parentSyncError=0;
#endif
    roundtrip(path,session,false);
    assert(sessionFile::remove(path.c_str())==0&&sessionFile::remove(path.c_str())==0);
    assert(sessionFile::load(path.c_str(),owner,saved)==sessionFile::Status::missing);assertEmpty(saved);
    assert(readRaw(target)==unrelated&&::unlink(target.c_str())==0&&::rmdir(root)==0);
    std::puts("Owner-scoped WebRTC profile, corruption, symlink refusal and atomic persistence passed");
}
