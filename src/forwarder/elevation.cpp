// SPDX-License-Identifier: GPL-3.0-or-later
#include "forwarder.hpp"
#include "files.hpp"
#ifdef OPENNOW_PS5
#include "../../examples/sandbox-elevation/elevation.hpp"
#endif

namespace opennow::forwarder {
namespace {
bool writable=false,unsafe=false;
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
Result initialize() noexcept {
    Result result;
    if(writable||checkAccess()){writable=true;result.code=Code::available;return result;}
#ifdef OPENNOW_PS5
    const auto status=elevation::request(elevation::Capability::filesystem,"/app0/opennow-forwarder-elevator.elf");
    unsafe=status==elevation::Status::transport_error||status==elevation::Status::protocol_error||
        status==elevation::Status::rollback_failed;
    if(status==elevation::Status::ok)writable=checkAccess();
    result.error=static_cast<int>(status);
#else
    result.error=errno;
#endif
    result.code=unsafe?Code::unsafeElevation:(writable?Code::available:Code::unavailable);
    return result;
}
bool available() noexcept {return writable;}
bool fatalStartup() noexcept {return unsafe;}
}
