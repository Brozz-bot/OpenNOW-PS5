#include <cstddef>
#include "stream/native/gpu_presenter.hpp"
extern "C" {
#include "platform/ps5-pad.h"
}
extern bool previewPad;
extern unsigned previewButtons;
extern unsigned long long previewTimeUs;
extern "C" {
int sceUserServiceGetInitialUser(int* user) {if(user)*user=0;return previewPad?0:-1;}
int scePadOpen(int,int,int,void*) {return previewPad?0:-1;}
int scePadReadState(int,PS5_PadData* data) {
    if(!previewPad||!data)return -1;
    *data=PS5_PadData{};
    data->connected=1;data->buttons=previewButtons;
    data->leftStick={128,128};data->rightStick={128,128};
    return 0;
}
int scePadSetVibration(int,const PS5_PadVibration*) {return 0;}
unsigned long long sceKernelGetProcessTime() {return previewTimeUs;}
}
namespace opennow::gpu {
bool settingsAvailable(const StreamSettings& settings) noexcept {
    return validateSettings(settings)==SettingsError::none&&!(settings.hdr()&&settings.fps>90);
}
StreamSettings bestSettings() noexcept {return settingsFor(StreamProfile::native_hdr90);}
}
