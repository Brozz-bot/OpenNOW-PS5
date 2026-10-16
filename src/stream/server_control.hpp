// SPDX-License-Identifier: GPL-3.0-or-later
// ServerControl formats adapted from OpenNOW native streamer, MIT, Copyright (c) 2025 Zortos (assets/licenses/OpenNOW-Android.txt).
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>

namespace opennow {
struct SessionTimer {
 std::uint8_t timerType=0;
 std::uint32_t secondsLeft=0;
 bool active=false;
 std::uint64_t receivedAt=0;
};
struct ServerTermination {
 bool present=false;
 std::uint32_t wireCode=0;
 std::optional<std::uint32_t> reason,extendedCode;
 std::optional<bool> resumable;
};
namespace control {
inline std::uint32_t le32(const std::uint8_t* p) noexcept {
 return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);
}
inline ServerTermination termination(const std::uint8_t* p) noexcept {
 ServerTermination t;
 t.present=true;t.wireCode=(std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|(std::uint32_t(p[2])<<8)|p[3];
 if(t.wireCode==0x80030023){t.reason=0x38;t.extendedCode=0x80030007;t.resumable=false;}
 else if(t.wireCode==0x80040010){t.reason=0x37;t.extendedCode=0x80040010;t.resumable=true;}
 return t;
}
struct Command {
 std::uint16_t code=0;
 const std::uint8_t* payload=nullptr;
 std::size_t size=0;
};
class Reader {
public:
 Reader(const std::uint8_t* bytes,std::size_t size):bytes_(bytes),size_(size),valid_((bytes||!size)&&size<=65539){}
 bool next(Command& command) noexcept {
  if(!valid_||!size_)return false;
  if(size_<4||count_==256){valid_=false;return false;}
  const auto size=std::size_t(bytes_[2])|(std::size_t(bytes_[3])<<8);
  if(size>size_-4){valid_=false;return false;}
  command={static_cast<std::uint16_t>(bytes_[0]|(bytes_[1]<<8)),bytes_+4,size};
  bytes_+=4+size;size_-=4+size;++count_;return true;
 }
 bool valid() const noexcept {return valid_;}
private:
 const std::uint8_t* bytes_;
 std::size_t size_;
 unsigned count_=0;
 bool valid_;
};
}
}
