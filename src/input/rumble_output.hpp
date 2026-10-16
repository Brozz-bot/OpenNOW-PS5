// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "rumble.hpp"
#include <limits>

namespace opennow {
struct RumbleNotice {
    Rumble command{};
    std::uint64_t receivedAt=0,revision=0,session=0;
};
class RumbleOutput {
public:
    PS5_PadVibration update(const RumbleNotice& notice,std::uint64_t session,bool eligible,
                           int intensity,std::uint64_t now) noexcept {
        if(session_!=session) {session_=session;expires_=0;intensity_=intensity;}
        if(!eligible||intensity_!=intensity) {
            intensity_=intensity;seen_=notice.revision;expires_=0;return {};
        }
        if(seen_!=notice.revision) {
            seen_=notice.revision;
            if(notice.session==session&&notice.command.controllerId==0&&notice.receivedAt<=now) {
                const auto duration=std::uint64_t(notice.command.durationMs)*1000;
                expires_=notice.receivedAt>std::numeric_limits<std::uint64_t>::max()-duration?
                    std::numeric_limits<std::uint64_t>::max():notice.receivedAt+duration;
                motors_=rumbleMotors(notice.command,intensity);
            }
        }
        return now<expires_?motors_:PS5_PadVibration{};
    }
private:
    std::uint64_t session_=0,seen_=0,expires_=0;
    int intensity_=100;
    PS5_PadVibration motors_{};
};
}
