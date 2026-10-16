// SPDX-License-Identifier: GPL-3.0-or-later
#include "forwarder/launch_args.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

using namespace opennow::forwarder;
static LaunchArgs parse(std::initializer_list<const char*> args) {
    return parseLaunchArgs(static_cast<int>(args.size()),args.begin());
}
int main() {
    assert(parseLaunchArgs(0,nullptr).status==LaunchArgsStatus::none);
    assert(parse({"/app0/eboot.bin"}).status==LaunchArgsStatus::none);
    for(const auto& args:{parse({"--gfn-app-id","42"}),parse({"/app0/eboot.bin","--gfn-app-id","42"})}) {
        assert(args.status==LaunchArgsStatus::valid&&!std::strcmp(args.appId,"42")&&!*args.store);
    }
    for(const auto& args:{parse({"--gfn-app-id","42","--store","XBOX"}),
        parse({"PPSA99082","--store","XBOX","--gfn-app-id","42"})}) {
        assert(args.status==LaunchArgsStatus::valid&&!std::strcmp(args.appId,"42")&&!std::strcmp(args.store,"XBOX"));
    }
    for(const auto& args:{parse({"--gfn-app-id"}),parse({"--gfn-app-id",""}),parse({"--store","STEAM"}),
        parse({"--unknown","42"}),parse({"--gfn-app-id","42","extra"}),parse({"eboot","--gfn-app-id","42","--store"}),
        parse({"--gfn-app-id","42","--gfn-app-id","43"}),parse({"--store","STEAM","--store","XBOX"}),
        parse({"--gfn-app-id","42","--store",""}),parse({"--gfn-app-id","42","--store","STEAM\n"}),
        parse({"--gfn-app-id","42",nullptr})}) {
        assert(args.status==LaunchArgsStatus::invalid&&!*args.appId&&!*args.store);
    }
    for(const char* id:{"+42","-42","42x","4 2","4.2"," 42","42\n","\xD9\xA4\xD9\xA2"})assert(parse({"--gfn-app-id",id}).status==LaunchArgsStatus::invalid);
    const std::string id(95,'9'),store(47,'A');
    auto args=parse({"--gfn-app-id",id.c_str(),"--store",store.c_str()});
    assert(args.status==LaunchArgsStatus::valid&&std::strlen(args.appId)==95&&std::strlen(args.store)==47);
    assert(parse({"--gfn-app-id",(id+"9").c_str()}).status==LaunchArgsStatus::invalid);
    assert(parse({"--gfn-app-id","42","--store",(store+"A").c_str()}).status==LaunchArgsStatus::invalid);
    assert(parseLaunchArgs(1,nullptr).status==LaunchArgsStatus::invalid);
    assert(parseLaunchArgs(-1,nullptr).status==LaunchArgsStatus::invalid);
    assert(parseLaunchArgs(6,nullptr).status==LaunchArgsStatus::invalid);
    std::puts("launch argv tests passed");
}
