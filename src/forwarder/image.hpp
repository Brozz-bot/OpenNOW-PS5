// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace opennow::forwarder {
using ImageWrite=bool (*)(void* context,const void* data,std::size_t size) noexcept;

bool encodeIconPng(const std::uint32_t* rgba,unsigned width,unsigned height,
                   ImageWrite write,void* context) noexcept;
bool encodeBackgroundDds(const std::uint32_t* rgba,unsigned sourceWidth,unsigned sourceHeight,
                         ImageWrite write,void* context,const std::atomic_bool* cancel=nullptr) noexcept;
}
