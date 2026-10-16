// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace opennow::forwarder {
enum class LaunchArgsStatus { none, valid, invalid };
struct LaunchArgs {
    LaunchArgsStatus status=LaunchArgsStatus::none;
    char appId[96]{};
    char store[48]{};
};
LaunchArgs parseLaunchArgs(int argc,const char* const argv[]) noexcept;
}
