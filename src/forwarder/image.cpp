// SPDX-License-Identifier: GPL-3.0-or-later
// BC7 encoder and DDS header adapted from ps5-forwarder-manager/src/fwd/image.cpp.
// Copyright (C) 2026 heydemoura
// Source: https://github.com/heydemoura/ps5-forwarder-manager
// Revision: 1fce14a7519d963a18f4b048a08bcea4cef59d0d
// Upstream ports https://ps5-forwarder.mph.am bundled module 1212.
#include "image.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace opennow::forwarder {
namespace {
constexpr unsigned iconSize=512,backgroundWidth=3840,backgroundHeight=2160;
constexpr unsigned maxSourceDimension=2048;
constexpr int weights[16]={0,4,9,13,17,21,26,30,34,38,43,47,51,56,60,64};

void putBig32(unsigned char* out,std::uint32_t value) noexcept {
    for(unsigned i=0;i<4;++i)out[i]=static_cast<unsigned char>(value>>(24-i*8));
}

void putLittle32(unsigned char* out,std::uint32_t value) noexcept {
    for(unsigned i=0;i<4;++i)out[i]=static_cast<unsigned char>(value>>(i*8));
}

std::uint32_t crc32(std::uint32_t crc,const unsigned char* data,std::size_t size) noexcept {
    for(std::size_t i=0;i<size;++i) {
        crc^=data[i];
        for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0xedb88320U:0U);
    }
    return crc;
}

bool pngChunk(ImageWrite write,void* context,const char* type,const unsigned char* data,unsigned size) noexcept {
    unsigned char header[8],checksum[4];
    putBig32(header,size);
    std::memcpy(header+4,type,4);
    const auto crc=crc32(crc32(0xffffffffU,header+4,4),data,size)^0xffffffffU;
    putBig32(checksum,crc);
    return write(context,header,sizeof(header))&&(!size||write(context,data,size))&&
        write(context,checksum,sizeof(checksum));
}

int fitEndpoint(const int input[4],int endpoint[4],int reconstructed[4]) noexcept {
    int parity=0;
    long long best=-1;
    for(int p=0;p<=1;++p) {
        long long error=0;
        for(unsigned channel=0;channel<4;++channel) {
            const int value=std::clamp((input[channel]-p+1)>>1,0,127);
            error+=std::abs(2*value+p-input[channel]);
        }
        if(best<0||error<best) {best=error;parity=p;}
    }
    for(unsigned channel=0;channel<4;++channel) {
        endpoint[channel]=std::clamp((input[channel]-parity+1)>>1,0,127);
        reconstructed[channel]=2*endpoint[channel]+parity;
    }
    return parity;
}

void encodeBlock(const int texels[64],unsigned char out[16]) noexcept {
    int minimum[4]={255,255,255,255},maximum[4]={0,0,0,0};
    for(unsigned texel=0;texel<16;++texel) {
        for(unsigned channel=0;channel<4;++channel) {
            const int value=texels[4*texel+channel];
            minimum[channel]=std::min(minimum[channel],value);
            maximum[channel]=std::max(maximum[channel],value);
        }
    }
    int endpoint0[4],endpoint1[4],reconstructed0[4],reconstructed1[4];
    int parity0=fitEndpoint(minimum,endpoint0,reconstructed0);
    int parity1=fitEndpoint(maximum,endpoint1,reconstructed1);
    int palette[64];
    for(unsigned entry=0;entry<16;++entry) {
        for(unsigned channel=0;channel<4;++channel)
            palette[4*entry+channel]=(reconstructed0[channel]*(64-weights[entry])+
                reconstructed1[channel]*weights[entry]+32)>>6;
    }
    int indices[16];
    for(unsigned texel=0;texel<16;++texel) {
        int best=0;
        long long bestDistance=-1;
        for(unsigned entry=0;entry<16;++entry) {
            long long distance=0;
            for(unsigned channel=0;channel<4;++channel) {
                const int difference=texels[4*texel+channel]-palette[4*entry+channel];
                distance+=static_cast<long long>(difference)*difference;
            }
            if(bestDistance<0||distance<bestDistance) {bestDistance=distance;best=static_cast<int>(entry);}
        }
        indices[texel]=best;
    }
    if(indices[0]>=8) {
        for(auto& index:indices)index=15-index;
        for(unsigned channel=0;channel<4;++channel)std::swap(endpoint0[channel],endpoint1[channel]);
        std::swap(parity0,parity1);
    }
    std::memset(out,0,16);
    unsigned position=0;
    const auto bits=[&](int value,unsigned count) noexcept {
        for(unsigned bit=0;bit<count;++bit)
            if((value>>bit)&1)out[(position+bit)>>3]|=static_cast<unsigned char>(1U<<((position+bit)&7));
        position+=count;
    };
    bits(64,7);
    for(unsigned channel=0;channel<4;++channel) {bits(endpoint0[channel],7);bits(endpoint1[channel],7);}
    bits(parity0,1);
    bits(parity1,1);
    bits(indices[0],3);
    for(unsigned texel=1;texel<16;++texel)bits(indices[texel],4);
}

struct Sample {
    unsigned first,second;
    float fraction;
};

Sample sampleAt(unsigned position,unsigned size,float offset,float scale) noexcept {
    const float coordinate=std::clamp((static_cast<float>(position)+0.5f+offset)/scale-0.5f,
                                     0.0f,static_cast<float>(size-1));
    const auto first=static_cast<unsigned>(coordinate);
    return {first,std::min(first+1,size-1),coordinate-static_cast<float>(first)};
}
}

bool encodeIconPng(const std::uint32_t* rgba,unsigned width,unsigned height,
                   ImageWrite write,void* context) noexcept {
    if(!rgba||!write||width!=iconSize||height!=iconSize)return false;
    constexpr unsigned char signature[]={137,80,78,71,13,10,26,10};
    unsigned char ihdr[13]{};
    putBig32(ihdr,width);
    putBig32(ihdr+4,height);
    ihdr[8]=8;ihdr[9]=6;
    if(!write(context,signature,sizeof(signature))||!pngChunk(write,context,"IHDR",ihdr,sizeof(ihdr)))return false;
    constexpr unsigned scanlineSize=1+iconSize*4;
    unsigned char row[2+5+scanlineSize+4];
    std::uint32_t adler1=1,adler2=0;
    for(unsigned y=0;y<height;++y) {
        unsigned position=0;
        if(y==0) {row[position++]=0x78;row[position++]=0x01;}
        row[position++]=y+1==height?1:0;
        row[position++]=static_cast<unsigned char>(scanlineSize);
        row[position++]=static_cast<unsigned char>(scanlineSize>>8);
        row[position++]=static_cast<unsigned char>(scanlineSize^0xffffU);
        row[position++]=static_cast<unsigned char>((scanlineSize^0xffffU)>>8);
        const unsigned start=position;
        row[position++]=0;
        for(unsigned x=0;x<width;++x) {
            const auto pixel=rgba[std::size_t(y)*width+x];
            for(unsigned channel=0;channel<4;++channel)row[position++]=static_cast<unsigned char>(pixel>>(channel*8));
        }
        for(unsigned i=start;i<position;++i) {adler1+=row[i];adler2+=adler1;}
        adler1%=65521;adler2%=65521;
        if(y+1==height) {putBig32(row+position,(adler2<<16)|adler1);position+=4;}
        if(!pngChunk(write,context,"IDAT",row,position))return false;
    }
    return pngChunk(write,context,"IEND",nullptr,0);
}

bool encodeBackgroundDds(const std::uint32_t* rgba,unsigned sourceWidth,unsigned sourceHeight,
                         ImageWrite write,void* context,const std::atomic_bool* cancel) noexcept {
    if(!rgba||!write||!sourceWidth||!sourceHeight||sourceWidth>maxSourceDimension||
       sourceHeight>maxSourceDimension||(cancel&&cancel->load(std::memory_order_relaxed)))return false;
    constexpr std::uint32_t linearSize=backgroundWidth*backgroundHeight;
    unsigned char header[148]{};
    std::memcpy(header,"DDS ",4);
    putLittle32(header+4,124);
    putLittle32(header+8,0x81007);
    putLittle32(header+12,backgroundHeight);
    putLittle32(header+16,backgroundWidth);
    putLittle32(header+20,linearSize);
    putLittle32(header+28,1);
    putLittle32(header+76,32);
    putLittle32(header+80,4);
    std::memcpy(header+84,"DX10",4);
    putLittle32(header+108,0x1000);
    putLittle32(header+128,98);
    putLittle32(header+132,3);
    putLittle32(header+140,1);
    if(!write(context,header,sizeof(header)))return false;
    const float scale=std::max(static_cast<float>(backgroundWidth)/static_cast<float>(sourceWidth),
                               static_cast<float>(backgroundHeight)/static_cast<float>(sourceHeight));
    const float offsetX=(static_cast<float>(sourceWidth)*scale-static_cast<float>(backgroundWidth))/2;
    const float offsetY=(static_cast<float>(sourceHeight)*scale-static_cast<float>(backgroundHeight))/2;
    unsigned char row[backgroundWidth*4];
    for(unsigned by=0;by<backgroundHeight/4;++by) {
        Sample ys[4];
        for(unsigned y=0;y<4;++y)ys[y]=sampleAt(by*4+y,sourceHeight,offsetY,scale);
        for(unsigned bx=0;bx<backgroundWidth/4;++bx) {
            if(cancel&&cancel->load(std::memory_order_relaxed))return false;
            Sample xs[4];
            for(unsigned x=0;x<4;++x)xs[x]=sampleAt(bx*4+x,sourceWidth,offsetX,scale);
            int texels[64];
            for(unsigned y=0;y<4;++y) {
                for(unsigned x=0;x<4;++x) {
                    const auto topLeft=rgba[std::size_t(ys[y].first)*sourceWidth+xs[x].first];
                    const auto topRight=rgba[std::size_t(ys[y].first)*sourceWidth+xs[x].second];
                    const auto bottomLeft=rgba[std::size_t(ys[y].second)*sourceWidth+xs[x].first];
                    const auto bottomRight=rgba[std::size_t(ys[y].second)*sourceWidth+xs[x].second];
                    for(unsigned channel=0;channel<4;++channel) {
                        const unsigned shift=channel*8;
                        const float a=static_cast<float>((topLeft>>shift)&0xff);
                        const float b=static_cast<float>((topRight>>shift)&0xff);
                        const float c=static_cast<float>((bottomLeft>>shift)&0xff);
                        const float d=static_cast<float>((bottomRight>>shift)&0xff);
                        const float top=a+(b-a)*xs[x].fraction;
                        const float bottom=c+(d-c)*xs[x].fraction;
                        texels[(y*4+x)*4+channel]=static_cast<int>(top+(bottom-top)*ys[y].fraction+0.5f);
                    }
                }
            }
            encodeBlock(texels,row+bx*16);
        }
        if((cancel&&cancel->load(std::memory_order_relaxed))||!write(context,row,sizeof(row)))return false;
    }
    return !cancel||!cancel->load(std::memory_order_relaxed);
}
}
