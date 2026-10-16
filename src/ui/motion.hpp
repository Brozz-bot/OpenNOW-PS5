// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>
#include <cstdint>

namespace opennow::ui {
// Immediate-mode animation state: critically damped springs advanced by
// wall-clock time, so motion looks the same at 60 Hz and 120 Hz.
class Motion {
public:
    void advance(std::uint64_t nowMicroseconds) noexcept {
        const double gap=last_?static_cast<double>(nowMicroseconds-last_)/1e6:0.0;
        dt_=!last_||gap>0.1?1.0f/60.0f:static_cast<float>(gap);
        last_=nowMicroseconds;
        clock_+=dt_;
        ++frame_;
        active_=false;
    }
    float follow(std::uint32_t key,float target,float omega,float initial,float epsilon) noexcept {
        Entry* slot=nullptr;
        Entry* oldest=&entries_[0];
        for(auto& entry:entries_) {
            if(entry.frame&&entry.key==key){slot=&entry;break;}
            if(entry.frame<oldest->frame)oldest=&entry;
        }
        if(slot&&slot->frame==frame_)return slot->value;
        if(!slot||slot->frame+1<frame_) {
            slot=slot?slot:oldest;
            slot->key=key;slot->value=initial;slot->velocity=0;
        }
        slot->frame=frame_;
        const float offset=slot->value-target, decay=std::exp(-omega*dt_);
        const float drive=slot->velocity+omega*offset;
        slot->value=target+(offset+drive*dt_)*decay;
        slot->velocity=(slot->velocity-omega*drive*dt_)*decay;
        if(std::abs(slot->value-target)<=epsilon&&std::abs(slot->velocity)<=epsilon*omega){slot->value=target;slot->velocity=0;}
        else active_=true;
        return slot->value;
    }
    void keepAlive() noexcept {active_=true;}
    bool animating() const noexcept {return active_;}
    float seconds() const noexcept {return clock_;}
private:
    struct Entry { std::uint32_t key=0; unsigned frame=0; float value=0,velocity=0; };
    Entry entries_[192]{};
    std::uint64_t last_=0;
    unsigned frame_=0;
    float dt_=1.0f/60.0f, clock_=0;
    bool active_=false;
};

constexpr std::uint32_t motionKey(std::uint32_t a,std::uint32_t b=0,std::uint32_t c=0) noexcept {
    std::uint32_t h=2166136261U;
    for(std::uint32_t v:{a,b,c})for(unsigned i=0;i<4;++i)h=(h^((v>>(i*8))&255U))*16777619U;
    return h;
}
}
