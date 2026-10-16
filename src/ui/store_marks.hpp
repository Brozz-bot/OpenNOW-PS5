// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace opennow::ui::marks {
enum class Store : std::uint8_t { none, steam, epic, ubisoft, battlenet, xbox, gog, ea, nvidia, gaijin };

struct MarkShape {
    const float* points;
    const std::uint16_t* ends;
    unsigned contours;
    bool evenOdd;
};
#include "store_marks.inc"

inline Store storeKey(std::string_view value) noexcept {
    char key[48]{};
    std::size_t length=0;
    for(;length<value.size()&&length+1<sizeof(key);++length) {
        char c=value[length];
        if(c>='a'&&c<='z')c=static_cast<char>(c-32);
        key[length]=c==' '||c=='-'?'_':c;
    }
    const std::string_view k(key,length);
    const auto has=[&](std::string_view needle){return k.find(needle)!=std::string_view::npos;};
    if(has("STEAM"))return Store::steam;
    if(has("EPIC"))return Store::epic;
    if(has("UBISOFT")||has("UPLAY"))return Store::ubisoft;
    if(has("BATTLE")||has("BLIZZARD"))return Store::battlenet;
    if(has("XBOX")||has("MICROSOFT"))return Store::xbox;
    if(has("GOG"))return Store::gog;
    if(has("GAIJIN"))return Store::gaijin;
    if(k=="NVIDIA")return Store::nvidia;
    if(k=="EA"||k=="EA_APP"||k=="ORIGIN")return Store::ea;
    return Store::none;
}

constexpr unsigned maskSide=48;
struct Mask {
    Store store=Store::none;
    unsigned size=0;
    std::uint8_t pixels[maskSide*maskSide]{};
};

inline void rasterize(const MarkShape& shape, unsigned size, std::uint8_t* out) noexcept {
    constexpr unsigned samples=16, maxCrossings=512;
    const float scale=static_cast<float>(size)/24.0f;
    float coverage[maskSide];
    struct Crossing { float x; int direction; };
    Crossing crossings[maxCrossings];
    for(unsigned row=0;row<size;++row) {
        std::fill(coverage,coverage+size,0.0f);
        for(unsigned sample=0;sample<samples;++sample) {
            const float y=(static_cast<float>(row)+(static_cast<float>(sample)+0.5f)/samples)/scale;
            unsigned count=0;
            unsigned begin=0;
            for(unsigned contour=0;contour<shape.contours;++contour) {
                const unsigned end=shape.ends[contour];
                for(unsigned i=begin,j=end-1;i<end;j=i++) {
                    const float ax=shape.points[j*2],ay=shape.points[j*2+1],bx=shape.points[i*2],by=shape.points[i*2+1];
                    if((ay<=y)==(by<=y)||count==maxCrossings)continue;
                    crossings[count++]={(ax+(y-ay)*(bx-ax)/(by-ay))*scale,by>ay?1:-1};
                }
                begin=end;
            }
            std::sort(crossings,crossings+count,[](const Crossing& a,const Crossing& b){return a.x<b.x;});
            int winding=0;
            for(unsigned i=0;i+1<count;++i) {
                winding+=crossings[i].direction;
                const bool filled=shape.evenOdd?(i&1)==0:winding!=0;
                if(!filled)continue;
                const float from=std::clamp(crossings[i].x,0.0f,static_cast<float>(size));
                const float to=std::clamp(crossings[i+1].x,0.0f,static_cast<float>(size));
                if(to<=from)continue;
                const unsigned first=static_cast<unsigned>(from), last=std::min(static_cast<unsigned>(to),size-1);
                if(first==last){coverage[first]+=(to-from)/samples;continue;}
                coverage[first]+=(static_cast<float>(first+1)-from)/samples;
                for(unsigned x=first+1;x<last;++x)coverage[x]+=1.0f/samples;
                coverage[last]+=(to-static_cast<float>(last))/samples;
            }
        }
        for(unsigned x=0;x<size;++x)out[row*size+x]=static_cast<std::uint8_t>(std::min(coverage[x],1.0f)*255+0.5f);
    }
}

inline const Mask* mask(Store store,unsigned size) noexcept {
    if(store==Store::none||size<4||size>maskSide)return nullptr;
    static Mask cache[24];
    static unsigned next=0;
    for(const auto& entry:cache)if(entry.store==store&&entry.size==size)return &entry;
    Mask& entry=cache[next];
    next=(next+1)%std::size(cache);
    entry.store=store;entry.size=size;
    rasterize(markShapes[static_cast<unsigned>(store)-1],size,entry.pixels);
    return &entry;
}
}
