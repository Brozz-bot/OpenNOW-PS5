// SPDX-License-Identifier: GPL-3.0-or-later
#include "stream/input_wire.hpp"
#include "input/input_queue.hpp"
#include "input/stream_keyboard.hpp"
#include "input/touch_mouse.hpp"
#include "input/launcher_mouse.hpp"

#include <cassert>
#include <cstring>

namespace {
using opennow::wire::Bytes;
constexpr std::uint64_t stamp=0x0102030405060708ULL;
#define T8 0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08

void wireFixtures() {
    using namespace opennow::wire;
    assert((key(true,0x41,0x1e,1,3,stamp)==Bytes{0x23,T8,0x22,0x03,0,0,0,0x00,0x41,0x00,0x01,0x00,0x1e,T8}));
    assert((key(true,0x41,0x1e,1,2,stamp)==Bytes{0x03,0,0,0,0x00,0x41,0x00,0x01,0x00,0x1e,T8}));
    assert((key(false,0x0d,0x1c,0,2,0x10)==Bytes{0x04,0,0,0,0x00,0x0d,0,0,0x00,0x1c,0,0,0,0,0,0,0,0x10}));
    assert((key(false,0x26,0x148,0,3,stamp)==Bytes{0x23,T8,0x22,0x04,0,0,0,0x00,0x26,0,0,0x01,0x48,T8}));
    assert((mouseMove(-5,300,3,stamp)==Bytes{0x23,T8,0x21,0x00,0x16,0x07,0,0,0,0xff,0xfb,0x01,0x2c,0,0,0,0,0,0,T8}));
    assert((mouseMove(-32768,32767,2,stamp)==Bytes{0x07,0,0,0,0x80,0x00,0x7f,0xff,0,0,0,0,0,0,T8}));
    assert((mouseButton(true,3,3,stamp)==Bytes{0x23,T8,0x22,0x08,0,0,0,0x03,0,0,0,0,0,T8}));
    assert((mouseButton(false,1,2,stamp)==Bytes{0x09,0,0,0,0x01,0,0,0,0,0,T8}));
    assert((mouseWheel(-120,120,2,stamp)==Bytes{0x0a,0,0,0,0xff,0x88,0,0x78,0,0,0,0,0,0,T8}));
    assert((mouseWheel(120,-120,3,stamp)==Bytes{0x23,T8,0x22,0x0a,0,0,0,0,0x78,0xff,0x88,0,0,0,0,0,0,T8}));
}

void gamepadFixtures() {
    using namespace opennow;
    const GamepadState state{0x1234,0xabcd,-32767,32767,-1,1,true};
    const Bytes event{12,0,0,0,26,0,0,0,1,1,20,0,0x34,0x12,0xcd,0xab,
                      1,0x80,0xff,0x7f,0xff,0xff,1,0,0,0,85,0,0,0,
                      8,7,6,5,4,3,2,1};
    assert(wire::gamepad(state,2,stamp)==event);
    Bytes framed{0x23,T8,0x21,0,38};
    framed.insert(framed.end(),event.begin(),event.end());
    assert(wire::gamepad(state,3,stamp)==framed);
    assert(wire::gamepad(state,4,stamp)==framed);
    assert(state.neutral()==0);
    GamepadState neutral;
    assert(neutral.neutral()==3);
    const auto disconnected=wire::gamepad(neutral,2,stamp);
    assert(disconnected.size()==38&&disconnected[6]==0&&disconnected[7]==0);
    assert(disconnected[8]==0&&disconnected[9]==0);
    for(unsigned i=12;i<24;++i)assert(disconnected[i]==0);
    neutral.connected=true;
    auto connected=disconnected;connected[8]=connected[9]=1;
    assert(wire::gamepad(neutral,2,stamp)==connected);
    neutral.lx=1;assert(neutral.neutral()==2);
    neutral.rx=1;assert(neutral.neutral()==0);
    neutral.lx=0;assert(neutral.neutral()==1);
}

void gamepadMapping() {
    using opennow::gamepadState;
    PS5_PadData pad{};
    pad.buttons=0xffffffff;pad.analogButtons={255,255};
    auto state=gamepadState(pad,5,5);
    assert(!state.connected&&!state.buttons&&!state.triggers&&state.neutral()==3);
    pad.connected=true;pad.leftStick={128,128};pad.rightStick={128,128};
    const unsigned ps[]={PS5_PAD_BUTTON_UP,PS5_PAD_BUTTON_DOWN,PS5_PAD_BUTTON_LEFT,PS5_PAD_BUTTON_RIGHT,
                         PS5_PAD_BUTTON_OPTIONS,PS5_PAD_BUTTON_TOUCH_PAD,PS5_PAD_BUTTON_L3,PS5_PAD_BUTTON_R3,
                         PS5_PAD_BUTTON_L1,PS5_PAD_BUTTON_R1,PS5_PAD_BUTTON_CROSS,PS5_PAD_BUTTON_CIRCLE,
                         PS5_PAD_BUTTON_SQUARE,PS5_PAD_BUTTON_TRIANGLE};
    const unsigned xb[]={1,2,4,8,16,32,64,128,256,512,4096,8192,16384,32768};
    for(unsigned i=0;i<14;++i) {
        pad.buttons=ps[i];state=gamepadState(pad,5,5);
        assert(state.connected&&state.buttons==xb[i]&&state.triggers==65535&&state.neutral()==3);
    }
    pad.buttons=PS5_PAD_BUTTON_L2|PS5_PAD_BUTTON_R2;pad.analogButtons={0x12,0x34};
    state=gamepadState(pad,5,5);
    assert(!state.buttons&&state.triggers==0x3412);
    pad.buttons=0;
    pad.leftStick={0,128};pad.rightStick={128,0};
    state=gamepadState(pad,0,0);
    assert(state.lx==-32767&&state.ly==0&&state.rx==0&&state.ry==32767);
    pad.leftStick={255,128};pad.rightStick={128,255};
    state=gamepadState(pad,50,50);
    assert(state.lx==32767&&state.ly==0&&state.rx==0&&state.ry==-32767);
    pad.leftStick={134,122};pad.rightStick={128,128};
    state=gamepadState(pad,5,5);
    assert(state.lx==405&&state.ly==402&&state.neutral()==2);
    pad.leftStick={134,128};pad.rightStick={134,128};
    state=gamepadState(pad,5,0);
    assert(!state.lx&&state.rx==1548&&state.neutral()==1);
    pad.leftStick={135,128};pad.rightStick={135,128};
    state=gamepadState(pad,5,50);
    assert(state.lx==177&&state.rx==0&&state.neutral()==2);
    pad.leftStick={192,192};pad.rightStick={0,0};
    state=gamepadState(pad,50,5);
    assert(state.lx==9855&&state.ly==-9855&&state.rx==-32767&&state.ry==32767);
}

void mouseSensitivity() {
    using namespace opennow::wire;
    assert((mouseMove(5,-5,2,stamp,50)==Bytes{7,0,0,0,0,3,0xff,0xfd,0,0,0,0,0,0,T8}));
    assert((mouseMove(1,-1,3,stamp,150)==Bytes{0x23,T8,0x21,0,22,7,0,0,0,0,2,0xff,0xfe,0,0,0,0,0,0,T8}));
    assert((mouseMove(32767,-32768,2,stamp,300)==Bytes{7,0,0,0,0x7f,0xff,0x80,0,0,0,0,0,0,0,T8}));
    assert((mouseMove(4,-4,2,stamp,10)==Bytes{7,0,0,0,0,0,0,0,0,0,0,0,0,0,T8}));
    assert(mouseMove(32767,-32768,3,stamp,100)==mouseMove(32767,-32768,3,stamp));
}

Bytes rumbleCommand(unsigned kind,unsigned blobLength,const Bytes& records) {
    Bytes command{0x0b,1,static_cast<std::uint8_t>(records.size()+4),0,
                  static_cast<std::uint8_t>(kind),0,static_cast<std::uint8_t>(blobLength),0};
    command.insert(command.end(),records.begin(),records.end());
    return command;
}

void rumbleFixtures() {
    using opennow::wire::decodeRumble;
    auto packet=rumbleCommand(1,12,{0,0,0x34,0x12,0xcd,0xab,3,0,0xff,0xff,0,0});
    auto commands=decodeRumble(packet.data(),packet.size());
    assert(commands[0]&&commands[0]->controllerId==0&&commands[0]->lowFrequency==0x1234);
    assert(commands[0]->highFrequency==0xabcd&&commands[0]->durationMs==1000);
    assert(!commands[1]&&!commands[2]);
    assert(commands[3]&&commands[3]->controllerId==3&&commands[3]->lowFrequency==65535);
    assert(commands[3]->highFrequency==0&&commands[3]->durationMs==1000);
    packet=rumbleCommand(2,16,{1,0,1,0,2,0,0x34,0x12,2,0,3,0,4,0,0,0});
    commands=decodeRumble(packet.data(),packet.size());
    assert(commands[1]&&commands[1]->lowFrequency==1&&commands[1]->highFrequency==2&&commands[1]->durationMs==0x1234);
    assert(commands[2]&&commands[2]->durationMs==1000);
    auto stop=rumbleCommand(2,8,{1,0,0,0,0,0,0xff,0xff});
    packet.insert(packet.end(),stop.begin(),stop.end());
    commands=decodeRumble(packet.data(),packet.size());
    assert(commands[1]&&!commands[1]->lowFrequency&&!commands[1]->highFrequency&&commands[1]->durationMs==65535);
    assert(commands[2]&&commands[2]->lowFrequency==3);
    packet=rumbleCommand(1,12,{0,0,1,0,2,0,0,0,3,0,4,0});
    commands=decodeRumble(packet.data(),packet.size());
    assert(commands[0]&&commands[0]->lowFrequency==3&&commands[0]->highFrequency==4);
}

bool noRumble(const opennow::RumbleBatch& commands) {
    for(const auto& command:commands)if(command)return false;
    return true;
}

void malformedRumble() {
    using opennow::wire::decodeRumble;
    assert(noRumble(decodeRumble(nullptr,0)));
    const auto full=rumbleCommand(2,8,{0,0,0xff,0xff,0,0,1,0});
    assert(noRumble(decodeRumble(nullptr,full.size())));
    for(unsigned n=0;n<full.size();++n)assert(noRumble(decodeRumble(full.data(),n)));
    auto packet=full;packet[2]=0xff;packet[3]=0xff;
    assert(noRumble(decodeRumble(packet.data(),packet.size())));
    packet=full;packet[0]=0xff;
    assert(noRumble(decodeRumble(packet.data(),packet.size())));
    packet={0x0b,1,0,0};
    assert(noRumble(decodeRumble(packet.data(),packet.size())));
    packet={0x0b,1,2,0,1,0};
    assert(noRumble(decodeRumble(packet.data(),packet.size())));
    packet=rumbleCommand(3,8,{0,0,0,0,1,0,2,0});
    assert(noRumble(decodeRumble(packet.data(),packet.size())));
    packet=rumbleCommand(1,6,{4,0,1,0,2,0});
    assert(noRumble(decodeRumble(packet.data(),packet.size())));
    packet=rumbleCommand(1,6,{0,1,1,0,2,0});
    assert(noRumble(decodeRumble(packet.data(),packet.size())));
    packet=rumbleCommand(1,5,{0,0,1,0,2,0});
    assert(noRumble(decodeRumble(packet.data(),packet.size())));
    packet=rumbleCommand(2,8,{0,0,1,0,2,0,1});
    assert(noRumble(decodeRumble(packet.data(),packet.size())));
    packet=rumbleCommand(1,255,{0,0,1,0,2,0,0xff});
    auto commands=decodeRumble(packet.data(),packet.size());
    assert(commands[0]&&commands[0]->lowFrequency==1&&commands[0]->highFrequency==2);
    packet=rumbleCommand(1,6,{0,0,1,0,2,0,1,0,3,0,4,0});
    commands=decodeRumble(packet.data(),packet.size());
    assert(commands[0]&&!commands[1]);
    packet={0x20,3,3,0,1,2,3};packet.insert(packet.end(),full.begin(),full.end());
    commands=decodeRumble(packet.data(),packet.size());
    assert(commands[0]&&commands[0]->lowFrequency==65535);
    packet=full;packet.insert(packet.end(),{0x0b,1,0xff,0xff,1,2});
    commands=decodeRumble(packet.data(),packet.size());
    assert(commands[0]&&commands[0]->lowFrequency==65535);
    packet={0x20,3,0xff,0xff};packet.insert(packet.end(),full.begin(),full.end());
    assert(noRumble(decodeRumble(packet.data(),packet.size())));
    packet={0x20,3,static_cast<std::uint8_t>(full.size()),0};packet.insert(packet.end(),full.begin(),full.end());
    assert(noRumble(decodeRumble(packet.data(),packet.size())));
}

void rumbleScaling() {
    const opennow::Rumble command{0,65535,32768,1234};
    auto motors=opennow::rumbleMotors(command,100);
    assert(motors.large_motor==255&&motors.small_motor==128);
    motors=opennow::rumbleMotors(command,50);
    assert(motors.large_motor==127&&motors.small_motor==64);
    motors=opennow::rumbleMotors(command,0);
    assert(!motors.large_motor&&!motors.small_motor);
    motors=opennow::rumbleMotors({0,256,255,1000},100);
    assert(motors.large_motor==1&&!motors.small_motor);
    motors=opennow::rumbleMotors({0,0,0,1000},100);
    assert(!motors.large_motor&&!motors.small_motor);
}

void wheelQueueScenarios() {
    opennow::InputQueue queue;
    opennow::InputEvent event;
    assert(queue.wheel(-99999,99999));
    assert(queue.take(event,false));
    assert(event.kind==opennow::InputEvent::Kind::wheel&&event.dx==-32768&&event.dy==32767);
    for(unsigned i=0;i<opennow::InputQueue::capacity;++i)assert(queue.wheel(0,120));
    assert(!queue.wheel(120,0));
    queue.cancel();
    assert(queue.takeCancel()&&!queue.takeCancel()&&!queue.take(event,true));
    queue.move(65535,-65535);
    queue.move(INT32_MAX,INT32_MIN);
    assert(queue.take(event,true)&&event.dx==65535&&event.dy==-65535);
}

PS5_PadData touchPad(unsigned fingers,std::uint8_t id0,std::uint16_t x0,std::uint16_t y0,std::uint8_t id1=0,std::uint16_t x1=0,std::uint16_t y1=0) {
    PS5_PadData pad{};
    pad.connected=true;pad.touch.fingers=static_cast<std::uint8_t>(fingers);
    pad.touch.touch[0]={x0,y0,id0,{}};pad.touch.touch[1]={x1,y1,id1,{}};
    return pad;
}

void touchScenarios() {
    opennow::TouchMouse mouse;
    auto m=mouse.update(touchPad(1,7,100,100),true);
    assert(!m.dx&&!m.dy&&!mouse.held);
    m=mouse.update(touchPad(1,7,130,90),true);
    assert(m.dx==30&&m.dy==-10);
    m=mouse.update(touchPad(1,7,1900,90),true);
    assert(m.dx==opennow::TouchMouse::maxStep&&m.dy==0);
    m=mouse.update(touchPad(1,8,50,50),true);
    assert(!m.dx&&!m.dy);
    m=mouse.update(touchPad(2,9,10,10,8,60,55),true);
    assert(!m.dx&&!m.dy);
    m=mouse.update(touchPad(2,9,10,10,8,70,65),true);
    assert(m.dx==0&&m.dy==0);
    m=mouse.update(touchPad(2,8,72,70,9,12,15),true);
    assert(m.dx==2&&m.dy==5);
    m=mouse.update(touchPad(1,9,12,12),true);
    assert(!m.dx&&!m.dy);
    m=mouse.update(touchPad(0,0,0,0),true);
    assert(!m.dx&&!m.dy&&mouse.finger==-1);
    m=mouse.update(touchPad(1,10,500,500),true);
    assert(!m.dx&&!m.dy);

    auto pad=touchPad(1,10,500,500);pad.buttons=PS5_PAD_BUTTON_TOUCH_PAD;
    mouse.update(pad,true);
    assert(mouse.held==opennow::TouchMouse::left);
    pad=touchPad(2,10,500,500,11,600,600);pad.buttons=PS5_PAD_BUTTON_TOUCH_PAD;
    mouse.update(pad,true);
    assert(mouse.held==opennow::TouchMouse::left);
    pad.buttons=0;mouse.update(pad,true);
    assert(!mouse.held);
    pad.buttons=PS5_PAD_BUTTON_TOUCH_PAD;mouse.update(pad,true);
    assert(mouse.held==opennow::TouchMouse::right);
    pad=touchPad(1,10,500,500);pad.buttons=PS5_PAD_BUTTON_TOUCH_PAD;mouse.update(pad,true);
    assert(mouse.held==opennow::TouchMouse::right);
    pad.buttons=0;mouse.update(pad,true);
    assert(!mouse.held);

    pad.buttons=PS5_PAD_BUTTON_TOUCH_PAD|PS5_PAD_BUTTON_OPTIONS;mouse.update(pad,true);
    assert(!mouse.held);
    pad.buttons=PS5_PAD_BUTTON_TOUCH_PAD;mouse.update(pad,true);
    assert(!mouse.held);
    pad.buttons=0;mouse.update(pad,true);

    pad.buttons=PS5_PAD_BUTTON_TOUCH_PAD;mouse.update(pad,true);
    assert(mouse.held==opennow::TouchMouse::left);
    PS5_PadData gone{};
    m=mouse.update(gone,true);
    assert(!mouse.held&&!m.dx&&!m.dy&&mouse.finger==-1);
    mouse.update(pad,true);
    assert(!mouse.held);
    pad.buttons=0;mouse.update(pad,true);
    pad.buttons=PS5_PAD_BUTTON_TOUCH_PAD;mouse.update(pad,true);
    assert(mouse.held==opennow::TouchMouse::left);
    m=mouse.update(touchPad(1,10,900,900),false);
    assert(!mouse.held&&!m.dx&&!m.dy);
    m=mouse.update(touchPad(1,10,950,900),true);
    assert(!m.dx&&!m.dy);
}

void queueScenarios() {
    using opennow::InputEvent;
    opennow::InputQueue queue;
    InputEvent e;
    assert(!queue.take(e,true));
    queue.move(0,0);
    assert(queue.size()==0);
    queue.move(3,4);queue.move(-1,2);
    assert(queue.size()==1);
    queue.move(70000,-70000);
    assert(queue.take(e,true)&&e.kind==InputEvent::Kind::move&&e.dx==65535&&e.dy==-65535);
    assert(queue.button(1,true));
    queue.move(5,0);
    assert(queue.key({0x41,0x1e,0}));
    queue.move(1,1);
    assert(queue.size()==4);
    assert(queue.take(e,false)&&e.kind==InputEvent::Kind::button&&e.button==1&&e.down);
    assert(queue.take(e,false)&&e.kind==InputEvent::Kind::move&&e.dx==5);
    assert(!queue.take(e,false));
    assert(queue.take(e,true)&&e.kind==InputEvent::Kind::key&&e.key.vk==0x41);
    assert(queue.take(e,false)&&e.dx==1&&e.dy==1);
    unsigned accepted=0;
    while(queue.key({0x42,0x30,0}))++accepted;
    assert(accepted==opennow::InputQueue::keyLimit);
    unsigned buttons=0;
    while(queue.button(1,(buttons&1)==0))++buttons;
    assert(queue.size()==opennow::InputQueue::capacity&&buttons==opennow::InputQueue::capacity-opennow::InputQueue::keyLimit);
    queue.move(9,9);
    assert(queue.size()==opennow::InputQueue::capacity);
    queue.cancel();
    assert(queue.size()==0);
    assert(queue.key({0x43,0x2e,0})&&queue.button(1,false));
    assert(queue.take(e,false)&&e.kind==InputEvent::Kind::cancel);
    assert(!queue.take(e,false));
    assert(queue.take(e,true)&&e.kind==InputEvent::Kind::key&&e.key.vk==0x43);
    assert(queue.take(e,false)&&e.kind==InputEvent::Kind::button&&!e.down&&!queue.take(e,true));
    for(unsigned i=0;i<opennow::InputQueue::capacity;++i)assert(queue.button(1,(i&1)==0));
    queue.cancel();queue.cancel();
    assert(queue.key({0x44,0x20,0}));
    queue.clear();
    assert(queue.size()==0&&queue.take(e,false)&&e.kind==InputEvent::Kind::cancel&&!queue.take(e,true));
}

unsigned find(const char* label) {
    for(unsigned i=0;i<opennow::StreamKeyboard::count;++i)if(!std::strcmp(opennow::StreamKeyboard::keys[i].label,label))return i;
    assert(false);
    return 0;
}

void keyboardScenarios() {
    using K=opennow::StreamKeyboard;
    for(unsigned r=0;r<K::rows;++r) {
        unsigned width=0;
        for(unsigned i=K::rowStart[r];i<K::rowStart[r+1];++i)width+=K::keys[i].width;
        assert(width==K::units);
    }
    K k;
    k.show();
    assert(k.open&&!std::strcmp(k.label(k.selected),"q"));
    k.move(-1,0);assert(!std::strcmp(k.label(k.selected),"Tab"));
    k.move(-1,0);assert(!std::strcmp(k.label(k.selected),"Tab"));
    k.move(0,-1);assert(!std::strcmp(k.label(k.selected),"1"));
    k.move(0,-1);assert(!std::strcmp(k.label(k.selected),"1"));
    k.selected=find("Backspace");k.move(1,0);assert(k.selected==find("Backspace"));
    k.move(0,1);assert(!std::strcmp(k.label(k.selected),"\\"));
    k.selected=find("g");k.move(0,1);assert(!std::strcmp(k.label(k.selected),"v"));
    k.move(0,1);assert(!std::strcmp(k.label(k.selected),"Space"));
    k.move(0,1);assert(!std::strcmp(k.label(k.selected),"Space"));

    opennow::InputQueue queue;
    opennow::InputEvent e;
    assert(k.press(find("h"),queue));
    assert(queue.take(e,true)&&e.key.vk==0x48&&e.key.scan==0x23&&e.key.modifiers==0);
    assert(k.press(find("Shift"),queue)&&k.shift&&queue.size()==0);
    assert(!std::strcmp(k.label(find("h")),"H")&&!std::strcmp(k.label(find("1")),"!"));
    assert(k.press(find("1"),queue)&&!k.shift);
    assert(queue.take(e,true)&&e.key.vk==0x31&&e.key.scan==0x02&&e.key.modifiers==opennow::modifierShift);
    assert(k.press(find("Caps"),queue)&&k.caps);
    assert(!std::strcmp(k.label(find("h")),"H")&&!std::strcmp(k.label(find("1")),"1"));
    assert(k.press(find("i"),queue));
    assert(queue.take(e,true)&&e.key.vk==0x49&&e.key.modifiers==opennow::modifierShift);
    assert(k.press(find("Shift"),queue)&&k.press(find("i"),queue));
    assert(queue.take(e,true)&&e.key.modifiers==0);
    assert(k.press(find("Caps"),queue)&&!k.caps);
    assert(k.press(find("Ctrl"),queue)&&k.press(find("Alt"),queue)&&k.latched(find("Ctrl"))&&k.latched(find("Alt")));
    assert(k.press(find("a"),queue)&&!k.ctrl&&!k.alt);
    assert(queue.take(e,true)&&e.key.vk==0x41&&e.key.modifiers==(opennow::modifierCtrl|opennow::modifierAlt));
    assert(k.press(K::spaceKey,queue)&&k.press(find("Tab"),queue)&&k.press(find("Esc"),queue)&&k.press(find("Left"),queue));
    assert(queue.take(e,true)&&e.key.vk==0x20&&e.key.scan==0x39);
    assert(queue.take(e,true)&&e.key.vk==0x09&&e.key.scan==0x0f);
    assert(queue.take(e,true)&&e.key.vk==0x1b&&e.key.scan==0x01);
    assert(queue.take(e,true)&&e.key.vk==0x25&&e.key.scan==0x14b);
    assert(k.press(K::backspaceKey,queue));
    assert(queue.take(e,true)&&e.key.vk==0x08&&e.key.scan==0x0e);
    assert(k.press(find("Enter"),queue));
    assert(queue.take(e,true)&&e.key.vk==0x0d&&e.key.scan==0x1c);

    for(unsigned i=0;i<opennow::InputQueue::keyLimit;++i)assert(k.press(find("x"),queue));
    k.press(find("Shift"),queue);
    assert(!k.press(find("y"),queue)&&k.shift);
    k.hide();
    assert(!k.open&&!k.shift&&!k.ctrl&&!k.alt);
}
}

void launcherScenarios() {
    opennow::LauncherMouse mouse;
    PS5_PadData pad{};pad.connected=true;pad.leftStick={128,128};pad.rightStick={255,128};
    auto f=mouse.update(pad,1000000,true);
    assert(!f.dx&&!f.dy&&!f.left&&!f.right&&!f.wheelX&&!f.wheelY);
    mouse.active=true;
    f=mouse.update(pad,1000000,true);
    assert(f.dx==11&&f.dy==0);
    f=mouse.update(pad,1016000,true);
    assert(f.dx==11);
    f=mouse.update(pad,1032000,true);
    assert(f.dx==11||f.dx==12);
    pad.rightStick={128+15,128-15};
    f=mouse.update(pad,1048000,true);
    assert(f.dx==0&&f.dy==0);
    pad.rightStick={0,128};mouse.speed=2;
    f=mouse.update(pad,1148000,true);
    assert(f.dx<=-70&&f.dx>=-71);
    mouse.cycleSpeed();assert(mouse.speed==0);
    pad.rightStick={128,255};
    f=mouse.update(pad,1164000,true);
    assert(f.dy==4&&f.dx==0);
    pad.rightStick={128,128};
    pad.buttons=PS5_PAD_BUTTON_R2;pad.analogButtons={40,0};
    f=mouse.update(pad,1180000,true);
    assert(f.left&&f.right);
    pad.buttons=0;pad.analogButtons={0,33};
    f=mouse.update(pad,1196000,true);
    assert(f.left&&!f.right);
    pad.analogButtons={0,32};
    pad.buttons=PS5_PAD_BUTTON_UP|PS5_PAD_BUTTON_LEFT;
    f=mouse.update(pad,1212000,true);
    assert(!f.left&&f.wheelY==120&&f.wheelX==-120);
    f=mouse.update(pad,1228000,true);
    assert(f.wheelY==120);
    mouse.wheelSent(1228000);
    f=mouse.update(pad,1300000,true);
    assert(!f.wheelX&&!f.wheelY);
    pad.buttons=PS5_PAD_BUTTON_DOWN|PS5_PAD_BUTTON_RIGHT;
    f=mouse.update(pad,1378000,true);
    assert(f.wheelY==-120&&f.wheelX==120);
    pad.rightStick={255,255};
    f=mouse.update(pad,1394000,false);
    assert(!f.dx&&!f.dy&&!f.wheelY&&mouse.last==0&&mouse.x==0);
    PS5_PadData gone{};
    f=mouse.update(gone,1410000,true);
    assert(!f.left&&!f.dx);
}

int main() {
    gamepadFixtures();
    gamepadMapping();
    mouseSensitivity();
    rumbleFixtures();
    malformedRumble();
    rumbleScaling();
    launcherScenarios();
    wireFixtures();
    wheelQueueScenarios();
    touchScenarios();
    queueScenarios();
    keyboardScenarios();
}
