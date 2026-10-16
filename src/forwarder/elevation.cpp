// SPDX-License-Identifier: GPL-3.0-or-later
#include "forwarder.hpp"
#include "files.hpp"
#include "../app_storage.h"
#ifdef OPENNOW_PS5
#include "../../examples/sandbox-elevation/elevation.hpp"
#endif

namespace opennow::forwarder {
namespace {
bool writable=false,unsafe=false,lost=false;
bool checkAccess() noexcept {
    if(!files::mkdir("/data/homebrew"))return false;
    char path[128];
    std::snprintf(path,sizeof(path),"/data/homebrew/.opennow-access-%u",static_cast<unsigned>(getpid()));
    int fd=accountFile::open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    if(fd<0)return false;
    const bool ok=accountFile::close(fd)==0;
    return accountFile::remove(path)==0&&ok;
}
}
bool rebaseSandbox(const char* sandboxes,const char* marker) noexcept {
    char probe[1024];
    if(std::snprintf(probe,sizeof(probe),"%s/sce_sys/param.json",opennow_app_root())>0&&files::exists(probe))return true;
    files::Directory directory(sandboxes);
    char prefix[16];
    std::snprintf(prefix,sizeof(prefix),"%s_",targetTitleId);
    while(const char* name=directory.next()) {
        if(std::strncmp(name,prefix,std::strlen(prefix)))continue;
        char sandbox[1024];
        if(!files::join(sandbox,sandboxes,name))continue;
        const int n=std::snprintf(probe,sizeof(probe),"%s" OPENNOW_SANDBOX_STORAGE_ROOT "/%s",sandbox,marker);
        if(n<=0||static_cast<std::size_t>(n)>=sizeof(probe)||!files::exists(probe))continue;
        return opennow_rebase_sandbox(sandbox)==0;
    }
    return false;
}
Result initialize() noexcept {
    Result result;
    if(writable||checkAccess()){writable=true;result.code=Code::available;return result;}
#ifdef OPENNOW_PS5
    char marker[64],markerPath[1024];
    std::snprintf(marker,sizeof(marker),".instance-%u",static_cast<unsigned>(getpid()));
    if(!files::join(markerPath,OPENNOW_STORAGE_ROOT,marker)||(!files::exists(markerPath)&&!files::writeFile(markerPath,"1",1))) {
        result.error=errno;result.code=Code::unavailable;return result;
    }
    const auto status=elevation::request(elevation::Capability::filesystem,opennow_app_file("opennow-forwarder-elevator.elf"));
    unsafe=status==elevation::Status::transport_error||status==elevation::Status::protocol_error||
        status==elevation::Status::rollback_failed;
    lost=!rebaseSandbox("/mnt/sandbox",marker);
    if(!lost&&files::join(markerPath,OPENNOW_STORAGE_ROOT,marker))(void)accountFile::remove(markerPath);
    if(status==elevation::Status::ok&&!lost)writable=checkAccess();
    result.error=static_cast<int>(status);
#else
    result.error=errno;
#endif
    result.code=lost?Code::sandboxLost:unsafe?Code::unsafeElevation:(writable?Code::available:Code::unavailable);
    return result;
}
bool available() noexcept {return writable;}
bool fatalStartup() noexcept {return unsafe||lost;}
}
