// SPDX-License-Identifier: GPL-3.0-or-later
#include "input/rumble_output.hpp"
#include <cassert>
#include <cstdio>
#include <limits>

int main() {
    using namespace opennow;
    const auto off=[](PS5_PadVibration m){return !m.large_motor&&!m.small_motor;};
    RumbleOutput output;
    RumbleNotice notice{{0,65535,32768,200},1000,1,1};
    auto motors=output.update(notice,1,true,100,1000);assert(motors.large_motor==255&&motors.small_motor==128);
    assert(!off(output.update(notice,1,true,100,200999)));
    assert(off(output.update(notice,1,true,100,201000)));
    notice={Rumble{0,65535,32768,1000},202000,2,1};
    assert(!off(output.update(notice,1,true,100,202000)));
    assert(off(output.update(notice,1,false,100,203000)));
    assert(off(output.update(notice,1,true,100,204000)));
    ++notice.revision;notice.receivedAt=205000;
    assert(!off(output.update(notice,1,true,100,205000)));
    assert(off(output.update(notice,2,true,100,206000)));
    ++notice.revision;notice.session=1;notice.receivedAt=207000;
    assert(off(output.update(notice,2,true,100,207000)));
    ++notice.revision;notice.session=2;notice.receivedAt=208000;
    assert(!off(output.update(notice,2,true,100,208000)));
    assert(off(output.update(notice,2,true,50,209000)));
    ++notice.revision;notice.receivedAt=210000;
    motors=output.update(notice,2,true,50,210000);assert(motors.large_motor==127&&motors.small_motor==64);
    ++notice.revision;notice.command.lowFrequency=notice.command.highFrequency=0;
    assert(off(output.update(notice,2,true,50,211000)));
    ++notice.revision;notice.command.controllerId=1;notice.command.lowFrequency=65535;
    assert(off(output.update(notice,2,true,50,212000)));
    ++notice.revision;notice.command.controllerId=0;notice.receivedAt=214000;
    assert(off(output.update(notice,2,true,50,213000)));
    ++notice.revision;notice.receivedAt=100;notice.command.durationMs=1;
    assert(off(output.update(notice,2,true,50,214000)));
    ++notice.revision;notice.receivedAt=std::numeric_limits<std::uint64_t>::max()-100;
    assert(!off(output.update(notice,2,true,50,notice.receivedAt)));
    assert(off(output.update(notice,2,false,50,notice.receivedAt)));
    std::puts("Rumble expiry, session isolation, disconnect/capture stops and intensity changes passed");
}
