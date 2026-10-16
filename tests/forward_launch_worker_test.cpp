// SPDX-License-Identifier: GPL-3.0-or-later
#define main preview_entry
#include "../src/main.cpp"
#undef main
#include "vendor/cJSON.h"
#include <cassert>
#include <filesystem>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {
std::string testRoot;
unsigned scenario=0,refreshes=0,lookups=0,catalogs=0,launches=0,sleeps=0;
std::uint64_t clockUs=1000000;
bool seed=true,verified=false,startedSignIn=false,escaped=false,stopped=false;
const char* mapPath(const char* path,std::string& out) {
    if(!std::strcmp(path,OPENNOW_LEGACY_ACCOUNT_PATH)){out=testRoot+"/legacy-missing";return out.c_str();}
    const std::size_t prefix=std::strlen(OPENNOW_STORAGE_ROOT);
    assert(!std::strncmp(path,OPENNOW_STORAGE_ROOT,prefix));out=testRoot+(path+prefix);return out.c_str();
}
opennow::Response reply(const char* body,long status=200) {return {status,const_cast<char*>(body),std::strlen(body),nullptr};}
const char* challenge=R"({"device_code":"fixture-device","user_code":"TEST","verification_uri":"https://login.nvidia.com/activate","interval":5})";
}
extern "C" {
int sceKernelOpen(const char* path,int flags,unsigned mode) {std::string mapped;return ::open(mapPath(path,mapped),flags,mode);}
std::int64_t sceKernelRead(int fd,void* data,std::size_t size) {return ::read(fd,data,size);}
std::int64_t sceKernelWrite(int fd,const void* data,std::size_t size) {return ::write(fd,data,size);}
int sceKernelClose(int fd) {return ::close(fd);}
int sceKernelFsync(int fd) {return ::fsync(fd);}
int sceKernelFchmod(int fd,unsigned mode) {return ::fchmod(fd,mode);}
int sceKernelRename(const char* from,const char* to) {std::string a,b;return ::rename(mapPath(from,a),mapPath(to,b));}
int sceKernelUnlink(const char* path) {std::string mapped;return ::unlink(mapPath(path,mapped));}
int sceKernelMkdir(const char* path,unsigned mode) {std::string mapped;return ::mkdir(mapPath(path,mapped),mode);}
unsigned long long sceKernelGetProcessTime() {return clockUs;}
int sceKernelUsleep(unsigned) {
    clockUs+=1000000;assert(++sleeps<100);
    if(scenario==2&&!startedSignIn) {
        assert(!refreshes&&!lookups&&!catalogs&&!launches&&published.state==State::idle);
        startedSignIn=true;command.store(1);
    }
    if(scenario==3&&!escaped) {
        assert(published.state==State::requesting&&!lookups&&!catalogs&&!launches);
        escaped=true;submit(18,{});
    }
    if(scenario==4&&publishedCloud.launchError&&!escaped) {
        assert(!catalogs&&!launches);escaped=true;submit(18,{});
    }
    if((scenario<=2&&launches)||(scenario>=3&&catalogs==2))command.store(10);
    return 0;
}
}
namespace ps5::demo {void requestStop() noexcept {stopped=true;}}
namespace opennow::gpu {
bool settingsAvailable(const StreamSettings& settings) noexcept {return validateSettings(settings)==SettingsError::none&&!settings.hardware();}
StreamSettings bestSettings() noexcept {return settingsFor(StreamProfile::compatibility);}
}
namespace opennow {
bool randomBytes(void* out,std::size_t size) noexcept {std::memset(out,1,size);return true;}
Http::Http() noexcept {buffer_=reinterpret_cast<char*>(this);}
Http::~Http()=default;
Response Http::request(void*,const char* method,const char* url,const char* body,const char* bearer,const char*) noexcept {
    if(std::strstr(url,"/device/authorize"))return reply(challenge);
    if(std::strstr(url,"/token")) {
        if(!seed) {
            ++refreshes;
            if((scenario==1||scenario==3)&&refreshes==1)return reply("{}",503);
        }
        return reply(R"({"access_token":"fixture-access","id_token":"fixture-cloud","refresh_token":"fixture-refresh","expires_in":3600})");
    }
    if(std::strstr(url,"/userinfo")){if(!seed)verified=true;return reply(R"({"sub":"fixture-user"})");}
    if(std::strstr(url,"/client_token"))return reply(R"({"client_token":"fixture-client"})");
    assert(verified);
    if(std::strstr(url,"serviceUrls"))return reply(R"({"gfnServiceInfo":{"gfnServiceEndpoints":[{"idpId":"PDiAhv2kJTFeQ7WOPqiQ2tRZ7lGhR2X11dXvM4TZSxg","streamingServiceUrl":"https://test.geforcenow.com/"}]}})");
    assert(bearer&&!std::strcmp(bearer,"fixture-cloud"));
    if(std::strstr(url,"serverInfo"))return reply(R"({"requestStatus":{"serverId":"GFN-PC"}})");
    if(std::strstr(url,"graphql")) {
        if(std::strstr(body,"variantIds")) {
            ++lookups;assert(!catalogs&&forwardLaunchMode.load());
            if(scenario==4)return reply(R"({"data":{"apps":{"items":[]}}})");
            return reply(R"({"data":{"apps":{"items":[{"title":"Fixture","gfn":{"playabilityState":"PLAYABLE"},"variants":[{"id":"42","appStore":"XBOX","gfn":{"status":"AVAILABLE","library":{"status":"MANUAL"}}}]}]}}})");
        }
        ++catalogs;assert(escaped&&!forwardLaunchMode.load());
        return reply(R"({"data":{"apps":{"pageInfo":{"hasNextPage":false,"endCursor":""},"items":[]}}})");
    }
    if(std::strstr(url,"nettestsession"))return reply(R"({"requestStatus":{"statusCode":1},"netTestSession":{"sessionId":"fixture-net"}})");
    if(!std::strcmp(method,"POST")) {
        ++launches;assert(lookups==1&&!catalogs);
        auto* root=cJSON_Parse(body);assert(root);
        const auto* features=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root,"sessionRequestData"),"requestedStreamingFeatures");
        assert(cJSON_GetObjectItemCaseSensitive(features,"maxBitrateKbps")->valueint==20000);cJSON_Delete(root);
        return reply(R"({"requestStatus":{"statusCode":1},"session":{"sessionId":"fixture-new","status":0}})");
    }
    assert(!std::strcmp(method,"DELETE"));return reply("{}");
}
}
static void runScenario(unsigned value) {
    scenario=value;
    char root[]="/tmp/opennow-forward-worker-XXXXXX";assert(mkdtemp(root));testRoot=root;
    auto settings=opennow::settingsFor(StreamProfile::quality);settings.bitrate_kbps=20000;
    assert(!opennow::settingsFile::save(OPENNOW_SETTINGS_PATH,{true,settings,true}));
    opennow::Http http;activeHttp=&http;
    if(scenario!=2) {
        opennow::Login login(opennow::Http::request,&http,OPENNOW_ACCOUNT_PATH);
        login.begin("00000000-0000-4000-8000-000000000000",0);login.tick(5);
        assert(login.view().sessionSaved);
    }
    seed=false;
    const char* args[]={"--gfn-app-id","42","--store","XBOX"};
    launchArgs=opennow::forwarder::parseLaunchArgs(4,args);forwardLaunchMode.store(true);
    worker(nullptr);assert(stopped);
    if(scenario<=2)assert(launches==1&&lookups==1&&!catalogs&&publishedLaunch==settings);
    if(scenario==1)assert(refreshes==2);
    if(scenario==3)assert(!lookups&&!launches&&catalogs==2&&refreshes==2);
    if(scenario==4)assert(lookups==1&&!launches&&catalogs==2);
    assert(std::filesystem::exists(testRoot+"/account.bin"));
    std::filesystem::remove_all(testRoot);
}
int main() {
    for(unsigned scenario=0;scenario<5;++scenario) {
        const pid_t child=fork();assert(child>=0);
        if(!child){runScenario(scenario);std::exit(0);}
        int status=0;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
    }
    std::puts("forward worker auth, refresh, saved settings and normal-launcher escape tests passed");
}
