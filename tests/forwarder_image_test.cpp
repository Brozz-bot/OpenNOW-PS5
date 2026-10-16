// SPDX-License-Identifier: GPL-3.0-or-later
#include "forwarder/image.hpp"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstring>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_SIMD
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wunused-parameter"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "vendor/stb_image.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

using namespace opennow::forwarder;
namespace {
struct Sink {
    std::vector<unsigned char> bytes;
    std::size_t calls=0,failAt=SIZE_MAX,largest=0;
    std::atomic_bool* cancel=nullptr;
    std::size_t cancelAfter=SIZE_MAX;
};

bool collect(void* context,const void* data,std::size_t size) noexcept {
    auto& sink=*static_cast<Sink*>(context);
    assert(data&&size);
    sink.largest=std::max(sink.largest,size);
    if(sink.calls++==sink.failAt)return false;
    const auto* bytes=static_cast<const unsigned char*>(data);
    sink.bytes.insert(sink.bytes.end(),bytes,bytes+size);
    if(sink.cancel&&sink.calls==sink.cancelAfter)sink.cancel->store(true);
    return true;
}

std::uint32_t big32(const unsigned char* p) {
    return (std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|(std::uint32_t(p[2])<<8)|p[3];
}

std::uint32_t little32(const unsigned char* p) {
    return p[0]|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);
}

std::uint32_t chunkCrc(const unsigned char* data,std::size_t size) {
    std::uint32_t table[256];
    for(unsigned byte=0;byte<256;++byte) {
        std::uint32_t value=byte;
        for(unsigned bit=0;bit<8;++bit)value=(value&1)?0xedb88320U^(value>>1):value>>1;
        table[byte]=value;
    }
    std::uint32_t crc=0xffffffffU;
    for(std::size_t i=0;i<size;++i)crc=table[(crc^data[i])&255]^(crc>>8);
    return crc^0xffffffffU;
}

void pngTest() {
    std::vector<std::uint32_t> pixels(512*512);
    for(unsigned y=0;y<512;++y)for(unsigned x=0;x<512;++x)
        pixels[y*512+x]=(x&255)|((y&255)<<8)|(((x+y)&255)<<16)|(((x*3+y*5)&255)<<24);
    Sink sink;
    assert(encodeIconPng(pixels.data(),512,512,collect,&sink));
    const unsigned char signature[]={137,80,78,71,13,10,26,10};
    assert(sink.bytes.size()>8&&!std::memcmp(sink.bytes.data(),signature,8));
    std::size_t position=8;
    unsigned chunks=0,idats=0;
    std::vector<unsigned char> deflate;
    while(position<sink.bytes.size()) {
        assert(sink.bytes.size()-position>=12);
        const auto* chunk=sink.bytes.data()+position;
        const auto size=big32(chunk);
        assert(size<=sink.bytes.size()-position-12);
        assert(chunkCrc(chunk+4,size+4)==big32(chunk+8+size));
        if(chunks==0) {
            assert(size==13&&!std::memcmp(chunk+4,"IHDR",4));
            assert(big32(chunk+8)==512&&big32(chunk+12)==512);
            const unsigned char format[]={8,6,0,0,0};
            assert(!std::memcmp(chunk+16,format,sizeof(format)));
        } else if(!std::memcmp(chunk+4,"IDAT",4)) {
            ++idats;
            deflate.insert(deflate.end(),chunk+8,chunk+8+size);
        } else {
            assert(!std::memcmp(chunk+4,"IEND",4)&&size==0);
            assert(position+12==sink.bytes.size());
        }
        position+=size+12;
        ++chunks;
    }
    assert(chunks==514&&idats==512);
    assert(sink.bytes.size()==8+25+512*(12+5+2049)+6+12);
    assert(deflate[0]==0x78&&deflate[1]==0x01);
    std::uint32_t adler1=1,adler2=0;
    position=2;
    for(unsigned y=0;y<512;++y) {
        assert(position+5+2049<=deflate.size());
        assert(deflate[position++]==(y==511?1:0));
        const unsigned size=deflate[position]|(unsigned(deflate[position+1])<<8);
        const unsigned inverse=deflate[position+2]|(unsigned(deflate[position+3])<<8);
        position+=4;
        assert(size==2049&&(size^inverse)==0xffff);
        assert(deflate[position]==0);
        for(unsigned i=0;i<size;++i) {
            adler1=(adler1+deflate[position++])%65521;
            adler2=(adler2+adler1)%65521;
        }
    }
    assert(position+4==deflate.size());
    assert(big32(deflate.data()+position)==((adler2<<16)|adler1));
    int width=0,height=0,channels=0;
    auto* decoded=stbi_load_from_memory(sink.bytes.data(),static_cast<int>(sink.bytes.size()),&width,&height,&channels,4);
    assert(decoded&&width==512&&height==512&&channels==4);
    for(std::size_t i=0;i<pixels.size();++i)for(unsigned channel=0;channel<4;++channel)
        assert(decoded[i*4+channel]==((pixels[i]>>(channel*8))&255));
    stbi_image_free(decoded);
    assert(sink.largest<=2060);
    for(const auto failure:{std::size_t(0),std::size_t(1),std::size_t(2),std::size_t(3),
                           std::size_t(4),std::size_t(5),std::size_t(6),sink.calls/2,sink.calls-2,sink.calls-1}) {
        Sink failed;failed.failAt=failure;
        assert(!encodeIconPng(pixels.data(),512,512,collect,&failed));
        assert(failed.calls==failure+1);
    }
}

unsigned readBits(const unsigned char* block,unsigned& position,unsigned count) {
    unsigned value=0;
    for(unsigned bit=0;bit<count;++bit,++position)value|=((block[position/8]>>(position%8))&1U)<<bit;
    return value;
}

void decodeBlock(const unsigned char* block,unsigned char out[64]) {
    unsigned position=0;
    assert(readBits(block,position,7)==64);
    unsigned endpoints[4][2];
    for(auto& channel:endpoints)for(auto& value:channel)value=readBits(block,position,7)*2;
    const unsigned p0=readBits(block,position,1),p1=readBits(block,position,1);
    for(auto& channel:endpoints) {channel[0]|=p0;channel[1]|=p1;}
    constexpr unsigned weights[]={0,4,9,13,17,21,26,30,34,38,43,47,51,55,60,64};
    for(unsigned texel=0;texel<16;++texel) {
        const auto index=readBits(block,position,texel==0?3:4);
        for(unsigned channel=0;channel<4;++channel)
            out[texel*4+channel]=static_cast<unsigned char>((endpoints[channel][0]*(64-weights[index])+
                endpoints[channel][1]*weights[index]+32)/64);
    }
    assert(position==128);
}

void ddsHeader(const Sink& sink) {
    assert(sink.bytes.size()==148+3840*2160);
    assert(sink.calls==541&&sink.largest==15360);
    const auto* header=sink.bytes.data();
    assert(!std::memcmp(header,"DDS ",4));
    assert(little32(header+4)==124&&little32(header+8)==0x81007);
    assert(little32(header+12)==2160&&little32(header+16)==3840);
    assert(little32(header+20)==3840*2160&&little32(header+24)==0&&little32(header+28)==1);
    for(unsigned i=32;i<76;++i)assert(header[i]==0);
    assert(little32(header+76)==32&&little32(header+80)==4&&!std::memcmp(header+84,"DX10",4));
    for(unsigned i=88;i<108;++i)assert(header[i]==0);
    assert(little32(header+108)==0x1000);
    for(unsigned i=112;i<128;++i)assert(header[i]==0);
    assert(little32(header+128)==98&&little32(header+132)==3);
    assert(little32(header+136)==0&&little32(header+140)==1&&little32(header+144)==0);
    for(std::size_t offset=148;offset<sink.bytes.size();offset+=16)assert((sink.bytes[offset]&127)==64);
}

void expectPixel(const Sink& sink,unsigned x,unsigned y,std::uint32_t expected,unsigned tolerance=0) {
    const auto offset=148+(std::size_t(y/4)*960+x/4)*16;
    unsigned char decoded[64];
    decodeBlock(sink.bytes.data()+offset,decoded);
    for(unsigned channel=0;channel<4;++channel) {
        const auto actual=decoded[((y%4)*4+x%4)*4+channel];
        assert(std::abs(int(actual)-int((expected>>(channel*8))&255))<=static_cast<int>(tolerance));
    }
}

void ddsTest() {
    std::vector<std::uint32_t> pixels(960*540);
    constexpr std::uint32_t colors[]={0xff592311U,0xff3517cdU,0xff8dcb31U,0xff214b9fU};
    for(unsigned y=0;y<540;++y)for(unsigned x=0;x<960;++x)pixels[y*960+x]=colors[(y>=270)*2+(x>=480)];
    pixels[0]=0xfff1d9c3U;pixels[1]=0xff153709U;
    pixels[960]=0xff9049e1U;pixels[961]=0xff060b13U;
    Sink sink;
    assert(encodeBackgroundDds(pixels.data(),960,540,collect,&sink));
    ddsHeader(sink);
    constexpr unsigned char managerBlock[]={0xc0,0xf3,0x8f,0xad,0xc3,0x0f,0xff,0xff,
                                            0x12,0xc4,0x11,0xc4,0x22,0xd6,0x55,0xf8};
    assert(!std::memcmp(sink.bytes.data()+148,managerBlock,sizeof(managerBlock)));
    expectPixel(sink,200,200,colors[0]);
    expectPixel(sink,3640,200,colors[1]);
    expectPixel(sink,200,1960,colors[2]);
    expectPixel(sink,3839,2159,colors[3]);
    for(const auto failure:{std::size_t(0),std::size_t(1),std::size_t(2)}) {
        Sink failed;failed.failAt=failure;
        assert(!encodeBackgroundDds(pixels.data(),960,540,collect,&failed));
        assert(failed.calls==failure+1);
    }
}

void cropTest() {
    std::vector<std::uint32_t> pixels(64*64);
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)
        pixels[y*64+x]=0xff590000U|((y*4+1)<<8)|(x*4+1);
    Sink sink;
    assert(encodeBackgroundDds(pixels.data(),64,64,collect,&sink));
    ddsHeader(sink);
    for(const unsigned y:{0U,1079U,2159U})for(const unsigned x:{0U,1919U,3839U}) {
        const double sx=std::clamp((x+0.5)/60.0-0.5,0.0,63.0);
        const double sy=std::clamp((y+0.5)/60.0+13.5,0.0,63.0);
        const auto expected=0xff590000U|(static_cast<unsigned>(sy*4+1+0.5)<<8)|static_cast<unsigned>(sx*4+1+0.5);
        expectPixel(sink,x,y,expected,2);
    }
}

void boundsAndCancelTest() {
    const std::uint32_t pixel=0x79592311;
    Sink untouched;
    assert(!encodeIconPng(nullptr,512,512,collect,&untouched));
    assert(!encodeIconPng(&pixel,512,512,nullptr,&untouched));
    for(const unsigned size:{0U,1U,511U,513U,UINT_MAX}) {
        assert(!encodeIconPng(&pixel,size,512,collect,&untouched));
        assert(!encodeIconPng(&pixel,512,size,collect,&untouched));
    }
    assert(!encodeBackgroundDds(nullptr,1,1,collect,&untouched));
    assert(!encodeBackgroundDds(&pixel,1,1,nullptr,&untouched));
    for(const unsigned size:{0U,2049U,UINT_MAX}) {
        assert(!encodeBackgroundDds(&pixel,size,1,collect,&untouched));
        assert(!encodeBackgroundDds(&pixel,1,size,collect,&untouched));
    }
    std::atomic_bool cancel{true};
    assert(!encodeBackgroundDds(&pixel,1,1,collect,&untouched,&cancel));
    assert(untouched.calls==0);
    for(const auto after:{std::size_t(1),std::size_t(2),std::size_t(541)}) {
        cancel.store(false);
        Sink cancelled;cancelled.cancel=&cancel;cancelled.cancelAfter=after;
        assert(!encodeBackgroundDds(&pixel,1,1,collect,&cancelled,&cancel));
        assert(cancelled.calls==after);
        if(after>=2)expectPixel(cancelled,0,0,pixel);
    }
    std::vector<std::uint32_t> tall(2048,pixel),wide(2048,pixel);
    for(const bool horizontal:{false,true}) {
        cancel.store(false);
        Sink cancelled;cancelled.cancel=&cancel;cancelled.cancelAfter=2;
        assert(!encodeBackgroundDds(horizontal?wide.data():tall.data(),horizontal?2048:1,horizontal?1:2048,
                                    collect,&cancelled,&cancel));
        assert(cancelled.calls==2);
        expectPixel(cancelled,0,0,pixel);
    }
}
}

int main() {
    pngTest();
    ddsTest();
    cropTest();
    boundsAndCancelTest();
    std::puts("forwarder image tests passed");
}
