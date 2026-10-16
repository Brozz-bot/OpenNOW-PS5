// SPDX-License-Identifier: GPL-3.0-or-later
// Keyboard and mouse wire formats adapted from OpenNOW Android StreamInputEncoder.kt, MIT, Copyright (c) 2025 Zortos (assets/licenses/OpenNOW-Android.txt).
// Gamepad events, sensitivity and haptic decoding adapted from OpenNOW native streamer, same MIT notice.
#pragma once
#include "../input/gamepad.hpp"
#include "../input/rumble.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace opennow::wire {
using Bytes=std::vector<std::uint8_t>;
inline void le(Bytes& b,std::uint64_t n,unsigned bytes){while(bytes--){b.push_back(static_cast<std::uint8_t>(n));n>>=8;}}
inline void le32(Bytes& b,std::uint32_t n){le(b,n,4);}
inline void be(Bytes& b,std::uint64_t n,unsigned bytes){while(bytes){--bytes;b.push_back(static_cast<std::uint8_t>(n>>(bytes*8)));}}
inline Bytes single(const Bytes& payload,int protocol,std::uint64_t now){
    if(protocol<=2)return payload;
    Bytes wire{0x23};be(wire,now,8);wire.push_back(0x22);
    wire.insert(wire.end(),payload.begin(),payload.end());
    return wire;
}
inline Bytes batch(const Bytes& payload,int protocol,std::uint64_t now){
    if(protocol<=2)return payload;
    Bytes wire{0x23};be(wire,now,8);wire.push_back(0x21);be(wire,payload.size(),2);
    wire.insert(wire.end(),payload.begin(),payload.end());
    return wire;
}
inline Bytes gamepad(const GamepadState& state,int protocol,std::uint64_t now){
    Bytes p;le(p,12,4);le(p,26,2);le(p,0,2);le(p,state.connected?0x0101:0,2);le(p,20,2);
    le(p,state.buttons,2);le(p,state.triggers,2);le(p,static_cast<std::uint16_t>(state.lx),2);
    le(p,static_cast<std::uint16_t>(state.ly),2);le(p,static_cast<std::uint16_t>(state.rx),2);le(p,static_cast<std::uint16_t>(state.ry),2);
    le(p,0,2);le(p,85,2);le(p,0,2);le(p,now,8);
    return batch(p,protocol,now);
}
inline Bytes key(bool down,std::uint16_t vk,std::uint16_t scan,std::uint16_t modifiers,int protocol,std::uint64_t now){
    Bytes p;le32(p,down?3:4);be(p,vk,2);be(p,modifiers,2);be(p,scan,2);be(p,now,8);
    return single(p,protocol,now);
}
inline Bytes mouseButton(bool down,std::uint8_t button,int protocol,std::uint64_t now){
    Bytes p;le32(p,down?8:9);p.push_back(button);p.push_back(0);be(p,0,4);be(p,now,8);
    return single(p,protocol,now);
}
inline Bytes mouseMove(std::int16_t dx,std::int16_t dy,int protocol,std::uint64_t now,int sensitivityPercent=100){
    const auto scale=[sensitivityPercent](std::int16_t delta){return static_cast<std::uint16_t>(static_cast<std::int16_t>(
        std::clamp(std::round(static_cast<double>(delta)*sensitivityPercent/100.0),-32768.0,32767.0)));};
    Bytes p;le32(p,7);be(p,scale(dx),2);be(p,scale(dy),2);be(p,0,2);be(p,0,4);be(p,now,8);
    return batch(p,protocol,now);
}
inline Bytes mouseWheel(std::int16_t dx,std::int16_t dy,int protocol,std::uint64_t now){
    Bytes p;le32(p,10);be(p,static_cast<std::uint16_t>(dx),2);be(p,static_cast<std::uint16_t>(dy),2);be(p,0,2);be(p,0,4);be(p,now,8);
    return single(p,protocol,now);
}
inline RumbleBatch decodeRumble(const std::uint8_t* bytes,std::size_t size) noexcept {
    RumbleBatch commands{};
    if(!bytes)return commands;
    const auto u16=[](const std::uint8_t* p){return static_cast<std::uint16_t>(p[0]|(p[1]<<8));};
    while(size>=4){
        const auto code=u16(bytes),length=u16(bytes+2);
        if(length>size-4)break;
        const auto* payload=bytes+4;
        bytes+=4+length;size-=4+length;
        if(code!=0x010b||length<4)continue;
        const auto kind=u16(payload);
        if(kind!=1&&kind!=2)continue;
        const unsigned recordLength=kind==1?6:8;
        const auto available=std::min<unsigned>(u16(payload+2),length-4);
        for(unsigned at=0;at+recordLength<=available;at+=recordLength){
            const auto* record=payload+4+at;
            const auto id=u16(record);
            if(id>=commands.size())continue;
            const auto duration=kind==2?u16(record+6):0;
            commands[id]=Rumble{static_cast<std::uint8_t>(id),u16(record+2),u16(record+4),
                                static_cast<std::uint32_t>(duration?duration:1000)};
        }
    }
    return commands;
}
}
