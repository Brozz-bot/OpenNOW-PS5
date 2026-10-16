// SPDX-License-Identifier: GPL-3.0-or-later
#include "cloud.hpp"
#include "vendor/cJSON.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

using namespace opennow;
static Response response(const char* text,long status=200) {return {status,const_cast<char*>(text),std::strlen(text),nullptr};}
struct Mock {
    unsigned queries=0,posts=0,deletes=0,calls=0;
    std::string lookup,launch;
    const char* catalog=nullptr;
    const char* created=nullptr;
    bool lookupError=false;
};
static constexpr char catalog[]=R"({"data":{"apps":{"items":[{"id":"app-uuid","title":"Fixture game","gfn":{"playabilityState":"PLAYABLE"},"variants":[{"id":"41","appStore":"STEAM","gfn":{"status":"AVAILABLE","library":{"status":"MANUAL"}}},{"id":"42","appStore":"XBOX","gfn":{"status":"AVAILABLE","library":{"status":"PLATFORM_SYNC","playStatus":"PLAYABLE"}}}]}]}}})";
static Response request(void* context,const char* method,const char* url,const char* body,const char*,const char*) {
    auto& m=*static_cast<Mock*>(context);++m.calls;
    if(std::strstr(url,"serviceUrls"))return response(R"({"gfnServiceInfo":{"gfnServiceEndpoints":[{"idpId":"PDiAhv2kJTFeQ7WOPqiQ2tRZ7lGhR2X11dXvM4TZSxg","streamingServiceUrl":"https://test.geforcenow.com/"}]}})");
    if(std::strstr(url,"serverInfo"))return response(R"({"requestStatus":{"serverId":"GFN-PC"}})");
    if(std::strstr(url,"graphql")) {
        ++m.queries;m.lookup=body;
        auto r=response(m.catalog?m.catalog:catalog);if(m.lookupError)r.error="Fixture lookup network failure";return r;
    }
    if(std::strstr(url,"nettestsession"))return response(R"({"requestStatus":{"statusCode":1},"netTestSession":{"sessionId":"net-fixture"}})");
    if(!std::strcmp(method,"POST")) {
        ++m.posts;m.launch=body;
        return response(m.created?m.created:R"({"requestStatus":{"statusCode":1},"session":{"sessionId":"new-fixture","status":0,"queuePosition":2}})");
    }
    if(!std::strcmp(method,"DELETE")){++m.deletes;return response("{}");}
    assert(false);return response("{}",500);
}
static void notStarted(Mock& mock,const char* id="42",const char* store="XBOX",const char* message=nullptr) {
    Cloud cloud(request,&mock);cloud.launchId(id,store,"jwt","device",1,{});
    assert(cloud.view().state==CloudState::failed&&cloud.view().launchError);
    assert(!*cloud.session().id&&mock.posts==0&&mock.deletes==0);
    if(message)assert(std::strstr(cloud.view().message,message));
}
int main() {
    {
        Mock mock;Cloud cloud(request,&mock);
        const auto settings=settingsFor(StreamProfile::quality);
        cloud.launchId("42","XBOX","jwt","device",1,settings);
        assert(mock.queries==1&&mock.posts==1&&cloud.view().state==CloudState::queued);
        assert(!cloud.view().count&&!cloud.library().count&&cloud.view().current.owned);
        assert(!std::strcmp(cloud.view().current.id,"42")&&!std::strcmp(cloud.view().current.store,"XBOX"));
        assert(cloud.session().settings==settings);
        auto* query=cJSON_Parse(mock.lookup.c_str());assert(query);
        const auto* vars=cJSON_GetObjectItemCaseSensitive(query,"variables");
        const auto* ids=cJSON_GetObjectItemCaseSensitive(vars,"ids");
        assert(cJSON_IsArray(ids)&&cJSON_GetArraySize(ids)==1&&cJSON_GetArrayItem(ids,0)->valueint==42);
        assert(std::strstr(cJSON_GetObjectItemCaseSensitive(query,"query")->valuestring,"variantIds:$ids"));
        assert(!cJSON_GetObjectItemCaseSensitive(vars,"searchString")&&!cJSON_GetObjectItemCaseSensitive(vars,"cursor"));
        cJSON_Delete(query);
        auto* launch=cJSON_Parse(mock.launch.c_str());assert(launch);
        const auto* data=cJSON_GetObjectItemCaseSensitive(launch,"sessionRequestData");
        assert(cJSON_GetObjectItemCaseSensitive(data,"appId")->valueint==42);
        assert(!std::strcmp(cJSON_GetObjectItemCaseSensitive(data,"cmsId")->valuestring,"42"));cJSON_Delete(launch);
        const auto calls=mock.calls;
        cloud.launchId("41","STEAM","jwt","device",2,settings);
        assert(mock.calls==calls&&!std::strcmp(cloud.session().id,"new-fixture"));
        assert(cloud.stop("jwt","device")&&mock.deletes==1);
    }
    {Mock mock;Cloud cloud(request,&mock);cloud.launchId("42","","jwt","device",1,{});assert(mock.posts==1&&!std::strcmp(cloud.view().current.store,"XBOX"));}
    {Mock mock;notStarted(mock,"42","STEAM","store no longer matches");}
    {Mock mock;notStarted(mock,"99","XBOX","not found");}
    for(const char* id:{"","0","2147483648","-1","42x","999999999999999999999999999999"}) {Mock mock;notStarted(mock,id);assert(!mock.calls);}
    for(const char* bad:{"{}",R"({"errors":[{"message":"fixture"}],"data":{"apps":{"items":[]}}})",
        R"({"data":{"apps":{"items":[{"title":"Fixture","variants":[{"id":"42","appStore":"XBOX","gfn":{"status":"AVAILABLE","library":{"status":"NOT_OWNED"}}}]}]}}})",
        R"({"data":{"apps":{"items":[{"title":"Fixture","gfn":{"playabilityState":"PLAYABLE"},"variants":[{"id":"42","appStore":"XBOX","gfn":{"status":"PATCHING","library":{"status":"MANUAL"}}}]}]}}})",
        R"({"data":{"apps":{"items":[{"title":"Fixture","variants":[{"id":"42","appStore":"XBOX"}]}]}}})"}) {
        Mock mock;mock.catalog=bad;notStarted(mock);
    }
    {Mock mock;mock.lookupError=true;notStarted(mock,"42","XBOX","network failure");}
    for(const char* conflict:{
        R"({"requestStatus":{"statusCode":11},"session":{"sessionId":"existing-fixture","status":2}})",
        R"({"requestStatus":{"statusCode":1,"statusDescription":"session_limit_reached"},"session":{"sessionId":"existing-fixture","status":2}})",
        R"({"requestStatus":{"statusCode":1,"unifiedErrorCode":"0x4af1201e"},"session":{"sessionId":"existing-fixture","status":2}})",
        R"({"requestStatus":{"statusCode":1},"session":{"sessionId":"existing-fixture","status":2,"errorCode":1257316382}})"}) {
        Mock mock;mock.created=conflict;Cloud cloud(request,&mock);cloud.launchId("42","XBOX","jwt","device",1,{});
        assert(mock.posts==1&&cloud.view().launchError&&cloud.view().state==CloudState::failed);
        assert(std::strstr(cloud.view().message,"already active")&&!*cloud.session().id);
        cloud.tick("jwt","device",20);assert(cloud.stop("jwt","device")&&!mock.deletes);
    }
    std::puts("forward launch tests passed");
}
