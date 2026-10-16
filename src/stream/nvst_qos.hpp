// SPDX-License-Identifier: GPL-3.0-or-later
// Byte layout and successful-send accounting adapted from OpenNOW commit
// 60ce69a07bf94ba484af7406d386ccb28e5a13e6 (nvst_control.rs, nvst.rs).
#pragma once
#include <array>
#include <cstdint>
namespace opennow::webrtc {
struct QosSample { std::uint32_t sequence=0,client_time_90khz=0; };
inline std::array<std::uint8_t,56> qosPacket(const QosSample& current) {
 std::array<std::uint8_t,56> p{};
 auto put=[&](unsigned at,std::uint32_t value,unsigned n=4){while(n--){p[at++]=value&255;value>>=8;}};
 put(0,0x207,2);put(2,52,2);
 put(4,7);put(12,current.sequence);
 put(34,1000,2);put(36,1000,2);put(40,current.client_time_90khz);
 return p;
}
class QosFeedback {
public:
 static constexpr std::uint64_t interval_us=50'000;
 QosSample next(std::uint64_t elapsed_us) const {return {sequence_+1,static_cast<std::uint32_t>((elapsed_us/1000)*90)};}
 auto packet(const QosSample& sample) const {return qosPacket(sample);}
 void queued(const QosSample& sample){sequence_=sample.sequence;}
 std::uint32_t queuedCount() const {return sequence_;}
private:
 std::uint32_t sequence_=0;
};
}
