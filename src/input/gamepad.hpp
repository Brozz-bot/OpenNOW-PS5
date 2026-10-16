// SPDX-License-Identifier: GPL-3.0-or-later
// Controller tuning adapted from OpenNOW ControllerInput.cpp, MIT, Copyright (c) 2025 Zortos (assets/licenses/OpenNOW-Android.txt).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
extern "C" {
#include "../platform/ps5-pad.h"
}

namespace opennow {
struct GamepadState {
    std::uint16_t buttons=0, triggers=0;
    std::int16_t lx=0, ly=0, rx=0, ry=0;
    bool connected=false;
    std::uint8_t neutral() const noexcept {return (lx==0&&ly==0?1:0)|(rx==0&&ry==0?2:0);}
};

inline GamepadState gamepadState(const PS5_PadData& pad,int leftDeadzonePercent,int rightDeadzonePercent) noexcept {
    GamepadState state;
    if(!pad.connected)return state;
    state.connected=true;
    constexpr unsigned ps[]={PS5_PAD_BUTTON_UP,PS5_PAD_BUTTON_DOWN,PS5_PAD_BUTTON_LEFT,PS5_PAD_BUTTON_RIGHT,
                             PS5_PAD_BUTTON_OPTIONS,PS5_PAD_BUTTON_TOUCH_PAD,PS5_PAD_BUTTON_L3,PS5_PAD_BUTTON_R3,
                             PS5_PAD_BUTTON_L1,PS5_PAD_BUTTON_R1,PS5_PAD_BUTTON_CROSS,PS5_PAD_BUTTON_CIRCLE,
                             PS5_PAD_BUTTON_SQUARE,PS5_PAD_BUTTON_TRIANGLE};
    constexpr std::uint16_t xb[]={1,2,4,8,16,32,64,128,256,512,4096,8192,16384,32768};
    for(unsigned i=0;i<14;++i)if(pad.buttons&ps[i])state.buttons|=xb[i];
    state.triggers=pad.analogButtons.l2|(pad.analogButtons.r2<<8);
    const auto stick=[](std::uint8_t x,std::uint8_t y,int percent) {
        using Stick=std::pair<std::int16_t,std::int16_t>;
        const auto normalize=[](std::uint8_t value){return (static_cast<int>(value)-128)/(value<128?128.0:127.0);};
        const double nx=normalize(x),ny=normalize(y),magnitude=std::hypot(nx,ny),deadzone=percent/100.0;
        if(magnitude<=deadzone)return Stick{};
        const double factor=(magnitude-deadzone)/((1.0-deadzone)*magnitude);
        return Stick{static_cast<std::int16_t>(std::round(std::clamp(nx*factor,-1.0,1.0)*32767.0)),
                     static_cast<std::int16_t>(-std::round(std::clamp(ny*factor,-1.0,1.0)*32767.0))};
    };
    const auto left=stick(pad.leftStick.x,pad.leftStick.y,leftDeadzonePercent);
    const auto right=stick(pad.rightStick.x,pad.rightStick.y,rightDeadzonePercent);
    state.lx=left.first;state.ly=left.second;state.rx=right.first;state.ry=right.second;
    return state;
}
}
