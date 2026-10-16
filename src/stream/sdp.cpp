// Protocol helpers adapted from OpenNOW-Switch, MIT.
#include "sdp.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cstdio>
#include <set>
#include <vector>
namespace opennow::sdp {
std::string ReplaceAll(std::string value, const std::string& from, const std::string& to)
{
    size_t pos = 0;
    while ((pos = value.find(from, pos)) != std::string::npos) {
        value.replace(pos, from.size(), to);
        pos += to.size();
    }
    return value;
}

std::vector<std::string> SplitSdpLines(const std::string& sdp)
{
    std::vector<std::string> lines;
    size_t start = 0;

    while (start < sdp.size()) {
        size_t end = sdp.find('\n', start);

        if (end == std::string::npos) {
            lines.push_back(sdp.substr(start));
            break;
        }

        const auto length = end > start && sdp[end - 1] == '\r' ? end - start - 1 : end - start;
        lines.push_back(sdp.substr(start, length));
        start = end + 1;
    }

    return lines;
}

std::string JoinSdpLines(const std::vector<std::string>& lines)
{
    std::string result;
    for (const auto& line : lines) {
        result += line;
        result += "\r\n";
    }
    return result;
}

bool StartsWithString(const std::string& value, const std::string& prefix)
{
    return value.rfind(prefix, 0) == 0;
}


bool ParseIntegerAttribute(const std::vector<std::string>& lines, const std::string& attribute,
    std::uint32_t& value, bool mask)
{
    const std::string prefix = "a=" + attribute + ":";
    bool found = false;
    for (const auto& line : lines) {
        if (!StartsWithString(line, prefix))
            continue;
        if (found)
            return false;
        found = true;
        const auto raw = audio::trim(line.substr(prefix.size()));
        if (mask && raw == "-1") {
            value = UINT32_MAX;
            continue;
        }
        const bool hex = mask && (raw.rfind("0x", 0) == 0 || raw.rfind("0X", 0) == 0);
        const char* first = raw.data() + (hex ? 2 : 0);
        const auto parsed = std::from_chars(first, raw.data() + raw.size(), value, hex ? 16 : 10);
        if (first == raw.data() + raw.size() || parsed.ec != std::errc{} ||
            parsed.ptr != raw.data() + raw.size())
            return false;
    }
    return true;
}

bool ParseRiInputCapabilities(const std::string& offer_sdp, webrtc::RiInputCapabilities& result)
{
    if (offer_sdp.size() > 65536)
        return false;
    const auto lines = SplitSdpLines(offer_sdp);
    webrtc::RiInputCapabilities caps;
    std::uint32_t threshold = caps.partial_reliable_threshold_ms;
    if (!ParseIntegerAttribute(lines, "ri.partialReliableThresholdMs", threshold, false) ||
        !threshold || threshold > 65535 ||
        !ParseIntegerAttribute(lines, "ri.hidDeviceMask", caps.hid_device_mask, true) ||
        !ParseIntegerAttribute(lines, "ri.enablePartiallyReliableTransferGamepad", caps.partial_reliable_gamepad_mask, true))
        return false;
    caps.partial_reliable_threshold_ms = static_cast<int>(threshold);
    caps.partial_reliable_hid_mask = caps.hid_device_mask;
    if (!ParseIntegerAttribute(lines, "ri.enablePartiallyReliableTransferHid", caps.partial_reliable_hid_mask, true))
        return false;
    result = caps;
    return true;
}

struct VideoFormat {
    int payload = -1;
    std::string rtpmap;
    std::string fmtp;
    std::vector<std::string> feedback;
};

VideoFormat SelectOfferVideoFormat(const std::vector<std::string>& lines, const StreamSettings& settings)
{
    std::array<std::string, 128> maps{}, params{};
    std::array<bool, 128> mapped{}, parameterized{};
    std::vector<unsigned> payloads;
    std::vector<std::string> feedback;
    bool in_video = false, session = true, session_can_send = true, can_send = true;
    if (lines.size() > 2048)
        return {};
    for (const auto& line : lines) {
        if (line.size() > 2048)
            return {};
        if (StartsWithString(line, "m=")) {
            session = false;
            in_video = StartsWithString(line, "m=video ");
            if (!in_video)
                continue;
            std::vector<std::string> fields;
            for (const auto& field : audio::split(line, ' '))
                if (!field.empty()) fields.push_back(field);
            unsigned port = 0;
            if (fields.size() < 4 || !audio::integer(fields[1], port) || !port || port > 65535)
                return {};
            for (std::size_t i = 3; i < fields.size(); ++i) {
                unsigned payload = 0;
                if (!audio::integer(fields[i], payload) || payload > 127 ||
                    std::find(payloads.begin(), payloads.end(), payload) != payloads.end())
                    return {};
                payloads.push_back(payload);
            }
            can_send = session_can_send;
            continue;
        }
        if (line == "a=sendrecv" || line == "a=sendonly" || line == "a=recvonly" || line == "a=inactive") {
            const bool sending = line == "a=sendrecv" || line == "a=sendonly";
            if (session) session_can_send = sending;
            if (in_video) can_send = sending;
        }
        if (!in_video)
            continue;
        if (StartsWithString(line, "a=rtcp-fb:")) {
            feedback.push_back(line.substr(10));
            continue;
        }
        const bool map = StartsWithString(line, "a=rtpmap:");
        if (!map && !StartsWithString(line, "a=fmtp:"))
            continue;
        const auto start = map ? 9u : 7u;
        const auto space = line.find_first_of(" \t", start);
        unsigned payload = 0;
        if (space == std::string::npos || !audio::integer(line.substr(start, space - start), payload) || payload > 127)
            return {};
        auto& seen = (map ? mapped : parameterized)[payload];
        if (seen)
            return {};
        seen = true;
        (map ? maps : params)[payload] = audio::trim(line.substr(space + 1));
    }
    if (!can_send)
        return {};
    const bool hevc = settings.codec() == VideoCodec::hevc;
    VideoFormat fallback;
    for (const auto payload : payloads) {
        const auto codec = audio::lower(maps[payload]);
        if (hevc ? codec != "h265/90000" && codec != "hevc/90000" : codec != "h264/90000")
            continue;
        bool valid = true, profile_seen = false, don_seen = false, mode_seen = false;
        unsigned packetization = 0;
        for (const auto& item : audio::split(params[payload], ';')) {
            const auto equal = item.find('=');
            const auto key = audio::lower(audio::trim(item.substr(0, equal)));
            if ((hevc && key != "profile-id" && key != "sprop-max-don-diff") || (!hevc && key != "packetization-mode"))
                continue;
            unsigned value = 0;
            if (equal == std::string::npos || !audio::integer(audio::trim(item.substr(equal + 1)), value)) {
                valid = false;
                break;
            }
            if (key == "profile-id") {
                valid = !profile_seen && value == (settings.tenBit() ? 2u : 1u);
                profile_seen = true;
            } else if (key == "sprop-max-don-diff") {
                valid = !don_seen && value == 0;
                don_seen = true;
            } else {
                valid = !mode_seen && value <= 1;
                packetization = value;
                mode_seen = true;
            }
            if (!valid)
                break;
        }
        if (!valid)
            continue;
        VideoFormat format;
        format.payload = static_cast<int>(payload);
        const auto pt = std::to_string(payload);
        format.rtpmap = "a=rtpmap:" + pt + (hevc ? " H265/90000" : " H264/90000");
        auto fmtp = params[payload];
        if (hevc && !profile_seen) {
            if (fmtp.empty())
                fmtp = "profile-id=" + std::to_string(settings.tenBit() ? 2 : 1) + ";level-id=156;sprop-max-don-diff=0";
            else
                fmtp += ";profile-id=" + std::to_string(settings.tenBit() ? 2 : 1);
        }
        if (!fmtp.empty()) format.fmtp = "a=fmtp:" + pt + " " + fmtp;
        for (const auto& attribute : feedback) {
            const auto space = attribute.find_first_of(" \t");
            if (space == std::string::npos)
                continue;
            const auto source = attribute.substr(0, space);
            const auto type = audio::trim(attribute.substr(space + 1));
            if ((source == pt || source == "*") && (type == "nack" || type == "nack pli")) {
                const auto selected = "a=rtcp-fb:" + pt + " " + type;
                if (std::find(format.feedback.begin(), format.feedback.end(), selected) == format.feedback.end())
                    format.feedback.push_back(selected);
            }
        }
        if (hevc || packetization == 1)
            return format;
        if (fallback.payload < 0)
            fallback = format;
    }
    return fallback;
}

int ExtractOfferMediaPort(const std::string& offer_sdp, const char* media_name)
{
    const std::string prefix = std::string("m=") + media_name + " ";
    for (const auto& line : SplitSdpLines(offer_sdp)) {
        if (!StartsWithString(line, prefix))
            continue;

        const size_t port_start = prefix.size();
        const size_t port_end = line.find(' ', port_start);
        const std::string port_text = line.substr(
            port_start,
            port_end == std::string::npos ? std::string::npos : port_end - port_start);

        char* end = nullptr;
        const long port = std::strtol(port_text.c_str(), &end, 10);
        if (end != port_text.c_str() && port > 0 && port <= 65535)
            return static_cast<int>(port);
    }

    return 0;
}

int CountSdpLinesWithPrefix(const std::string& sdp, const std::string& prefix)
{
    int count = 0;
    for (const auto& line : SplitSdpLines(sdp)) {
        if (StartsWithString(line, prefix))
            ++count;
    }
    return count;
}


std::string MediaKindFromMLine(const std::string& line)
{
    if (StartsWithString(line, "m=audio "))
        return "audio";
    if (StartsWithString(line, "m=video "))
        return "video";
    if (StartsWithString(line, "m=application "))
        return "application";
    return "";
}

std::vector<std::string> ExtractOfferMediaOrder(const std::string& offer_sdp)
{
    std::vector<std::string> order;
    for (const auto& line : SplitSdpLines(offer_sdp)) {
        const std::string media = MediaKindFromMLine(line);
        if (!media.empty())
            order.push_back(media);
    }
    return order;
}

std::string ExtractOfferMid(const std::string& offer_sdp, const std::string& media)
{
    bool in_media = false;
    for (const auto& line : SplitSdpLines(offer_sdp)) {
        if (StartsWithString(line, "m="))
            in_media = MediaKindFromMLine(line) == media;
        if (in_media && StartsWithString(line, "a=mid:"))
            return line.substr(std::strlen("a=mid:"));
    }
    return "";
}

std::string ExtractOfferBundleGroup(const std::string& offer_sdp)
{
    for (const auto& line : SplitSdpLines(offer_sdp)) {
        if (StartsWithString(line, "a=group:BUNDLE "))
            return line;
    }
    return "";
}

struct SdpMediaSection {
    std::string media;
    std::vector<std::string> lines;
};

std::string AlignAnswerSdpToOffer(const std::string& answer_sdp, const std::string& offer_sdp)
{
    std::vector<std::string> session_lines;
    std::vector<SdpMediaSection> sections;
    std::vector<std::string> candidates;
    std::set<std::string> seen_candidates;

    for (const auto& line : SplitSdpLines(answer_sdp)) {
        if (StartsWithString(line, "m=")) {
            sections.push_back({MediaKindFromMLine(line), {line}});
            continue;
        }

        if (StartsWithString(line, "a=candidate:")) {
            if (seen_candidates.insert(line).second)
                candidates.push_back(line);
            continue;
        }

        if (sections.empty())
            session_lines.push_back(line);
        else
            sections.back().lines.push_back(line);
    }

    const std::string offer_bundle = ExtractOfferBundleGroup(offer_sdp);
    bool bundle_replaced = false;
    for (auto& line : session_lines) {
        if (!offer_bundle.empty() && StartsWithString(line, "a=group:BUNDLE ")) {
            line = offer_bundle;
            bundle_replaced = true;
        }
    }
    if (!offer_bundle.empty() && !bundle_replaced)
        session_lines.push_back(offer_bundle);

    for (auto& section : sections) {
        const std::string offer_mid = ExtractOfferMid(offer_sdp, section.media);
        if (offer_mid.empty())
            continue;

        bool mid_replaced = false;
        for (auto& line : section.lines) {
            if (StartsWithString(line, "a=mid:")) {
                line = "a=mid:" + offer_mid;
                mid_replaced = true;
                break;
            }
        }
        if (!mid_replaced && section.lines.size() > 1)
            section.lines.insert(section.lines.begin() + 1, "a=mid:" + offer_mid);
    }

    std::vector<SdpMediaSection> ordered;
    std::vector<bool> used(sections.size(), false);
    for (const auto& media : ExtractOfferMediaOrder(offer_sdp)) {
        for (size_t i = 0; i < sections.size(); ++i) {
            if (!used[i] && sections[i].media == media) {
                ordered.push_back(sections[i]);
                used[i] = true;
                break;
            }
        }
    }
    for (size_t i = 0; i < sections.size(); ++i) {
        if (!used[i])
            ordered.push_back(sections[i]);
    }

    if (!ordered.empty() && !candidates.empty()) {
        auto& first_section = ordered.front().lines;
        first_section.insert(first_section.end(), candidates.begin(), candidates.end());
    }

    std::vector<std::string> out = session_lines;
    for (const auto& section : ordered)
        out.insert(out.end(), section.lines.begin(), section.lines.end());
    return JoinSdpLines(out);
}

std::string AdaptAnswerSdpToOffer(
    const std::string& answer_sdp,
    const std::string& offer_sdp,
    const opennow::StreamSettings& settings,const audio::Format* selectedAudio)
{
    if(offer_sdp.size()>65536||answer_sdp.size()>65536||CountSdpLinesWithPrefix(offer_sdp,"m=video ")!=1||
       CountSdpLinesWithPrefix(answer_sdp,"m=video ")!=1)return {};
    const std::vector<std::string> offer_lines = SplitSdpLines(offer_sdp);
    const auto video_format=SelectOfferVideoFormat(offer_lines,settings);
    if(video_format.payload<0)return {};
    const bool offerHasAudio=CountSdpLinesWithPrefix(offer_sdp,"m=audio ")!=0;
    const auto audioFormat=selectedAudio?*selectedAudio:audio::selectFormat(offer_sdp,2);
    if(offerHasAudio&&!audio::validFormat(audioFormat))return {};
    std::vector<std::string> lines = SplitSdpLines(answer_sdp);
    std::vector<std::string> out;
    bool in_video = false;
    bool in_audio = false;
    bool video_bitrate_added = false;
    std::size_t video_start = 0;
    unsigned audioSections=0;
    const auto appendAudio=[&]{
        out.push_back(audioFormat.rtpmap);
        out.push_back("a=fmtp:"+std::to_string(audioFormat.payload)+" "+audioFormat.fmtp);
        if(audioFormat.redPayload>=0){out.push_back(audioFormat.redRtpmap);out.push_back(audioFormat.redFmtp);}
        out.push_back("a=recvonly");
    };
    const auto appendVideo=[&]{
        if(!video_bitrate_added)
            out.insert(out.begin()+static_cast<std::ptrdiff_t>(video_start+1),"b=AS:"+std::to_string(settings.bitrate_kbps));
        out.push_back(video_format.rtpmap);
        if(!video_format.fmtp.empty())out.push_back(video_format.fmtp);
        out.insert(out.end(),video_format.feedback.begin(),video_format.feedback.end());
        out.push_back("a=recvonly");
    };

    for (const auto& line : lines) {
        if (StartsWithString(line, "m=")) {
            if(in_audio)appendAudio();
            if(in_video)appendVideo();
            in_video = StartsWithString(line, "m=video ");
            in_audio = StartsWithString(line, "m=audio ");
            if (in_video) {
                video_start=out.size();
                out.push_back("m=video 9 UDP/TLS/RTP/SAVPF " + std::to_string(video_format.payload));
                continue;
            } else if (in_audio) {
                if(!offerHasAudio||++audioSections>1)return {};
                out.push_back("m=audio 9 UDP/TLS/RTP/SAVPF "+std::to_string(audioFormat.payload)+
                    (audioFormat.redPayload>=0?" "+std::to_string(audioFormat.redPayload):""));
                continue;
            } else if (StartsWithString(line, "m=application")) {
                out.push_back("m=application 9 UDP/DTLS/SCTP webrtc-datachannel");
                continue;
            }
        }

        if (in_audio && (StartsWithString(line,"a=rtpmap:")||StartsWithString(line,"a=fmtp:")||StartsWithString(line,"a=rtcp-fb:")))
            continue;

        if (in_video && StartsWithString(line, "c=IN ")) {
            out.push_back(line);
            if(!video_bitrate_added)out.push_back("b=AS:" + std::to_string(settings.bitrate_kbps));
            video_bitrate_added = true;
            continue;
        }

        if (in_video && (StartsWithString(line,"b=AS:")||StartsWithString(line,"b=TIAS:")||
            StartsWithString(line,"a=rtcp-fb:")||StartsWithString(line,"a=fmtp:")||StartsWithString(line,"a=rtpmap:")||
            StartsWithString(line,"a=ssrc:")||StartsWithString(line,"a=ssrc-group:")))
            continue;
        if ((in_video||in_audio)&&(line=="a=sendrecv"||line=="a=sendonly"||line=="a=recvonly"||line=="a=inactive"))
            continue;

        out.push_back(line);
    }
    if(in_audio)appendAudio();
    if(in_video)appendVideo();
    if(offerHasAudio&&!audioSections)return {};

    return AlignAnswerSdpToOffer(JoinSdpLines(out), offer_sdp);
}

std::string ExtractSdpValue(const std::string& sdp, const std::string& prefix)
{
    for (const auto& line : SplitSdpLines(sdp)) {
        if (StartsWithString(line, prefix))
            return line.substr(prefix.size());
    }
    return "";
}

int ExtractNvstIntValue(const std::string& nvst_sdp, const std::string& prefix)
{
    for (const auto& line : SplitSdpLines(nvst_sdp)) {
        if (!StartsWithString(line, prefix))
            continue;

        const std::string value = line.substr(prefix.size());
        char* end = nullptr;
        const long parsed = std::strtol(value.c_str(), &end, 10);
        if (end != value.c_str() && parsed > 0 && parsed <= 65535)
            return static_cast<int>(parsed);
    }
    return 0;
}

std::string ExtractSignalingHost(const std::string& url)
{
    size_t start = url.find("://");
    start = start == std::string::npos ? 0 : start + 3;

    size_t end = url.find('/', start);
    std::string host_port = url.substr(start, end == std::string::npos ? std::string::npos : end - start);

    const size_t at = host_port.rfind('@');
    if (at != std::string::npos)
        host_port.erase(0, at + 1);

    if (!host_port.empty() && host_port.front() == '[') {
        const size_t close = host_port.find(']');
        return close == std::string::npos ? host_port : host_port.substr(1, close - 1);
    }

    const size_t colon = host_port.find(':');
    return colon == std::string::npos ? host_port : host_port.substr(0, colon);
}

std::string DottedIpv4FromGfnHost(const std::string& host)
{
    int octets[4] = {-1, -1, -1, -1};
    size_t pos = 0;

    for (int i = 0; i < 4; ++i) {
        if (pos >= host.size() || !std::isdigit(static_cast<unsigned char>(host[pos])))
            return "";

        int value = 0;
        while (pos < host.size() && std::isdigit(static_cast<unsigned char>(host[pos]))) {
            value = value * 10 + (host[pos] - '0');
            ++pos;
        }

        if (value < 0 || value > 255)
            return "";

        octets[i] = value;
        if (i < 3) {
            if (pos >= host.size() || host[pos] != '-')
                return "";
            ++pos;
        }
    }

    return std::to_string(octets[0]) + "." + std::to_string(octets[1]) + "." +
           std::to_string(octets[2]) + "." + std::to_string(octets[3]);
}

std::string NormalizeGfnMediaIp(const std::string& host)
{
    std::string media_ip = DottedIpv4FromGfnHost(host);
    return media_ip.empty() ? host : media_ip;
}

std::string RewriteGfnMediaCandidate(
    std::string candidate,
    const std::string& media_ip_hint,
    int media_port_hint,
    const std::string& signaling_url)
{
    if (media_port_hint < 0 || media_port_hint > 65535)
        return candidate;
    if (!StartsWithString(candidate, "a=candidate:") && !StartsWithString(candidate, "candidate:"))
        return candidate;

    size_t pos = 0, endpoint_start = 0, address_end = 0;
    for (unsigned token = 0; token < 6; ++token) {
        while (pos < candidate.size() && std::isspace(static_cast<unsigned char>(candidate[pos]))) ++pos;
        if (pos == candidate.size()) return candidate;
        if (token == 4) endpoint_start = pos;
        const auto start = pos;
        while (pos < candidate.size() && !std::isspace(static_cast<unsigned char>(candidate[pos]))) ++pos;
        if (token == 4) address_end = pos;
        if (token == 5 && candidate.find_first_not_of("0123456789", start) < pos) return candidate;
    }
    const bool mapped_endpoint = !media_ip_hint.empty() && media_port_hint > 0;
    const auto address = candidate.substr(endpoint_start, address_end - endpoint_start);
    unsigned a, b, c, d;
    char extra;
    if (std::sscanf(address.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) != 4 ||
        a > 255 || b > 255 || c > 255 || d > 255)
        return candidate;
    const bool placeholder = a == 0 && b == 0 && c == 0 && d == 0;
    const bool unroutable = a == 10 || a == 127 || (a == 169 && b == 254) ||
        (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) ||
        (a >= 224 && a <= 239) || (a == 100 && b >= 64 && b <= 127);
    if (!placeholder && !(mapped_endpoint && unroutable))
        return candidate;
    const auto media_ip = NormalizeGfnMediaIp(media_ip_hint.empty() ? ExtractSignalingHost(signaling_url) : media_ip_hint);
    if (std::sscanf(media_ip.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) != 4 ||
        a > 255 || b > 255 || c > 255 || d > 255 || media_ip == "0.0.0.0")
        return candidate;
    if (mapped_endpoint)
        candidate.replace(endpoint_start, pos - endpoint_start, media_ip + " " + std::to_string(media_port_hint));
    else
        candidate.replace(endpoint_start, address_end - endpoint_start, media_ip);
    return candidate;
}

std::string BuildManualMediaCandidate(
    const std::string& signaling_url,
    const std::string& media_ip_hint,
    int media_port_hint,
    int foundation)
{
    if (media_ip_hint.empty() || media_port_hint <= 0) return {};
    const auto candidate = "a=candidate:" + std::to_string(foundation) + " 1 UDP 2130706431 0.0.0.0 9 typ host";
    const auto rewritten = RewriteGfnMediaCandidate(candidate, media_ip_hint, media_port_hint, signaling_url);
    return rewritten == candidate ? std::string{} : rewritten;
}

std::string PrepareGfnOfferSdp(
    std::string sdp,
    const std::string& signaling_url,
    const std::string& media_ip_hint,
    int media_port_hint)
{
    std::string prepared;
    size_t start = 0;
    while (start < sdp.size()) {
        auto end = sdp.find('\n', start);
        if (end == std::string::npos) end = sdp.size();
        auto line = sdp.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        prepared += RewriteGfnMediaCandidate(line, media_ip_hint, media_port_hint, signaling_url);
        prepared += "\r\n";
        start = end + 1;
    }
    return prepared;
}



}
