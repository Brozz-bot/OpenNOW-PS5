// SPDX-License-Identifier: GPL-3.0-or-later
#include "installer_client.hpp"
#include "installer_protocol.hpp"
#include <atomic>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

#ifdef OPENNOW_PS5
extern "C" {
int sceKernelOpen(const char*,int,unsigned);
int sceKernelClose(int);
std::int64_t sceKernelRead(int,void*,std::size_t);
int sceNetConnect(int,const void*,std::uint32_t);
int sceNetSend(int,const void*,std::size_t,int);
int sceNetRecv(int,void*,std::size_t,int);
int sceNetSetsockopt(int,int,int,const void*,std::uint32_t);
int sceNetSocket(const char*,int,int,int);
int sceNetSocketClose(int);
unsigned long long sceKernelGetProcessTime();
}
#endif

namespace opennow::forwarder::installer {
namespace {
std::atomic_uint32_t nextId{1};
#ifdef OPENNOW_PS5
constexpr char helperPath[]="/app0/opennow-tile-installer.elf";
std::atomic_bool probeRunning{false},cachedAvailable{false};
std::atomic_uint64_t cacheUntil{0};
struct NetSockaddrIn {
    std::uint8_t length,family;
    std::uint16_t port;
    std::uint32_t address;
    std::uint16_t virtualPort;
    std::uint8_t zero[6];
};
int connect() noexcept {
    const int socket=sceNetSocket("opennow_tile_installer",2,1,6);
    if(socket<0)return -1;
    constexpr int level=0xffff,timeout=wire::ioTimeoutUs;
    for(const int option:{0x1105,0x1106,0x1109}) {
        if(sceNetSetsockopt(socket,level,option,&timeout,sizeof(timeout))<0) {
            sceNetSocketClose(socket);return -1;
        }
    }
    constexpr std::uint16_t port=9021;
    const NetSockaddrIn address{sizeof(NetSockaddrIn),2,static_cast<std::uint16_t>((port<<8)|(port>>8)),0x0100007f,0,{0}};
    if(sceNetConnect(socket,&address,sizeof(address))<0){sceNetSocketClose(socket);return -1;}
    return socket;
}
void* probe(void*) noexcept {
    bool present=false;
    const int helper=sceKernelOpen(helperPath,O_RDONLY|O_NOFOLLOW,0);
    if(helper>=0) {
        sceKernelClose(helper);
        const int socket=connect();
        present=socket>=0;
        if(socket>=0)sceNetSocketClose(socket);
    }
    cachedAvailable.store(present,std::memory_order_relaxed);
    cacheUntil.store(sceKernelGetProcessTime()+(present?30'000'000:5'000'000),std::memory_order_relaxed);
    probeRunning.store(false,std::memory_order_release);
    return nullptr;
}
std::int64_t readSocket(void* context,void* data,std::size_t size) noexcept {
    return sceNetRecv(*static_cast<int*>(context),data,size,0);
}
std::int64_t writeSocket(void* context,const void* data,std::size_t size) noexcept {
    return sceNetSend(*static_cast<int*>(context),data,size,0);
}
bool upload(int helper,const wire::Io& io) noexcept {
    std::array<std::uint8_t,4096> bytes{};
    if(!wire::transfer(bytes.data(),64,[&](void* p,std::size_t n){return sceKernelRead(helper,p,n);}))return false;
    if(std::memcmp(bytes.data(),"\x7f" "ELF\x02\x01\x01",7))return false;
    const std::uint64_t sectionOffset=wire::get32(bytes.data()+40)|(std::uint64_t(wire::get32(bytes.data()+44))<<32);
    const auto entrySize=wire::get16(bytes.data()+58),count=wire::get16(bytes.data()+60);
    if(sectionOffset<64||entrySize!=64||!count||sectionOffset>16*1024*1024)return false;
    const auto extent=sectionOffset+std::uint64_t(entrySize)*count;
    if(extent>16*1024*1024)return false;
    if(!wire::transfer(bytes.data(),64,[&](const void* p,std::size_t n){return io.write(io.context,p,n);}))return false;
    std::uint64_t remaining=extent-64;
    while(remaining) {
        const auto capacity=remaining<bytes.size()?static_cast<std::size_t>(remaining):bytes.size();
        const auto n=sceKernelRead(helper,bytes.data(),capacity);
        if(n<=0||static_cast<std::uint64_t>(n)>capacity||
           !wire::transfer(bytes.data(),static_cast<std::size_t>(n),[&](const void* p,std::size_t length){return io.write(io.context,p,length);}))return false;
        remaining-=static_cast<std::uint64_t>(n);
    }
    return sceKernelRead(helper,bytes.data(),1)==0;
}
#endif
Result submit(wire::Operation operation,const char* argument,Visit visit=nullptr,void* context=nullptr) noexcept {
    wire::Request request;
    request.operation=operation;request.pid=static_cast<std::uint32_t>(getpid());
    request.id=nextId.fetch_add(1,std::memory_order_relaxed);
    if(!request.id)request.id=nextId.fetch_add(1,std::memory_order_relaxed);
    if(argument) {
        const auto length=strnlen(argument,sizeof(request.argument));
        if(length==sizeof(request.argument)){Result r;r.code=Code::invalidGame;r.error=EINVAL;return r;}
        std::memcpy(request.argument,argument,length);
    }
    if(!wire::valid(request)){Result r;r.code=Code::invalidGame;r.error=EINVAL;return r;}
#ifdef OPENNOW_PS5
    const int helper=sceKernelOpen(helperPath,O_RDONLY|O_NOFOLLOW,0);
    if(helper<0){Result r;r.code=Code::unavailable;return r;}
    int socket=connect();
    Result result=wire::failure(EIO);
    if(socket>=0) {
        const wire::Io io{&socket,readSocket,writeSocket};
        if(upload(helper,io))result=wire::exchange(io,request,visit,context);
        sceNetSocketClose(socket);
    }
    sceKernelClose(helper);return result;
#else
    (void)visit;(void)context;
    Result r;r.code=Code::unavailable;r.error=ENOSYS;return r;
#endif
}
}
bool available() noexcept {
#ifdef OPENNOW_PS5
    const auto now=sceKernelGetProcessTime();
    bool expected=false;
    if(now>=cacheUntil.load(std::memory_order_relaxed)&&probeRunning.compare_exchange_strong(expected,true,std::memory_order_acquire)) {
        pthread_attr_t attributes;
        const int initialized=pthread_attr_init(&attributes);
        pthread_t thread;
        const bool started=initialized==0&&pthread_attr_setdetachstate(&attributes,PTHREAD_CREATE_DETACHED)==0&&
            pthread_create(&thread,&attributes,probe,nullptr)==0;
        if(initialized==0)pthread_attr_destroy(&attributes);
        if(!started) {
            cacheUntil.store(now+5'000'000,std::memory_order_relaxed);
            probeRunning.store(false,std::memory_order_release);
        }
    }
    return cachedAvailable.load(std::memory_order_relaxed);
#else
    return false;
#endif
}
Result install(const char* stageName) noexcept {return submit(wire::Operation::install,stageName);}
Result list(Visit visit,void* context) noexcept {return submit(wire::Operation::list,nullptr,visit,context);}
Result remove(const char* titleId) noexcept {return submit(wire::Operation::remove,titleId);}
}
