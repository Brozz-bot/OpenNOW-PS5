// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../src/forwarder/installer_protocol.hpp"
#include "../../src/forwarder/sandbox.hpp"
#include "../../src/forwarder/tile_store.hpp"
#include <cstdio>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace {
using namespace opennow::forwarder;
namespace wire=installer::wire;
struct Listing {
    const wire::Io& io;
    std::uint32_t id;
    unsigned count=0;
    int error=0;
    bool connected=true;
};
bool emit(void* context,const Entry& entry) noexcept {
    auto& listing=*static_cast<Listing*>(context);
    if(listing.count==wire::maxEntries){listing.error=EOVERFLOW;return false;}
    wire::Frame frame;
    if(!wire::encode(entry,listing.id,frame)){listing.error=EPROTO;return false;}
    if(!wire::send(listing.io,frame)){listing.connected=false;return false;}
    ++listing.count;return true;
}
Result execute(const wire::Request& request,const wire::Io& io,bool& connected) noexcept {
    char root[1024]{};
    switch(sandbox::resolve(request.pid,root,sizeof(root))) {
    case sandbox::Status::invalidPid:case sandbox::Status::wrongTitle: {
        Result result;result.code=Code::invalidGame;result.error=EPERM;return result;
    }
    case sandbox::Status::unavailable:case sandbox::Status::invalidRoot: {
        Result result;result.code=Code::unavailable;return result;
    }
    case sandbox::Status::ok:break;
    }
    switch(request.operation) {
    case wire::Operation::install: {
        char source[1024];
        const int length=std::snprintf(source,sizeof(source),"%s/download0/opennow/tile-stage/%s",root,request.argument);
        if(length<0||static_cast<std::size_t>(length)>=sizeof(source))return wire::failure(ENAMETOOLONG);
        return store::install(source);
    }
    case wire::Operation::list: {
        Listing listing{io,request.id};
        auto result=store::list(emit,&listing);
        connected=listing.connected;
        if(listing.error)result=wire::failure(listing.error);
        result.count=listing.count;return result;
    }
    case wire::Operation::remove:return store::remove(request.argument);
    }
    return wire::failure(EINVAL);
}
bool serve(const wire::Io& io) noexcept {
    wire::Frame frame;wire::Request request;
    if(!wire::receive(io,frame)||!wire::decode(frame,request))return false;
    bool connected=true;
    const auto result=execute(request,io,connected);
    return connected&&wire::encode(result,request.id,frame)&&wire::send(io,frame);
}
std::int64_t readInput(void*,void* bytes,std::size_t size) noexcept {return read(STDIN_FILENO,bytes,size);}
std::int64_t writeOutput(void*,const void* bytes,std::size_t size) noexcept {return write(STDOUT_FILENO,bytes,size);}
}
int main() {
    const timeval timeout{installer::wire::ioTimeoutUs/1'000'000,installer::wire::ioTimeoutUs%1'000'000};
    if(setsockopt(STDIN_FILENO,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))!=0||
       setsockopt(STDOUT_FILENO,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout))!=0)return 1;
    return serve({nullptr,readInput,writeOutput})?0:1;
}
