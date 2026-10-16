// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cassert>
#include <limits>
#include <vector>
#define OPENNOW_PS5 1
#define main installer_payload_main
#include "../tooling/tile_installer/main.cpp"
#undef main
#include "../src/forwarder/installer_client.cpp"

namespace test {
using namespace opennow::forwarder;
namespace wire=installer::wire;
struct Buffer {
    std::vector<std::uint8_t> input,output;
    std::size_t offset=0,readChunk=3,writeChunk=7,writeLimit=SIZE_MAX;
    static std::int64_t read(void* context,void* bytes,std::size_t size) noexcept {
        auto& b=*static_cast<Buffer*>(context);
        const auto n=std::min({size,b.readChunk,b.input.size()-b.offset});
        if(n)std::memcpy(bytes,b.input.data()+b.offset,n);
        b.offset+=n;return static_cast<std::int64_t>(n);
    }
    static std::int64_t write(void* context,const void* bytes,std::size_t size) noexcept {
        auto& b=*static_cast<Buffer*>(context);
        if(b.output.size()>=b.writeLimit)return -1;
        const auto n=std::min({size,b.writeChunk,b.writeLimit-b.output.size()});
        const auto* p=static_cast<const std::uint8_t*>(bytes);
        b.output.insert(b.output.end(),p,p+n);return static_cast<std::int64_t>(n);
    }
    wire::Io io() noexcept {return {this,read,write};}
    void append(const wire::Frame& f) {input.insert(input.end(),f.bytes.begin(),f.bytes.begin()+wire::size(wire::kind(f)));}
};
wire::Request request(wire::Operation operation=wire::Operation::list,std::uint32_t id=0x12345678) {
    wire::Request r;r.operation=operation;r.pid=123;r.id=id;
    if(operation==wire::Operation::install)std::strcpy(r.argument,"job-123-1");
    if(operation==wire::Operation::remove)std::strcpy(r.argument,"PPSA99500");
    return r;
}
Entry entry(unsigned index=0) {
    Entry e;std::snprintf(e.titleId,sizeof(e.titleId),"CUSA%05u",10000+index);
    std::strcpy(e.game.id,"123");std::strcpy(e.game.title,"Test game");std::strcpy(e.game.store,"Steam");
    return e;
}
Result result(Code code=Code::listed) {Result r;r.code=code;return r;}
void appendResult(Buffer& b,const Result& r,std::uint32_t id=0x12345678) {
    wire::Frame f;assert(wire::encode(r,id,f));b.append(f);
}
void appendEntry(Buffer& b,const Entry& e,std::uint32_t id=0x12345678) {
    wire::Frame f;assert(wire::encode(e,id,f));b.append(f);
}
unsigned visits=0;
bool cancel(void*,const Entry&) noexcept {++visits;return false;}
bool visit(void*,const Entry& e) noexcept {assert(wire::valid(e));++visits;return true;}
sandbox::Status resolution=sandbox::Status::ok;
unsigned resolved=0,stored=0,listCount=2;
std::uint32_t resolvedPid=0;
Code storeCode=Code::listed;
char source[1024]{};
void resetPayload() {
    resolution=sandbox::Status::ok;resolved=stored=0;listCount=2;storeCode=Code::listed;source[0]=0;
}
void fixedPaths(const Paths& paths) {
    assert(!std::strcmp(paths.root,"/data/homebrew")&&!paths.templateRoot&&!paths.collisionRoots&&!paths.collisionRootCount&&!paths.stageRoot);
}
std::vector<std::uint8_t> helper,sent,replies;
std::size_t helperOffset=0,replyOffset=0;
std::atomic_bool helperPresent{true},connectBlocked{false};
std::atomic_uint opens{0},closes{0},sockets{0},socketCloses{0},connects{0},options{0},credentialWrites{0};
std::atomic_uint64_t clock{1};
int optionFailure=0,connectResult=0;
void makeHelper() {
    helper.assign(128,0);std::memcpy(helper.data(),"\x7f" "ELF\x02\x01\x01",7);
    wire::put32(helper.data()+40,64);wire::put16(helper.data()+58,64);wire::put16(helper.data()+60,1);
}
void resetNative(Code code=Code::created) {
    assert(!installer::probeRunning.load());
    helperPresent=true;helperOffset=replyOffset=0;sent.clear();replies.clear();optionFailure=connectResult=0;
    makeHelper();
    auto r=result(code);std::strcpy(r.titleId,"PPSA99500");
    wire::Frame f;assert(wire::encode(r,installer::nextId.load(),f));
    replies.assign(f.bytes.begin(),f.bytes.begin()+wire::resultSize);
}
void finishProbe() {
    for(unsigned i=0;i<5000&&installer::probeRunning.load(std::memory_order_acquire);++i)usleep(1000);
    assert(!installer::probeRunning.load(std::memory_order_acquire));
}
}

namespace opennow::forwarder::sandbox {
Status resolve(std::uint32_t pid,char* root,std::size_t capacity) noexcept {
    ++test::resolved;test::resolvedPid=pid;
    if(test::resolution==Status::ok)std::snprintf(root,capacity,"/mnt/sandbox/PPSA99082_123");
    return test::resolution;
}
}
namespace opennow::forwarder::store {
Result install(const char* source,const Paths& paths) noexcept {
    ++test::stored;test::fixedPaths(paths);std::snprintf(test::source,sizeof(test::source),"%s",source);
    auto r=test::result(Code::created);std::strcpy(r.titleId,"PPSA99500");return r;
}
Result list(Visit visit,void* context,const Paths& paths) noexcept {
    ++test::stored;test::fixedPaths(paths);auto r=test::result(test::storeCode);
    if(r.code==Code::listed)for(unsigned i=0;i<test::listCount;++i){++r.count;if(!visit(context,test::entry(i)))break;}
    return r;
}
Result remove(const char* titleId,const Paths& paths) noexcept {
    ++test::stored;test::fixedPaths(paths);auto r=test::result(Code::removed);
    std::snprintf(r.titleId,sizeof(r.titleId),"%s",titleId);return r;
}
}
extern "C" {
int sceKernelOpen(const char* path,int flags,unsigned) {
    assert(!std::strcmp(path,"/app0/opennow-tile-installer.elf")&&flags==(O_RDONLY|O_NOFOLLOW));
    ++test::opens;return test::helperPresent?10:-1;
}
int sceKernelClose(int fd) {assert(fd==10);++test::closes;return 0;}
std::int64_t sceKernelRead(int fd,void* bytes,std::size_t size) {
    assert(fd==10);const auto n=std::min({size,std::size_t{5},test::helper.size()-test::helperOffset});
    if(n)std::memcpy(bytes,test::helper.data()+test::helperOffset,n);
    test::helperOffset+=n;return static_cast<std::int64_t>(n);
}
int sceNetSocket(const char*,int domain,int type,int protocol) {assert(domain==2&&type==1&&protocol==6);++test::sockets;return 20;}
int sceNetSocketClose(int fd) {assert(fd==20);++test::socketCloses;return 0;}
int sceNetSetsockopt(int fd,int level,int option,const void* value,std::uint32_t size) {
    assert(fd==20&&level==0xffff&&size==sizeof(int)&&*static_cast<const int*>(value)==5'000'000);
    assert(option==0x1105||option==0x1106||option==0x1109);++test::options;
    return option==test::optionFailure?-1:0;
}
int sceNetConnect(int fd,const void* address,std::uint32_t size) {
    assert(fd==20&&size==16);const auto* p=static_cast<const std::uint8_t*>(address);
    assert(p[0]==16&&p[1]==2&&p[2]==0x23&&p[3]==0x3d&&p[4]==127&&p[5]==0&&p[6]==0&&p[7]==1);
    ++test::connects;while(test::connectBlocked.load())usleep(1000);
    return test::connectResult;
}
int sceNetSend(int fd,const void* bytes,std::size_t size,int flags) {
    assert(fd==20&&!flags);const auto n=std::min(size,std::size_t{7});const auto* p=static_cast<const std::uint8_t*>(bytes);
    test::sent.insert(test::sent.end(),p,p+n);return static_cast<int>(n);
}
int sceNetRecv(int fd,void* bytes,std::size_t size,int flags) {
    assert(fd==20&&!flags);const auto n=std::min({size,std::size_t{3},test::replies.size()-test::replyOffset});
    if(n)std::memcpy(bytes,test::replies.data()+test::replyOffset,n);
    test::replyOffset+=n;return static_cast<int>(n);
}
unsigned long long sceKernelGetProcessTime() {return test::clock.load();}
int seteuid(uid_t) noexcept {++test::credentialWrites;return -1;}
int setuid(uid_t) noexcept {++test::credentialWrites;return -1;}
int setgid(gid_t) noexcept {++test::credentialWrites;return -1;}
int setegid(gid_t) noexcept {++test::credentialWrites;return -1;}
int kernel_copyin(const void*,std::intptr_t,std::size_t) {++test::credentialWrites;return -1;}
}

int main() {
    using namespace test;
    const auto uid=getuid(),euid=geteuid();const auto gid=getgid(),egid=getegid();
    wire::Frame frame;
    auto r=request(wire::Operation::install);
    assert(wire::encode(r,frame));
    const std::uint8_t golden[]={'O','N','T','I',1,0,1,0,88,0,0,0,0x78,0x56,0x34,0x12,1,0,0,0,123,0,0,0};
    assert(!std::memcmp(frame.bytes.data(),golden,sizeof(golden)));
    assert(!std::strcmp(reinterpret_cast<const char*>(frame.bytes.data()+24),"job-123-1"));
    for(const char* name:{"",".","..","/tmp/stage","a/b","../stage","a.b","a b","a\\b"})assert(!wire::validStageName(name));
    assert(wire::validStageName("job-123-1_A"));
    char large[65];std::memset(large,'a',64);large[64]=0;assert(!wire::validStageName(large));large[63]=0;assert(wire::validStageName(large));
    for(const char* title:{"","PPSA99082","PPSA99100","PPSA99203","PPSA1234","PPSA123456","PPSA12x45","/PPSA99500","XXXX99500"})assert(!wire::validTitleId(title));
    assert(wire::validTitleId("PPSA99500")&&wire::validTitleId("CUSA12345")&&wire::validTitleId("LAPY12345"));
    for(auto pid:{0U,0x80000000U,UINT32_MAX}){auto bad=r;bad.pid=pid;assert(!wire::valid(bad));}
    auto bad=r;bad.id=0;assert(!wire::valid(bad));bad=r;bad.operation=static_cast<wire::Operation>(4);assert(!wire::valid(bad));
    bad=request();bad.argument[0]='a';assert(!wire::valid(bad));
    for(const auto code:wire::codes) {
        auto value=result(code);value.error=EACCES;
        assert(wire::encode(value,r.id,frame));Result decoded;assert(wire::decode(frame,decoded));
        assert(decoded.code==code&&decoded.error==EACCES);
    }
    for(unsigned field=0;field<12;++field) {
        assert(wire::encode(r,frame));
        switch(field) {
        case 0:frame.bytes[0]=0;break;
        case 1:wire::put16(frame.bytes.data()+4,2);break;
        case 2:wire::put16(frame.bytes.data()+6,4);break;
        case 3:wire::put32(frame.bytes.data()+8,wire::entrySize+1);break;
        case 4:wire::put32(frame.bytes.data()+8,wire::requestSize-1);break;
        case 5:wire::put32(frame.bytes.data()+12,0);break;
        case 6:wire::put32(frame.bytes.data()+16,4);break;
        case 7:wire::put32(frame.bytes.data()+20,0);break;
        case 8:wire::put32(frame.bytes.data()+20,UINT32_MAX);break;
        case 9:frame.bytes[24]='/';break;
        case 10:std::memset(frame.bytes.data()+24,'a',64);break;
        case 11:frame.bytes[87]='a';break;
        }
        Buffer b;b.input.assign(frame.bytes.begin(),frame.bytes.begin()+wire::requestSize);
        resetPayload();assert(!serve(b.io()));assert(!resolved&&!stored&&b.output.empty());
    }
    assert(wire::encode(r,frame));
    for(std::size_t cut=0;cut<wire::requestSize;++cut) {
        Buffer b;b.input.assign(frame.bytes.begin(),frame.bytes.begin()+cut);
        resetPayload();assert(!serve(b.io()));assert(!resolved&&!stored);
    }
    for(const auto operation:{wire::Operation::install,wire::Operation::list,wire::Operation::remove}) {
        const auto req=request(operation);assert(wire::encode(req,frame));Buffer b;b.append(frame);
        resetPayload();assert(serve(b.io()));assert(resolved==1&&resolvedPid==123&&stored==1);
        if(operation==wire::Operation::install)assert(!std::strcmp(source,"/mnt/sandbox/PPSA99082_123/download0/opennow/tile-stage/job-123-1"));
        Buffer client;client.input=b.output;visits=0;
        const auto value=wire::exchange(client.io(),req,visit,nullptr);assert(value.ok());
        assert(client.output==b.input&&client.offset==client.input.size());
        if(operation==wire::Operation::list)assert(value.count==2&&visits==2);
    }
    for(auto status:{sandbox::Status::invalidPid,sandbox::Status::wrongTitle,sandbox::Status::unavailable,sandbox::Status::invalidRoot}) {
        Buffer b;assert(wire::encode(r,frame));b.append(frame);resetPayload();resolution=status;assert(serve(b.io()));assert(resolved==1&&!stored);
        Buffer client;client.input=b.output;const auto value=wire::exchange(client.io(),r);assert(!value.ok());
        assert(value.code==(status==sandbox::Status::wrongTitle||status==sandbox::Status::invalidPid?Code::invalidGame:Code::unavailable));
    }
    Buffer overflow;const auto listing=request();assert(wire::encode(listing,frame));overflow.append(frame);
    resetPayload();listCount=wire::maxEntries+1;assert(serve(overflow.io()));Buffer receiver;receiver.input=overflow.output;
    const auto capped=wire::exchange(receiver.io(),listing);assert(capped.code==Code::ioError&&capped.error==EOVERFLOW&&capped.count==wire::maxEntries);
    Buffer entries;appendEntry(entries,entry());appendEntry(entries,entry(1));auto listed=result();listed.count=2;appendResult(entries,listed);
    const auto complete=entries.input;visits=0;
    auto value=wire::exchange(entries.io(),listing,cancel);assert(value.code==Code::cancelled&&value.count==1&&visits==1&&entries.offset==complete.size());
    for(std::size_t cut=0;cut<complete.size();++cut) {
        Buffer b;b.input.assign(complete.begin(),complete.begin()+cut);assert(!wire::exchange(b.io(),listing).ok());
    }
    Buffer duplicate;appendEntry(duplicate,entry());appendEntry(duplicate,entry());appendResult(duplicate,listed);
    assert(wire::exchange(duplicate.io(),listing).code==Code::ioError);
    Buffer mismatch;appendResult(mismatch,listed,listing.id+1);assert(wire::exchange(mismatch.io(),listing).code==Code::ioError);
    Buffer wrongCount;appendResult(wrongCount,listed);assert(wire::exchange(wrongCount.io(),listing).code==Code::ioError);
    Buffer remoteError;auto failure=result(Code::conflict);failure.error=EACCES;appendResult(remoteError,failure);
    value=wire::exchange(remoteError.io(),r);assert(value.code==Code::conflict&&value.error==EACCES);
    Buffer writeFailure;writeFailure.writeLimit=10;assert(wire::exchange(writeFailure.io(),r).code==Code::ioError);
    Buffer tooMany;for(unsigned i=0;i<=wire::maxEntries;++i)appendEntry(tooMany,entry(i));
    assert(wire::exchange(tooMany.io(),listing).code==Code::ioError);
    for(unsigned field=0;field<6;++field) {
        auto good=result();assert(wire::encode(good,listing.id,frame));
        switch(field) {
        case 0:wire::put32(frame.bytes.data()+16,99);break;
        case 1:wire::put32(frame.bytes.data()+20,UINT32_MAX);break;
        case 2:wire::put32(frame.bytes.data()+24,wire::maxEntries+1);break;
        case 3:std::memcpy(frame.bytes.data()+28,"PPSA99082",10);break;
        case 4:frame.bytes[38]=1;break;
        case 5:std::memset(frame.bytes.data()+28,'a',10);break;
        }
        Buffer b;b.append(frame);assert(wire::exchange(b.io(),listing).code==Code::ioError);
    }
    for(unsigned field=0;field<4;++field) {
        assert(wire::encode(entry(),listing.id,frame));
        if(field==0)std::memcpy(frame.bytes.data()+16,"PPSA99082",10);
        if(field==1)frame.bytes[650]=2;
        if(field==2)frame.bytes[652]=1;
        if(field==3)std::memset(frame.bytes.data()+26,'a',96);
        Buffer b;b.append(frame);assert(wire::exchange(b.io(),listing).code==Code::ioError);
    }
    for(const char* id:{"abc","0","-1","+1","1e3","2147483648","99999999999999999999999999999999999999"}) {
        assert(wire::encode(entry(),listing.id,frame));
        std::memset(frame.bytes.data()+26,0,96);std::memcpy(frame.bytes.data()+26,id,std::strlen(id));
        Entry invalid;assert(!wire::decode(frame,invalid));
        Buffer b;b.append(frame);visits=0;
        assert(wire::exchange(b.io(),listing,visit).code==Code::ioError&&visits==0);
    }
    for(const char control:{'\n','\t','\x1f','\x7f'}) {
        assert(wire::encode(entry(),listing.id,frame));frame.bytes[282]=static_cast<std::uint8_t>(control);
        Entry invalid;assert(!wire::decode(frame,invalid));
        Buffer b;b.append(frame);visits=0;
        assert(wire::exchange(b.io(),listing,visit).code==Code::ioError&&visits==0);
    }
    auto maximumId=entry();std::strcpy(maximumId.game.id,"2147483647");
    assert(wire::encode(maximumId,listing.id,frame));Entry validMaximumId;assert(wire::decode(frame,validMaximumId));
    resetNative();const auto expectedId=installer::nextId.load();
    value=installer::install("job-123-1");assert(value.code==Code::created&&sent.size()==helper.size()+wire::requestSize);
    assert(std::equal(helper.begin(),helper.end(),sent.begin()));
    Buffer nativeRequest;nativeRequest.input.assign(sent.begin()+helper.size(),sent.end());
    wire::Request decoded;assert(wire::receive(nativeRequest.io(),frame)&&wire::decode(frame,decoded));
    assert(decoded.pid==static_cast<std::uint32_t>(getpid())&&decoded.id==expectedId&&!std::strcmp(decoded.argument,"job-123-1"));
    resetNative(Code::removed);assert(installer::remove("PPSA99500").code==Code::removed);
    resetNative();Buffer nativeEntries;const auto listId=installer::nextId.load();
    appendEntry(nativeEntries,entry(),listId);appendEntry(nativeEntries,entry(1),listId);appendResult(nativeEntries,listed,listId);
    replies=nativeEntries.input;visits=0;value=installer::list(cancel,nullptr);
    assert(value.code==Code::cancelled&&value.count==1&&visits==1&&replyOffset==replies.size()&&sent.size()==helper.size()+wire::requestSize);
    resetNative();auto expectedOpens=opens.load();
    assert(installer::install("../bad").code==Code::invalidGame&&installer::remove("PPSA99082").code==Code::invalidGame);
    assert(opens==expectedOpens);
    resetNative();helper.push_back(0xff);assert(installer::install("job-1").code==Code::ioError&&sent.size()==128);
    resetNative();helper.resize(63);assert(installer::install("job-1").code==Code::ioError&&sent.empty());
    resetNative();helper[0]=0;assert(installer::install("job-1").code==Code::ioError&&sent.empty());
    resetNative();wire::put32(helper.data()+40,16*1024*1024);assert(installer::install("job-1").code==Code::ioError&&sent.empty());
    resetNative();optionFailure=0x1106;assert(installer::install("job-1").code==Code::ioError&&sent.empty());
    resetNative();connectResult=-1;assert(installer::install("job-1").code==Code::ioError&&sent.empty());
    resetNative();helperPresent=false;assert(installer::install("job-1").code==Code::unavailable);
    resetNative();connectBlocked=true;installer::available();assert(installer::probeRunning.load());
    connectBlocked=false;finishProbe();assert(installer::available()&&sent.empty());
    expectedOpens=opens.load();const auto expectedConnects=connects.load();
    for(unsigned i=0;i<10000;++i)assert(installer::available());
    assert(opens==expectedOpens&&connects==expectedConnects);
    test::clock+=31'000'000;helperPresent=false;installer::available();finishProbe();assert(!installer::available());
    expectedOpens=opens.load();for(unsigned i=0;i<10000;++i)assert(!installer::available());assert(opens==expectedOpens);
    assert(socketCloses==sockets&&closes+2==opens);
    assert(!credentialWrites&&uid==getuid()&&euid==geteuid()&&gid==getgid()&&egid==getegid());
    std::puts("installer protocol/client/payload tests passed");
}
