// SPDX-License-Identifier: GPL-3.0-or-later
#include "sandbox.hpp"
#include <climits>
#include <cstdlib>
#include <cstring>

#ifdef OPENNOW_PS5
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#endif

namespace opennow::forwarder::sandbox {
namespace {
constexpr char targetTitle[]="PPSA99082";
constexpr char rootPrefix[]="/mnt/sandbox/PPSA99082_";
constexpr std::size_t rootLength=sizeof(rootPrefix)-1+3;
constexpr std::size_t maximumSnapshot=1024*1024;

#ifdef OPENNOW_PS5
using FileInfo=kinfo_file;
constexpr int vnodeType=KF_TYPE_VNODE,directoryType=KF_VTYPE_VDIR,rootFd=KF_FD_TYPE_ROOT;
constexpr unsigned validAttributes=KF_ATTR_VALID;

struct AppInfo {
    std::uint32_t appId;
    std::uint64_t unknown1;
    std::uint32_t appType;
    char titleId[10];
    char unknown2[0x3c];
};
static_assert(offsetof(AppInfo,titleId)==0x14&&sizeof(AppInfo)==0x60);
extern "C" int sceKernelGetAppInfo(pid_t,AppInfo*);

bool readIdentity(void*,std::uint32_t pid,detail::Identity& identity) noexcept {
    AppInfo info{};
    if(sceKernelGetAppInfo(static_cast<pid_t>(pid),&info)!=0)return false;
    identity.appId=info.appId;
    std::memcpy(identity.titleId,info.titleId,sizeof(identity.titleId));
    return true;
}
bool readFiles(void*,std::uint32_t pid,void* data,std::size_t& size) noexcept {
    const int mib[]={CTL_KERN,KERN_PROC,KERN_PROC_FILEDESC,static_cast<int>(pid)};
    return sysctl(mib,4,data,&size,nullptr,0)==0;
}
#else
using FileInfo=testing::FileInfo;
constexpr int vnodeType=1,directoryType=2,rootFd=-2;
constexpr unsigned validAttributes=1;
#endif

Status parseFiles(const unsigned char* data,std::size_t size,char (&path)[rootLength+1]) noexcept {
    constexpr auto pathOffset=offsetof(FileInfo,kf_path);
    bool found=false;
    for(std::size_t offset=0;offset<size;) {
        std::int32_t length=0;
        if(size-offset<sizeof(length))return Status::invalidRoot;
        std::memcpy(&length,data+offset,sizeof(length));
        if(length<=0)return Status::invalidRoot;
        const auto recordSize=static_cast<std::size_t>(length);
        if(recordSize<=pathOffset||recordSize>sizeof(FileInfo)||recordSize>size-offset)
            return Status::invalidRoot;
        FileInfo file{};
        std::memcpy(&file,data+offset,pathOffset);
        if(file.kf_fd==rootFd) {
            if(found||file.kf_type!=vnodeType||file.kf_vnode_type!=directoryType||
               !(file.kf_status&validAttributes))return Status::invalidRoot;
            const auto* candidate=reinterpret_cast<const char*>(data+offset+pathOffset);
            const auto available=recordSize-pathOffset;
            const auto* end=static_cast<const char*>(std::memchr(candidate,'\0',available));
            if(!end||static_cast<std::size_t>(end-candidate)!=rootLength||
               std::memcmp(candidate,rootPrefix,sizeof(rootPrefix)-1)!=0)
                return Status::invalidRoot;
            for(std::size_t i=sizeof(rootPrefix)-1;i<rootLength;++i)
                if(candidate[i]<'0'||candidate[i]>'9')return Status::invalidRoot;
            std::memcpy(path,candidate,rootLength+1);
            found=true;
        }
        offset+=recordSize;
    }
    return found?Status::ok:Status::invalidRoot;
}

Status resolveWith(std::uint32_t pid,char* root,std::size_t capacity,void* context,
                   detail::ReadIdentity identity,detail::ReadFiles files) noexcept {
    if(root&&capacity)root[0]='\0';
    if(pid==0||pid>INT_MAX)return Status::invalidPid;
    if(!root||capacity<=rootLength)return Status::invalidRoot;
    if(!identity||!files)return Status::unavailable;
    detail::Identity before{};
    if(!identity(context,pid,before))return Status::unavailable;
    if(std::memcmp(before.titleId,targetTitle,sizeof(targetTitle)))return Status::wrongTitle;
    std::size_t size=0;
    if(!files(context,pid,nullptr,size))return Status::unavailable;
    if(!size)return Status::invalidRoot;
    if(size>maximumSnapshot)return Status::unavailable;
    auto* data=static_cast<unsigned char*>(std::malloc(size));
    if(!data)return Status::unavailable;
    const auto allocated=size;
    char path[rootLength+1]{};
    Status status=Status::unavailable;
    if(files(context,pid,data,size))
        status=size<=allocated?parseFiles(data,size,path):Status::invalidRoot;
    std::free(data);
    if(status!=Status::ok)return status;
    detail::Identity after{};
    if(!identity(context,pid,after))return Status::unavailable;
    if(std::memcmp(after.titleId,targetTitle,sizeof(targetTitle)))return Status::wrongTitle;
    if(after.appId!=before.appId)return Status::invalidPid;
    std::memcpy(root,path,sizeof(path));
    return Status::ok;
}
}

Status resolve(std::uint32_t pid,char* root,std::size_t capacity) noexcept {
#ifdef OPENNOW_PS5
    return resolveWith(pid,root,capacity,nullptr,readIdentity,readFiles);
#else
    return resolveWith(pid,root,capacity,nullptr,nullptr,nullptr);
#endif
}

#ifndef OPENNOW_PS5
Status testing::resolve(std::uint32_t pid,char* root,std::size_t capacity,const Source& source) noexcept {
    return resolveWith(pid,root,capacity,source.context,source.identity,source.files);
}
#endif
}
