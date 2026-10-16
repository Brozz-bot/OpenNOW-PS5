// SPDX-License-Identifier: GPL-3.0-or-later
#include "forwarder/sandbox.hpp"
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace sandbox=opennow::forwarder::sandbox;
using sandbox::Status;
using File=sandbox::testing::FileInfo;

struct Fixture {
    std::uint32_t pid=481;
    sandbox::detail::Identity first{7,"PPSA99082"},last=first;
    std::vector<unsigned char> records;
    unsigned identityCalls=0,filesCalls=0,failIdentityCall=0;
    bool identityAvailable=true,queryAvailable=true,readAvailable=true;
    std::size_t querySize=0,reportedSize=0;

    sandbox::testing::Source source() noexcept {return {this,identity,files};}
    static bool identity(void* context,std::uint32_t pid,sandbox::detail::Identity& output) noexcept {
        auto& f=*static_cast<Fixture*>(context);
        assert(pid==f.pid);
        output=f.identityCalls++?f.last:f.first;
        return f.identityAvailable&&f.identityCalls!=f.failIdentityCall;
    }
    static bool files(void* context,std::uint32_t pid,void* data,std::size_t& size) noexcept {
        auto& f=*static_cast<Fixture*>(context);
        assert(pid==f.pid);
        ++f.filesCalls;
        if(!data) {size=f.querySize?f.querySize:f.records.size();return f.queryAvailable;}
        if(f.records.size()>size)return false;
        std::memcpy(data,f.records.data(),f.records.size());
        size=f.reportedSize?f.reportedSize:f.records.size();
        return f.readAvailable;
    }
    void add(const char* path,int descriptor=-2) {
        File file{};
        file.kf_type=1;
        file.kf_fd=descriptor;
        file.kf_vnode_type=2;
        file.kf_status=1;
        assert(std::strlen(path)<sizeof(file.kf_path));
        std::strcpy(file.kf_path,path);
        file.kf_structsize=static_cast<std::int32_t>(offsetof(File,kf_path)+std::strlen(path)+1);
        const auto* bytes=reinterpret_cast<const unsigned char*>(&file);
        records.insert(records.end(),bytes,bytes+file.kf_structsize);
    }
    void setInt(std::size_t offset,std::int32_t value) {
        assert(offset+sizeof(value)<=records.size());
        std::memcpy(records.data()+offset,&value,sizeof(value));
    }
};

static void expect(Fixture fixture,Status expected,std::uint32_t pid=481) {
    char output[64]="must be cleared";
    assert(sandbox::testing::resolve(pid,output,sizeof(output),fixture.source())==expected);
    if(expected==Status::ok) {
        assert(std::strcmp(output,"/mnt/sandbox/PPSA99082_007")==0);
        assert(fixture.identityCalls==2&&fixture.filesCalls==2);
    } else assert(output[0]=='\0');
}

int main() {
    Fixture valid;
    valid.add("/mnt/sandbox/PPSA99082_999/download0/.instance-481",19);
    valid.add("/mnt/sandbox/PPSA99082_007");
    valid.add("/mnt/sandbox/PPSA99082_999",-3);
    expect(valid,Status::ok);
    expect({},Status::invalidPid,0);
    expect({},Status::invalidPid,static_cast<std::uint32_t>(INT_MAX)+1);
    expect({},Status::invalidPid,UINT32_MAX);
    Fixture missing;
    expect(missing,Status::invalidRoot);
    missing.add("/mnt/sandbox/PPSA99082_007/download0/.instance-481",-1);
    missing.add("/mnt/sandbox/PPSA99082_007",-3);
    expect(missing,Status::invalidRoot);
    Fixture wrong=valid;
    std::strcpy(wrong.first.titleId,"PPSA12345");
    expect(wrong,Status::wrongTitle);
    wrong=valid;
    std::memset(wrong.first.titleId,'X',sizeof(wrong.first.titleId));
    expect(wrong,Status::wrongTitle);
    wrong=valid;
    wrong.first.titleId[9]='X';
    expect(wrong,Status::wrongTitle);
    for(const char* path:{"","/","/mnt/sandbox/PPSA12345_007","/mnt/sandbox/PPSA99082_",
        "/mnt/sandbox/PPSA99082_07","/mnt/sandbox/PPSA99082_0007","/mnt/sandbox/PPSA99082_00x",
        "/mnt/sandbox/PPSA99082_+07","/mnt/sandbox/PPSA99082_007/","/mnt/sandbox/PPSA99082_007/app0",
        "/mnt/sandbox/PPSA99082_007/../PPSA99082_999","/mnt/sandbox/PPSA99082_007\n"}) {
        Fixture f;f.add(path);expect(f,Status::invalidRoot);
    }
    for(const char* other:{"/mnt/sandbox/PPSA99082_007","/mnt/sandbox/PPSA99082_008"}) {
        Fixture f=valid;f.add(other);expect(f,Status::invalidRoot);
    }
    for(auto offset:{offsetof(File,kf_type),offsetof(File,kf_vnode_type)}) {
        Fixture f;f.add("/mnt/sandbox/PPSA99082_007");f.setInt(offset,0);expect(f,Status::invalidRoot);
    }
    Fixture attributes;attributes.add("/mnt/sandbox/PPSA99082_007");
    std::memset(attributes.records.data()+offsetof(File,kf_status),0,sizeof(File::kf_status));
    expect(attributes,Status::invalidRoot);
    for(auto length:{-1,0,1,static_cast<int>(offsetof(File,kf_path)),INT_MAX}) {
        Fixture f;f.add("/mnt/sandbox/PPSA99082_007");f.setInt(0,length);expect(f,Status::invalidRoot);
    }
    Fixture single;single.add("/mnt/sandbox/PPSA99082_007");
    for(std::size_t size=1;size<single.records.size();++size) {
        Fixture f=single;f.records.resize(size);expect(f,Status::invalidRoot);
    }
    Fixture unterminated=single;unterminated.records.back()='x';expect(unterminated,Status::invalidRoot);
    Fixture trailing=single;trailing.records.push_back(0);expect(trailing,Status::invalidRoot);
    Fixture later=valid;later.records.push_back(0);expect(later,Status::invalidRoot);
    Fixture gone=valid;gone.identityAvailable=false;expect(gone,Status::unavailable);
    Fixture exited=valid;exited.failIdentityCall=2;expect(exited,Status::unavailable);
    Fixture query=valid;query.queryAvailable=false;expect(query,Status::unavailable);
    Fixture read=valid;read.readAvailable=false;expect(read,Status::unavailable);
    Fixture oversized=valid;oversized.querySize=1024*1024+1;expect(oversized,Status::unavailable);
    Fixture inflated=valid;inflated.reportedSize=valid.records.size()+1;expect(inflated,Status::invalidRoot);
    Fixture reused=valid;++reused.last.appId;expect(reused,Status::invalidPid);
    Fixture retitled=valid;std::strcpy(retitled.last.titleId,"PPSA12345");expect(retitled,Status::wrongTitle);
    for(std::size_t size=0;size<27;++size) {
        Fixture f=valid;
        char root[64]="unchanged";
        assert(sandbox::testing::resolve(f.pid,root,size,f.source())==Status::invalidRoot);
        assert(!f.identityCalls&&!f.filesCalls);
        if(size)assert(!root[0]);else assert(!std::strcmp(root,"unchanged"));
    }
    Fixture f=valid;
    assert(sandbox::testing::resolve(f.pid,nullptr,64,f.source())==Status::invalidRoot);
    char exact[27];
    assert(sandbox::testing::resolve(f.pid,exact,sizeof(exact),f.source())==Status::ok);
    assert(!std::strcmp(exact,"/mnt/sandbox/PPSA99082_007"));
    char root[64]="unchanged";
    assert(sandbox::resolve(481,root,sizeof(root))==Status::unavailable&&!root[0]);
    assert(sandbox::testing::resolve(481,root,sizeof(root),{})==Status::unavailable&&!root[0]);
    Fixture mismatch=valid;std::strcpy(mismatch.first.titleId,"PPSA12345");
    assert(sandbox::testing::resolve(mismatch.pid,root,sizeof(root),mismatch.source())==Status::wrongTitle);
    assert(mismatch.identityCalls==1&&!mismatch.filesCalls);
    std::puts("sandbox resolver tests passed");
}
