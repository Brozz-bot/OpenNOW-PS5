// SPDX-License-Identifier: GPL-3.0-or-later
// Motor intensity scaling adapted from OpenNOW ControllerInput.cpp, MIT, Copyright (c) 2025 Zortos (assets/licenses/OpenNOW-Android.txt).
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
extern "C" {
#include "../platform/ps5-pad.h"
}

namespace opennow {
struct Rumble {
    std::uint8_t controllerId=0;
    std::uint16_t lowFrequency=0, highFrequency=0;
    std::uint32_t durationMs=1000;
};
using RumbleBatch=std::array<std::optional<Rumble>,4>;

inline PS5_PadVibration rumbleMotors(const Rumble& command,int intensityPercent) noexcept {
    return {static_cast<std::uint8_t>((std::uint32_t(command.lowFrequency)*intensityPercent/100)>>8),
            static_cast<std::uint8_t>((std::uint32_t(command.highFrequency)*intensityPercent/100)>>8)};
}
}
