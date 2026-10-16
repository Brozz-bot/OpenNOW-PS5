// SPDX-License-Identifier: GPL-3.0-or-later
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

extern "C" int sysctlbyname(const char* name,void* output,std::size_t* length,const void*,std::size_t) {
    if(std::strcmp(name,"kern.rng_pseudo")||!output||!length){errno=EINVAL;return -1;}
    const int fd=::open("/dev/urandom",O_RDONLY);if(fd<0)return -1;
    std::size_t received=0;
    while(received<*length) {
        const auto n=::read(fd,static_cast<unsigned char*>(output)+received,*length-received);
        if(n<0&&errno==EINTR)continue;
        if(n<=0){::close(fd);return -1;}
        received+=static_cast<std::size_t>(n);
    }
    return ::close(fd);
}
