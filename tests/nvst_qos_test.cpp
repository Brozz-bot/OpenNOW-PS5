// SPDX-License-Identifier: GPL-3.0-or-later
#include "stream/nvst_qos.hpp"
#include <cassert>
#include <cstdint>
#include <limits>
#include <string>
using namespace opennow::webrtc;
static unsigned word(const std::array<std::uint8_t,56>& p,unsigned at){return p[at]|unsigned(p[at+1])<<8|unsigned(p[at+2])<<16|unsigned(p[at+3])<<24;}
int main(){
 const auto fixture=qosPacket({6,1818674});
 const std::string hex="07023400" "07000000" "00000000" "06000000" "00000000" "00000000" "00000000"
                       "00000000" "0000e803" "e8030000" "32c01b00" "00000000" "00000000" "00000000";
 assert(hex.size()==fixture.size()*2);
 for(unsigned i=0;i<fixture.size();++i)assert(fixture[i]==std::stoul(hex.substr(i*2,2),nullptr,16));
 QosFeedback f;
 assert(f.queuedCount()==0);
 for(unsigned i=1;i<=5;++i){
  const auto sample=f.next(i*QosFeedback::interval_us);
  const auto packet=f.packet(sample);
  assert(sample.sequence==i&&word(packet,12)==i);
  assert(word(packet,40)==i*4500);
  for(unsigned offset:{16u,20u,24u,28u,44u,48u,52u})assert(word(packet,offset)==0);
  assert(packet[32]==0&&packet[33]==0&&packet[38]==0&&packet[39]==0);
  f.queued(sample);
  assert(f.queuedCount()==i);
 }
 const auto failed=f.next(300000);
 const auto retry=f.next(350000);
 assert(failed.sequence==6&&retry.sequence==6&&f.queuedCount()==5);
 assert(word(f.packet(retry),40)==31500);
 f.queued(retry);
 assert(f.next(350000).sequence==7);
 assert(f.next(0).client_time_90khz==0);
 assert(f.next(999).client_time_90khz==0);
 assert(f.next(1000).client_time_90khz==90);
 assert(f.next(55556).client_time_90khz==4950);
 assert(f.next(47721858000ULL).client_time_90khz==4294967220u);
 assert(f.next(47721859000ULL).client_time_90khz==14);
 constexpr auto longest=std::numeric_limits<std::uint64_t>::max();
 assert(f.next(longest).client_time_90khz==static_cast<std::uint32_t>((longest/1000)*90));
 f.queued({UINT32_MAX,0});
 assert(f.next(400000).sequence==0);
 f={};
 assert(f.queuedCount()==0&&f.next(0).sequence==1);
}
