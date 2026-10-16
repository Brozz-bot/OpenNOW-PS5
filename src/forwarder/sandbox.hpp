// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>

namespace opennow::forwarder::sandbox {
enum class Status { ok, invalidPid, wrongTitle, unavailable, invalidRoot };
Status resolve(std::uint32_t pid,char* root,std::size_t capacity) noexcept;

namespace detail {
struct Identity {
    std::uint32_t appId{};
    char titleId[10]{};
};
using ReadIdentity=bool (*)(void*,std::uint32_t,Identity&) noexcept;
using ReadFiles=bool (*)(void*,std::uint32_t,void*,std::size_t&) noexcept;
}

#ifndef OPENNOW_PS5
namespace testing {
struct FileInfo {
    std::int32_t kf_structsize;
    std::int32_t kf_type;
    std::int32_t kf_fd;
    std::int32_t kf_vnode_type;
    std::uint16_t kf_status;
    char kf_path[1024];
};
struct Source {
    void* context{};
    detail::ReadIdentity identity{};
    detail::ReadFiles files{};
};
Status resolve(std::uint32_t pid,char* root,std::size_t capacity,const Source&) noexcept;
}
#endif
}
