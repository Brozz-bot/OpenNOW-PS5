#include "stream/nvst_sdp.hpp"
#include "stream/sdp.hpp"

#include <cassert>
#include <string>

namespace
{

bool HasLine(const std::string& sdp, const std::string& line)
{
    return sdp.find(line + "\n") != std::string::npos;
}

bool HasAttribute(const std::string& sdp, const std::string& attribute)
{
    return sdp.find("a=" + attribute) != std::string::npos;
}

} // namespace

int main()
{
    const std::string mapped_offer =
        "v=0\r\nc=IN IP4 0.0.0.0\r\n"
        "a=candidate:1 1 udp 2122260223 10.0.175.0 47998 typ host\r\n";
    const auto mapped = opennow::sdp::PrepareGfnOfferSdp(
        mapped_offer, "wss://signaling.example/nvst/", "198.51.100.55", 443);
    assert(mapped.find("a=candidate:1 1 udp 2122260223 198.51.100.55 443 typ host") != std::string::npos);
    assert(mapped.find("c=IN IP4 0.0.0.0") != std::string::npos);
    assert(opennow::sdp::PrepareGfnOfferSdp(mapped_offer, "wss://signaling.example/nvst/", "", 0) == mapped_offer);
    assert(opennow::sdp::PrepareGfnOfferSdp(mapped_offer, "wss://signaling.example/nvst/", "198.51.100.55", 0) == mapped_offer);
    const std::string trickled = "candidate:1 1 udp 2122260223 10.0.175.0 47998 typ host generation 0";
    assert(opennow::sdp::RewriteGfnMediaCandidate(trickled, "198.51.100.55", 18784) ==
        "candidate:1 1 udp 2122260223 198.51.100.55 18784 typ host generation 0");
    assert(opennow::sdp::RewriteGfnMediaCandidate(trickled, "198.51.100.55", 65536) == trickled);
    assert(opennow::sdp::RewriteGfnMediaCandidate("candidate:malformed", "198.51.100.55", 443) == "candidate:malformed");
    for (const auto* address : {"203.0.113.10", "8.8.8.8", "0.0.0.1", "9.255.255.255", "11.0.0.0",
             "100.63.255.255", "100.128.0.0", "126.255.255.255", "128.0.0.0", "169.253.255.255",
             "169.255.0.0", "172.15.255.255", "172.32.0.0", "192.167.255.255", "192.169.0.0",
             "223.255.255.255", "240.0.0.0", "255.255.255.255", "host.local", "2001:db8::1", "300.1.1.1"}) {
        const auto candidate = std::string("candidate:1 1 UDP 1 ") + address + " 47998 typ host generation 0";
        assert(opennow::sdp::RewriteGfnMediaCandidate(candidate, "198.51.100.55", 443) == candidate);
        const auto offer = "v=0\r\na=" + candidate + "\r\n";
        assert(opennow::sdp::PrepareGfnOfferSdp(offer, "", "198.51.100.55", 443) == offer);
    }
    for (const auto* address : {"0.0.0.0", "10.0.0.0", "10.255.255.255", "100.64.0.0", "100.127.255.255",
             "127.0.0.0", "127.255.255.255", "169.254.0.0", "169.254.255.255", "172.16.0.0",
             "172.31.255.255", "192.168.0.0", "192.168.255.255", "224.0.0.0", "239.255.255.255"}) {
        const auto candidate = std::string("candidate:1 1 UDP 1 ") + address + " 47998 typ host generation 0";
        const std::string rewritten = "candidate:1 1 UDP 1 198.51.100.55 443 typ host generation 0";
        assert(opennow::sdp::RewriteGfnMediaCandidate(candidate, "198.51.100.55", 443) == rewritten);
        assert(opennow::sdp::PrepareGfnOfferSdp("v=0\r\na=" + candidate + "\r\n", "", "198.51.100.55", 443) ==
            "v=0\r\na=" + rewritten + "\r\n");
        if (std::string(address) != "0.0.0.0")
            assert(opennow::sdp::RewriteGfnMediaCandidate(candidate, "", 0, "wss://198.51.100.55/nvst/") == candidate);
    }
    const std::string normalized_offer = "v=0\r\na=ice-ufrag:test\r\na=ice-pwd:test-password\r\na=fingerprint:sha-256 11:22\r\n";
    for (const auto* offer : {
             "v=0\na=ice-ufrag:test\na=ice-pwd:test-password\na=fingerprint:sha-256 11:22",
             "v=0\na=ice-ufrag:test\na=ice-pwd:test-password\na=fingerprint:sha-256 11:22\n",
             "v=0\r\na=ice-ufrag:test\r\na=ice-pwd:test-password\r\na=fingerprint:sha-256 11:22",
             "v=0\na=ice-ufrag:test\r\na=ice-pwd:test-password\na=fingerprint:sha-256 11:22\r\n"}) {
        assert(opennow::sdp::PrepareGfnOfferSdp(offer, "", "", 0) == normalized_offer);
    }
    assert(opennow::sdp::PrepareGfnOfferSdp(normalized_offer, "", "", 0) == normalized_offer);
    assert(opennow::sdp::PrepareGfnOfferSdp("", "", "", 0).empty());
    const std::string zero_offer = "v=0\nc=IN IP4 0.0.0.0\na=candidate:1 1 UDP 1 0.0.0.0 9 typ host";
    assert(opennow::sdp::PrepareGfnOfferSdp(zero_offer, "", "198-51-100-55.example", 443) ==
        "v=0\r\nc=IN IP4 0.0.0.0\r\na=candidate:1 1 UDP 1 198.51.100.55 443 typ host\r\n");
    for (const auto& signaling : {"wss://198.51.100.55/nvst/", "wss://198-51-100-55.example/nvst/"}) {
        assert(opennow::sdp::PrepareGfnOfferSdp(zero_offer, signaling, "", 0) ==
            "v=0\r\nc=IN IP4 0.0.0.0\r\na=candidate:1 1 UDP 1 198.51.100.55 9 typ host\r\n");
    }
    assert(opennow::sdp::PrepareGfnOfferSdp(zero_offer, "", "198.51.100.55", 0) ==
        "v=0\r\nc=IN IP4 0.0.0.0\r\na=candidate:1 1 UDP 1 198.51.100.55 9 typ host\r\n");
    assert(opennow::sdp::PrepareGfnOfferSdp(zero_offer, "wss://unparseable.example/nvst/", "", 0) ==
        "v=0\r\nc=IN IP4 0.0.0.0\r\na=candidate:1 1 UDP 1 0.0.0.0 9 typ host\r\n");
    assert(opennow::sdp::RewriteGfnMediaCandidate("candidate:1 1 UDP 1 0.0.0.0 47998 typ host", "", 0,
        "wss://198-51-100-55.example/nvst/") == "candidate:1 1 UDP 1 198.51.100.55 47998 typ host");
    assert(opennow::sdp::BuildManualMediaCandidate("", "198.51.100.55", 443, 100) ==
        "a=candidate:100 1 UDP 2130706431 198.51.100.55 443 typ host");
    assert(opennow::sdp::BuildManualMediaCandidate("wss://198.51.100.55/", "", 0, 100).empty());

    opennow::StreamSettings settings;
    settings.width = 1280;
    settings.height = 720;
    settings.fps = 60;
    settings.bitrate_kbps = 12000;
    settings.quality = opennow::QualityMode::adaptive;

    const std::string answer =
        "v=0\r\n"
        "a=ice-ufrag:test-ufrag\r\n"
        "a=ice-pwd:test-password\r\n"
        "a=fingerprint:sha-256 AA:BB:CC\r\n";
    const std::string sdp = opennow::webrtc::BuildNvstSdp(
        answer, settings, opennow::webrtc::RiInputCapabilities {});

    for (const auto& line : {
        "a=video.maxFPS:60",
        "a=video.initialBitrateKbps:4000",
        "a=video.initialPeakBitrateKbps:4000",
        "a=vqos.bw.maximumBitrateKbps:12000",
        "a=vqos.bw.minimumBitrateKbps:4000",
        "a=vqos.dynamicStreamingMode:3",
        "a=vqos.drc.enable:1",
        "a=vqos.resControl.cpmRtc.featureMask:3",
    }) {
        assert(HasLine(sdp, line));
    }

    for (const auto& attribute : {
        "vqos.bw.peakBitrateKbps:",
        "vqos.bw.serverPeakBitrateKbps:",
        "vqos.bw.enableBandwidthEstimation:",
        "vqos.bw.disableBitrateLimit:",
        "vqos.grc.maximumBitrateKbps:",
        "vqos.grc.enable:",
        "vqos.dfc.enable:",
        "vqos.dfc.adjustResAndFps:",
        "vqos.resControl.cpmRtc.enable:",
        "vqos.resControl.cpmRtc.minResolutionPercent:",
        "vqos.resControl.cpmRtc.resolutionChangeHoldonMs:",
    }) {
        assert(!HasAttribute(sdp, attribute));
    }

    settings.bitrate_kbps = 20000;
    const std::string quality_sdp = opennow::webrtc::BuildNvstSdp(
        answer, settings, opennow::webrtc::RiInputCapabilities {});
    assert(HasLine(quality_sdp, "a=video.initialBitrateKbps:5000"));
    assert(HasLine(quality_sdp, "a=vqos.bw.maximumBitrateKbps:20000"));
    for(auto profile:{opennow::StreamProfile::quality,opennow::StreamProfile::smooth,
                     opennow::StreamProfile::experimental,opennow::StreamProfile::compatibility}){
        const auto target=opennow::settingsFor(profile);
        auto negotiated=opennow::webrtc::BuildNvstSdp(answer,target,{});
        assert(HasLine(negotiated,"a=video.clientViewportWd:"+std::to_string(target.width)));
        assert(HasLine(negotiated,"a=video.clientViewportHt:"+std::to_string(target.height)));
        assert(HasLine(negotiated,"a=video.maxFPS:"+std::to_string(target.fps)));
        assert(HasLine(negotiated,"a=vqos.bw.maximumBitrateKbps:"+std::to_string(target.bitrate_kbps)));
        assert(HasLine(negotiated,"a=video.dynamicRangeMode:0"));
        assert(HasLine(negotiated,"a=video.bitDepth:8"));
        assert(HasLine(negotiated,"a=video.maxNumReferenceFrames:4"));
        const std::string offer="v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 98\r\na=rtpmap:98 H264/90000\r\n"
            "m=audio 9 UDP/TLS/RTP/SAVPF 111 63\r\na=rtpmap:111 opus/48000/2\r\na=rtpmap:63 red/48000/2\r\na=fmtp:63 111/111\r\n";
        const std::string raw="v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 96\r\nc=IN IP4 0.0.0.0\r\na=rtpmap:96 H264/90000\r\n"
            "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\na=rtpmap:111 opus/48000/2\r\na=fmtp:111 stereo=0\r\n";
        const auto adapted=opennow::sdp::AdaptAnswerSdpToOffer(raw,offer,target);
        assert(HasLine(adapted,"b=AS:"+std::to_string(target.bitrate_kbps)+"\r"));
        assert(adapted.find("stereo=1;sprop-stereo=1;maxaveragebitrate=256000")!=std::string::npos);
        assert(adapted.find("stereo=0")==std::string::npos);
        assert(adapted.find("a=rtpmap:63 red/48000/2")!=std::string::npos);
    }
    const auto hdr=opennow::settingsFor(opennow::StreamProfile::native_hdr120);
    for(auto profile:{opennow::StreamProfile::native_hdr120,opennow::StreamProfile::native_hdr60,
                     opennow::StreamProfile::native_4k120,opennow::StreamProfile::native_1080,
                     opennow::StreamProfile::native_hdr90,opennow::StreamProfile::native_4k90}){
        const auto target=opennow::settingsFor(profile);
        const auto native=opennow::webrtc::BuildNvstSdp(answer,target,{});
        assert(HasLine(native,"a=vqos.bw.minimumBitrateKbps:"+std::to_string(target.bitrate_kbps*3/4)));
        assert(HasLine(native,"a=video.initialBitrateKbps:"+std::to_string(target.bitrate_kbps*3/4)));
        assert(HasLine(native,"a=video.maxFPS:"+std::to_string(target.fps)));
        assert(HasLine(native,"a=video.maxNumReferenceFrames:1"));
        assert(HasLine(native,"a=video.dynamicRangeMode:"+std::to_string(target.hdr()?1:0)));
        assert(HasLine(native,"a=video.bitStreamFormat:"+std::to_string(target.codec()==opennow::VideoCodec::hevc?1:0)));
        assert(HasLine(native,"a=vqos.dynamicStreamingMode:0"));
        assert(HasLine(native,"a=vqos.drc.enable:0"));
        assert(HasLine(native,"a=vqos.resControl.cpmRtc.minResolutionPercent:100"));
        assert(native.find("a=vqos.drc.enable:1")==std::string::npos);
        assert(native.find("a=vqos.bw.serverPeakBitrateKbps:")<native.find("m=audio"));
    }
    // Every real profile is reachable once through the user-visible L1 cycle.
    bool visited[static_cast<unsigned>(opennow::StreamProfile::count)]{};
    auto selected=opennow::StreamProfile::native_hdr120;
    for(unsigned i=0;i<static_cast<unsigned>(opennow::StreamProfile::count);++i){
        const auto index=static_cast<unsigned>(selected);assert(index<static_cast<unsigned>(opennow::StreamProfile::count)&&!visited[index]);visited[index]=true;
        selected=opennow::nextProfile(selected);
    }
    assert(selected==opennow::StreamProfile::native_hdr120);
    const std::string hevcOffer="v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 98 100\r\na=rtpmap:98 H264/90000\r\na=rtpmap:100 H265/90000\r\na=fmtp:100 profile-id=2;level-id=156;sprop-max-don-diff=0\r\n";
    const std::string rawHevc=answer+"m=video 9 UDP/TLS/RTP/SAVPF 96\r\na=rtpmap:96 H265/90000\r\na=fmtp:96 profile-id=2;level-id=156;sprop-max-don-diff=0\r\n";
    assert(opennow::sdp::AdaptAnswerSdpToOffer(rawHevc,rawHevc,settings).empty());
    const auto hdrAnswer=opennow::sdp::AdaptAnswerSdpToOffer(rawHevc,hevcOffer,hdr);
    assert(hdrAnswer.find("a=rtpmap:100 H265/90000")!=std::string::npos);
    assert(hdrAnswer.find("profile-id=2;level-id=156")!=std::string::npos);
    const auto hdrSdp=opennow::webrtc::BuildNvstSdp(hdrAnswer,hdr,{});
    for(const auto* a:{"a=video.dynamicRangeMode:1","a=video.bitDepth:10","a=video.chromaFormat:1","a=video.bitStreamFormat:1","a=video.maxFPS:120"})assert(HasLine(hdrSdp,a));
    auto sdrMain10=hdr;sdrMain10.mode=opennow::VideoMode::hevcMain10SdrHardware;sdrMain10.width=1440;sdrMain10.height=1080;sdrMain10.fps=47;
    const auto sdrAnswer=opennow::sdp::AdaptAnswerSdpToOffer(rawHevc,hevcOffer,sdrMain10);
    assert(!sdrAnswer.empty()&&sdrAnswer.find("profile-id=2")!=std::string::npos);
    const auto sdrSdp=opennow::webrtc::BuildNvstSdp(sdrAnswer,sdrMain10,{});
    for(const auto* a:{"a=video.dynamicRangeMode:0","a=video.bitDepth:10","a=video.bitStreamFormat:1","a=video.maxFPS:47","a=video.clientViewportWd:1440","a=video.clientViewportHt:1080"})assert(HasLine(sdrSdp,a));
    auto main8=hevcOffer;main8.replace(main8.find("profile-id=2"),12,"profile-id=1");
    assert(opennow::sdp::AdaptAnswerSdpToOffer(rawHevc,main8,sdrMain10).empty());
    const auto noFmtp=std::string("v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 100\r\na=rtpmap:100 H265/90000\r\n");
    assert(opennow::sdp::AdaptAnswerSdpToOffer(rawHevc,noFmtp,sdrMain10).find("profile-id=2")!=std::string::npos);
    assert(opennow::sdp::AdaptAnswerSdpToOffer(rawHevc,noFmtp+"a=fmtp:100 sprop-max-don-diff=0\r\n",sdrMain10).find("profile-id=2")!=std::string::npos);
    auto softwareFixed=settings;softwareFixed.network=opennow::NetworkPolicy::fixed;
    auto decoderToggle=softwareFixed;decoderToggle.mode=opennow::VideoMode::h264Hardware;
    assert(opennow::webrtc::BuildNvstSdp(answer,softwareFixed,{})==opennow::webrtc::BuildNvstSdp(answer,decoderToggle,{}));
    assert(HasLine(opennow::webrtc::BuildNvstSdp(answer,decoderToggle,{}),"a=vqos.dynamicStreamingMode:0"));
    assert(opennow::sdp::AdaptAnswerSdpToOffer(rawHevc,"v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 98\r\na=rtpmap:98 H264/90000\r\n",hdr).empty());
    auto interleaved=hevcOffer;interleaved.replace(interleaved.find("sprop-max-don-diff=0"),20,"sprop-max-don-diff=1");
    assert(opennow::sdp::AdaptAnswerSdpToOffer(rawHevc,interleaved,hdr).empty());
    const std::string rawVideo="v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 96\r\nc=IN IP4 0.0.0.0\r\n"
        "a=rtpmap:96 H264/90000\r\na=fmtp:96 packetization-mode=1\r\na=rtcp-fb:96 nack\r\na=rtcp-fb:96 nack pli\r\na=sendrecv\r\n";
    const std::string orderedOffer="v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 102 98\r\n"
        "a=rtpmap:98 H264/90000\r\na=fmtp:98 packetization-mode=1\r\n"
        "a=rtpmap:102 H264/90000\r\na=fmtp:102 packetization-mode=1\r\n";
    const auto orderedAnswer=opennow::sdp::AdaptAnswerSdpToOffer(rawVideo,orderedOffer,settings);
    assert(orderedAnswer.find("m=video 9 UDP/TLS/RTP/SAVPF 102\r\n")!=std::string::npos);
    assert(orderedAnswer.find("a=rtcp-fb:")==std::string::npos);
    const std::string selectedOffer="v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 98\r\na=sendonly\r\n"
        "a=rtpmap:100 H264/90000\r\na=fmtp:100 packetization-mode=1\r\n"
        "a=rtpmap:98 H264/90000\r\na=fmtp:98 packetization-mode=1\r\n"
        "a=rtcp-fb:* nack\r\na=rtcp-fb:* nack pli\r\na=rtcp-fb:98 goog-remb\r\n";
    const auto selectedAnswer=opennow::sdp::AdaptAnswerSdpToOffer(rawVideo,selectedOffer,settings);
    assert(selectedAnswer.find("m=video 9 UDP/TLS/RTP/SAVPF 98\r\n")!=std::string::npos);
    assert(selectedAnswer.find("a=rtcp-fb:98 nack\r\n")!=std::string::npos);
    assert(selectedAnswer.find("a=rtcp-fb:98 nack pli\r\n")!=std::string::npos);
    assert(selectedAnswer.find("goog-remb")==std::string::npos);
    for(const auto* direction:{"a=recvonly\r\n","a=inactive\r\n"}){
        assert(opennow::sdp::AdaptAnswerSdpToOffer(rawVideo,orderedOffer+direction,settings).empty());
        assert(opennow::sdp::AdaptAnswerSdpToOffer(rawVideo,std::string("v=0\r\n")+direction+orderedOffer.substr(5),settings).empty());
    }
    auto disabledOffer=orderedOffer;disabledOffer.replace(disabledOffer.find("m=video 9"),9,"m=video 0");
    assert(opennow::sdp::AdaptAnswerSdpToOffer(rawVideo,disabledOffer,settings).empty());
    for(const auto* mapping:{"h264/90000","H264/90000"}){
        const auto minimal="v=0\r\nm=video 9 RTP/AVP 98\r\na=rtpmap:98 "+std::string(mapping)+"\r\n";
        assert(!opennow::sdp::AdaptAnswerSdpToOffer(rawVideo,minimal,settings).empty());
    }
    for(const auto* mapping:{"H264/8000","H264/90000/2"})
        assert(opennow::sdp::AdaptAnswerSdpToOffer(rawVideo,"m=video 9 RTP/AVP 98\r\na=rtpmap:98 "+std::string(mapping)+"\r\n",settings).empty());
    for(const auto* params:{"profile-id=2junk","profile-id=2;profile-id=1","sprop-max-don-diff=bad","sprop-max-don-diff=1","profile-id=2;sprop-max-don-diff=0junk"}){
        assert(opennow::sdp::AdaptAnswerSdpToOffer(rawHevc,noFmtp+"a=fmtp:100 "+params+"\r\n",hdr).empty());
    }
    const auto unknownKey=opennow::sdp::AdaptAnswerSdpToOffer(rawHevc,noFmtp+"a=fmtp:100 x-profile-id=1;sprop-max-don-diff=0\r\n",hdr);
    assert(unknownKey.find(";profile-id=2")!=std::string::npos);
    assert(opennow::sdp::AdaptAnswerSdpToOffer(rawHevc,noFmtp,hdr).find("a=fmtp:100 profile-id=2;level-id=156;sprop-max-don-diff=0\r\n")!=std::string::npos);
    const auto wrongMode="m=video 9 RTP/AVP 98\r\na=rtpmap:98 H264/90000\r\na=fmtp:98 packetization-mode=10\r\n";
    assert(opennow::sdp::AdaptAnswerSdpToOffer(rawVideo,wrongMode,settings).empty());
    const std::string alternateRaw="v=0\r\nm=video 9 RTP/AVP 120 121\r\nc=IN IP4 0.0.0.0\r\n"
        "b=AS:1\r\nb=TIAS:1000\r\na=rtpmap:120 VP8/90000\r\na=rtpmap:121 rtx/90000\r\na=fmtp:121 apt=120\r\n"
        "a=rtcp-fb:120 goog-remb\r\na=sendonly\r\n";
    const auto rebuilt=opennow::sdp::AdaptAnswerSdpToOffer(alternateRaw,selectedOffer,settings);
    assert(rebuilt.find("a=rtpmap:98 H264/90000\r\n")!=std::string::npos);
    assert(rebuilt.find("VP8")==std::string::npos&&rebuilt.find("rtx")==std::string::npos&&rebuilt.find("apt=")==std::string::npos);
    assert(rebuilt.find("a=sendonly")==std::string::npos&&rebuilt.find("a=recvonly\r\n")!=std::string::npos);
    assert(rebuilt.find("b=TIAS:")==std::string::npos);
    assert(rebuilt.find("b=AS:")==rebuilt.rfind("b=AS:"));
    assert(opennow::sdp::AdaptAnswerSdpToOffer(rebuilt,selectedOffer,settings)==rebuilt);
    const auto nvstHdr=opennow::webrtc::BuildNvstSdp(answer,hdr,{});
    assert(HasLine(nvstHdr,"a=video.dx9EnableHdr:1"));
    assert(nvstHdr.find("m=mic")==std::string::npos);
    const auto nvstSdr=opennow::webrtc::BuildNvstSdp(answer,sdrMain10,{});
    assert(HasLine(nvstSdr,"a=video.dx9EnableHdr:0"));
    assert(opennow::webrtc::BuildNvstSdp("v=0\r\n",settings,{}).empty());
    for(int fps:{30,60,90,120}){
        auto target=hdr;target.fps=fps;
        const auto result=opennow::webrtc::BuildNvstSdp(answer,target,{});
        assert(HasLine(result,"a=video.framePacing.pid.minTargetFrameTimeUs:"+std::to_string(950000/fps)));
        assert(HasAttribute(result,"video.encoderFeatureSetting:")==(fps>60));
        assert(HasAttribute(result,"video.encoderPreset:")==(fps>60));
        assert(HasAttribute(result,"vqos.maxStreamFpsEstimate:")==(fps>60));
        if(fps>60){
            assert(HasLine(result,"a=video.encoderFeatureSetting:47"));
            assert(HasLine(result,"a=video.encoderPreset:6"));
            assert(HasLine(result,"a=bwe.iirFilterFactor:8"));
            assert(HasLine(result,"a=vqos.resControl.dfc.useClientFpsPerf:0"));
            assert(HasLine(result,"a=vqos.maxStreamFpsEstimate:"+std::to_string(fps)));
            assert(HasLine(result,"a=video.fbcDynamicFpsGrabTimeoutMs:"+std::to_string(fps==90?9:6)));
            assert(HasLine(result,"a=vqos.resControl.cpmRtc.decodeTimeThresholdMs:"+std::to_string(fps==90?11:9)));
        }
        assert(HasLine(result,"a=packetPacing.numGroups:5"));
        assert(HasLine(result,"a=vqos.fec.repairPercent:5"));
    }
    for(const auto quality:{opennow::QualityMode::original,opennow::QualityMode::adaptive,opennow::QualityMode::clarity}){
        auto target=hdr;target.quality=quality;
        const auto result=opennow::webrtc::BuildNvstSdp(answer,target,{});
        assert(HasLine(result,"a=packetPacing.numGroups:"+std::to_string(quality==opennow::QualityMode::original?5:quality==opennow::QualityMode::adaptive?6:8)));
        assert(HasLine(result,"a=vqos.fec.repairPercent:"+std::to_string(quality==opennow::QualityMode::original?5:quality==opennow::QualityMode::adaptive?8:10)));
    }
    for(const auto* missing:{"a=ice-ufrag:","a=ice-pwd:","a=fingerprint:sha-256 "}){
        auto incomplete=answer;
        const auto start=incomplete.find(missing);
        incomplete.erase(start,incomplete.find("\r\n",start)+2-start);
        assert(opennow::webrtc::BuildNvstSdp(incomplete,settings,{}).empty());
        incomplete=answer;
        const auto value=incomplete.find(missing)+std::char_traits<char>::length(missing);
        incomplete.replace(value,incomplete.find("\r\n",value)-value," \t ");
        assert(opennow::webrtc::BuildNvstSdp(incomplete,settings,{}).empty());
    }
    opennow::webrtc::RiInputCapabilities caps;
    assert(opennow::sdp::ParseRiInputCapabilities("",caps));
    assert(caps.partial_reliable_threshold_ms==16&&caps.hid_device_mask==UINT32_MAX&&caps.partial_reliable_gamepad_mask==15&&caps.partial_reliable_hid_mask==UINT32_MAX);
    assert(opennow::sdp::ParseRiInputCapabilities("a=ri.partialReliableThresholdMs:65535\r\na=ri.hidDeviceMask:0x80000001\r\na=ri.enablePartiallyReliableTransferGamepad:0\r\n",caps));
    assert(caps.partial_reliable_threshold_ms==65535&&caps.hid_device_mask==0x80000001u&&caps.partial_reliable_hid_mask==caps.hid_device_mask&&caps.partial_reliable_gamepad_mask==0);
    assert(opennow::sdp::ParseRiInputCapabilities("a=ri.hidDeviceMask:7\na=ri.enablePartiallyReliableTransferHid:4294967295\r\na=ri.enablePartiallyReliableTransferGamepad:-1\n",caps));
    assert(caps.hid_device_mask==7&&caps.partial_reliable_hid_mask==UINT32_MAX&&caps.partial_reliable_gamepad_mask==UINT32_MAX);
    for(const auto* value:{"","0","-1","65536","16junk","0x10","4294967296"}){
        auto previous=caps;
        assert(!opennow::sdp::ParseRiInputCapabilities("a=ri.partialReliableThresholdMs:"+std::string(value)+"\r\n",caps));
        assert(caps.hid_device_mask==previous.hid_device_mask&&caps.partial_reliable_threshold_ms==previous.partial_reliable_threshold_ms);
    }
    for(const auto* attribute:{"ri.hidDeviceMask","ri.enablePartiallyReliableTransferHid","ri.enablePartiallyReliableTransferGamepad"}){
        for(const auto* value:{"","-2","4294967296","0x100000000","0x","0xgg","7junk","1.0"})
            assert(!opennow::sdp::ParseRiInputCapabilities("a="+std::string(attribute)+":"+value+"\r\n",caps));
        for(const auto* value:{"0","4294967295","-1","0xffffffff","0X80000000"})
            assert(opennow::sdp::ParseRiInputCapabilities("a="+std::string(attribute)+":"+value+"\r\n",caps));
        assert(!opennow::sdp::ParseRiInputCapabilities("a="+std::string(attribute)+":1\r\na="+attribute+":2\r\n",caps));
    }
    const auto nackOnly=opennow::sdp::AdaptAnswerSdpToOffer(rawVideo,orderedOffer+"a=rtcp-fb:102 nack\r\na=rtcp-fb:* nack\r\n",settings);
    assert(nackOnly.find("a=rtcp-fb:102 nack\r\n")!=std::string::npos);
    assert(nackOnly.find("nack pli")==std::string::npos);
    assert(nackOnly.find("a=rtcp-fb:")==nackOnly.rfind("a=rtcp-fb:"));
    assert(opennow::sdp::AdaptAnswerSdpToOffer(rawVideo,"m=video 9 RTP/AVP 98\r\na=rtpmap:100 H264/90000\r\n",settings).empty());
    auto duplicateFmtp=orderedOffer+"a=fmtp:102 packetization-mode=0\r\n";
    assert(opennow::sdp::AdaptAnswerSdpToOffer(rawVideo,duplicateFmtp,settings).empty());
    const auto mixedOffer="v=0\na=recvonly\r\nm=video 9 RTP/AVP 98\na=sendonly\r\na=rtpmap:98 H264/90000\n";
    assert(!opennow::sdp::AdaptAnswerSdpToOffer(rawVideo,mixedOffer,settings).empty());
    return 0;
}
