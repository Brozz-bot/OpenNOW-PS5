// SPDX-License-Identifier: GPL-3.0-or-later
#include "forwarder.hpp"
#include "../app_storage.h"
#include "files.hpp"
#include "image.hpp"
#include "launch_args.hpp"
#include "../vendor/cJSON.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <pthread.h>
#include <openssl/sha.h>

namespace opennow::forwarder {
namespace {
pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
struct Guard {
    Guard(){pthread_mutex_lock(&mutex);}
    ~Guard(){pthread_mutex_unlock(&mutex);}
};
using Json=std::unique_ptr<cJSON,decltype(&cJSON_Delete)>;
Json json(cJSON* value) noexcept {return Json(value,cJSON_Delete);}
const char* string(const cJSON* value) noexcept {return cJSON_IsString(value)?value->valuestring:nullptr;}
const char* field(const cJSON* object,const char* name) noexcept {return string(cJSON_GetObjectItemCaseSensitive(object,name));}
bool same(const char* a,const char* b) noexcept {return a&&b&&!std::strcmp(a,b);}
bool bounded(const char* value,std::size_t capacity) noexcept {return std::memchr(value,0,capacity)!=nullptr;}
bool validGame(const Game& game) noexcept {
    if(!bounded(game.id,sizeof(game.id))||!bounded(game.store,sizeof(game.store))||
       !bounded(game.title,sizeof(game.title))||!game.id[0]||!game.title[0])return false;
    std::uint64_t id=0;
    for(const char* p=game.id;*p;++p) {
        if(*p<'0'||*p>'9')return false;
        id=id*10+static_cast<unsigned>(*p-'0');
        if(id>2147483647U)return false;
    }
    if(!id)return false;
    for(const char* p=game.store;*p;++p)if(static_cast<unsigned char>(*p)<32||*p==127)return false;
    return true;
}
bool pathValid(const Paths& paths) noexcept {
    if(!paths.root||!paths.templateRoot||paths.root[0]!='/'||paths.templateRoot[0]!='/'||
       std::strlen(paths.root)>700||std::strlen(paths.templateRoot)>700||
       paths.collisionRootCount>64||(paths.collisionRootCount&&!paths.collisionRoots))return false;
    for(unsigned i=0;i<paths.collisionRootCount;++i)
        if(!paths.collisionRoots[i]||paths.collisionRoots[i][0]!='/'||std::strlen(paths.collisionRoots[i])>700)return false;
    return true;
}
bool saveJson(cJSON* object,char* out,std::size_t size) noexcept {
    return object&&out&&size<2147483647U&&cJSON_PrintPreallocated(object,out,static_cast<int>(size),1);
}
bool set(cJSON* object,const char* name,const char* value) noexcept {
    auto* item=cJSON_CreateString(value);
    if(!item)return false;
    if(cJSON_ReplaceItemInObjectCaseSensitive(object,name,item))return true;
    cJSON_Delete(item);return false;
}
Json readJson(const char* directory,const char* name) noexcept {
    char path[1024],text[16384];
    if(!files::join(path,directory,name)||!files::readText(path,text,sizeof(text)))return json(nullptr);
    if(std::strstr(text,"\\u0000"))return json(nullptr);
    return json(cJSON_ParseWithOpts(text,nullptr,1));
}
bool readEntry(const char* root,const char* titleId,Entry& entry) noexcept {
    if(!validTileId(titleId))return false;
    char directory[1024],sysPath[1024];
    if(!files::join(directory,root,titleId)||!files::join(sysPath,directory,"sce_sys"))return false;
    files::Directory folder(directory),sys(sysPath);
    if(!folder.valid()||!sys.valid())return false;
    auto document=readJson(directory,"forwarder.json");
    if(!document||!same(field(document.get(),"target"),targetTitleId))return false;
    const char* format=field(document.get(),"format");
    if(format&&!same(format,"ps5-forwarder/1"))return false;
    const auto* args=cJSON_GetObjectItemCaseSensitive(document.get(),"args");
    if(!cJSON_IsArray(args))return false;
    const int count=cJSON_GetArraySize(args);
    if(count!=2&&count!=4)return false;
    const char* argv[4]{};
    for(int i=0;i<count;++i)if(!(argv[i]=string(cJSON_GetArrayItem(args,i))))return false;
    const auto launch=parseLaunchArgs(count,argv);
    if(launch.status!=LaunchArgsStatus::valid)return false;
    auto param=readJson(sysPath,"param.json");
    if(!param||!same(field(param.get(),"titleId"),titleId))return false;
    char content[96];std::snprintf(content,sizeof(content),"UP9000-%s_00-PS5FORWARDER0000",titleId);
    if(!same(field(param.get(),"contentId"),content))return false;
    const auto* localized=cJSON_GetObjectItemCaseSensitive(param.get(),"localizedParameters");
    const char* language=field(localized,"defaultLanguage");
    const char* title=language?field(cJSON_GetObjectItemCaseSensitive(localized,language),"titleName"):nullptr;
    if(!title||std::strlen(title)>=sizeof(entry.game.title))return false;
    entry={};
    std::snprintf(entry.titleId,sizeof(entry.titleId),"%s",titleId);
    std::snprintf(entry.game.id,sizeof(entry.game.id),"%s",launch.appId);
    std::snprintf(entry.game.store,sizeof(entry.game.store),"%s",launch.store);
    std::snprintf(entry.game.title,sizeof(entry.game.title),"%s",title);
    return validGame(entry.game);
}
int indexOf(const char* titleId) noexcept {
    if(!titleId||std::strlen(titleId)!=9||std::strncmp(titleId,"PPSA",4))return -1;
    unsigned number=0;
    for(unsigned i=4;i<9;++i){if(titleId[i]<'0'||titleId[i]>'9')return -1;number=number*10+unsigned(titleId[i]-'0');}
    return number>=99200&&number<=99899?static_cast<int>(number-99200):-1;
}
bool inventory(const char* root,bool (&occupied)[700],const Game* game,Entry& existing) noexcept {
    files::Directory directory(root);
    if(!directory.valid())return directory.error()==ENOENT||directory.error()==ENOTDIR;
    while(const char* name=directory.next()) {
        const int index=indexOf(name);
        if(index>=0)occupied[index]=true;
        char child[1024],sysPath[1024];
        if(!files::join(child,root,name)||!files::join(sysPath,child,"sce_sys"))return false;
        files::Directory folder(child);
        if(!folder.valid())continue;
        files::Directory sys(sysPath);
        if(sys.valid()) {
            auto param=readJson(sysPath,"param.json");
            char paramPath[1024];
            if(!files::join(paramPath,sysPath,"param.json"))return false;
            if(!param&&files::exists(paramPath)){errno=EINVAL;return false;}
            if(param) {
                const int embedded=indexOf(field(param.get(),"titleId"));
                if(embedded>=0)occupied[embedded]=true;
            }
        }
        if(game&&existing.titleId[0]==0) {
            Entry candidate;
            if(readEntry(root,name,candidate)&&same(candidate.game.id,game->id)&&
               (!candidate.game.store[0]||!game->store[0]||same(candidate.game.store,game->store)))existing=candidate;
        }
    }
    return directory.error()==0;
}
Result result(Code code,const char* titleId=nullptr,int error=0) noexcept {
    Result value;value.code=code;value.error=error;
    if(titleId)std::snprintf(value.titleId,sizeof(value.titleId),"%s",titleId);
    return value;
}
bool cancelled(const Job& job) noexcept {return job.cancel&&job.cancel->load();}
void progress(const Job& job,Phase phase,unsigned n) noexcept {
    if(job.progress)job.progress(job.context,{phase,n,6});
}
bool fetch(const Game& game,art::Kind kind,unsigned char* compressed,std::uint32_t* pixels,
           unsigned width,unsigned height,const Options& options,const std::atomic_bool& stop) noexcept {
    char url[224];
    const char* artwork=kind==art::Kind::tile?game.art:(game.hero[0]?game.hero:game.art);
    std::size_t size=0;
    return options.fetch&&art::requestUrl(artwork,kind,url,sizeof(url))&&
        options.fetch(options.fetchContext,url,compressed,art::maxCompressedBytes,size,stop)&&
        art::decode(compressed,size,width,height,pixels);
}
bool writeImage(const char* stage,const char* name,const std::uint32_t* pixels,bool icon,const Job& job) noexcept {
    char path[1024];
    if(!files::join(path,stage,name))return false;
    int fd=accountFile::open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0644);
    if(fd<0)return false;
    const bool ok=icon?encodeIconPng(pixels,512,512,files::write,&fd):
        encodeBackgroundDds(pixels,art::heroWidth,art::heroHeight,files::write,&fd,job.cancel);
    return files::finish(fd,ok);
}
bool writeStage(const char* stage,const Game& game,const Options& options,const Job& job) noexcept {
    char path[1024],from[1024],sys[1024],module[1024];
    if(!files::join(sys,stage,"sce_sys")||!files::join(module,stage,"sce_module")||
       !files::mkdir(sys)||!files::mkdir(module))return false;
    const char* program[]={"eboot.bin","sce_module/libc.prx"};
    for(const char* name:program) {
        if(!files::join(path,stage,name)||!files::join(from,options.paths.templateRoot,name)||
           !files::copy(from,path,2*1024*1024))return false;
    }
    auto compressed=std::unique_ptr<unsigned char,decltype(&std::free)>(static_cast<unsigned char*>(std::malloc(art::maxCompressedBytes)),std::free);
    auto pixels=std::unique_ptr<std::uint32_t,decltype(&std::free)>(static_cast<std::uint32_t*>(std::malloc(art::heroWidth*art::heroHeight*4)),std::free);
    if(!compressed||!pixels){errno=ENOMEM;return false;}
    const std::atomic_bool neverStop{false};
    const auto& stop=job.cancel?*job.cancel:neverStop;
    progress(job,Phase::downloadingIcon,1);
    if(!fetch(game,art::Kind::tile,compressed.get(),pixels.get(),512,512,options,stop)||cancelled(job)||
       !writeImage(stage,"sce_sys/icon0.png",pixels.get(),true,job))return false;
    progress(job,Phase::downloadingBackground,2);
    if(!fetch(game,art::Kind::hero,compressed.get(),pixels.get(),art::heroWidth,art::heroHeight,options,stop)||cancelled(job))return false;
    progress(job,Phase::writing,3);
    if(!writeImage(stage,"sce_sys/pic0.dds",pixels.get(),false,job)||cancelled(job)||
       !files::join(from,stage,"sce_sys/pic0.dds")||!files::join(path,stage,"sce_sys/pic1.dds")||
       !files::copy(from,path,3840*2160+148))return false;
    return files::syncDirectory(sys)&&files::syncDirectory(module);
}
bool verifyProgram(const char* root,const char* name,std::size_t size,const char* expected) noexcept {
    char path[1024];
    if(!files::join(path,root,name))return false;
    const int fd=accountFile::open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0)return false;
    auto bytes=std::unique_ptr<unsigned char,decltype(&std::free)>(static_cast<unsigned char*>(std::malloc(size+1)),std::free);
    if(!bytes){accountFile::close(fd);errno=ENOMEM;return false;}
    std::size_t used=0;
    bool ok=true;
    while(used<=size) {
        const auto n=accountFile::read(fd,bytes.get()+used,size+1-used);
        if(n<0&&errno==EINTR)continue;
        if(n<0){ok=false;break;}
        if(!n)break;
        used+=static_cast<std::size_t>(n);
    }
    if(accountFile::close(fd)!=0)ok=false;
    if(!ok||used!=size)return false;
    unsigned char digest[32];char hex[65];
    if(!SHA256(bytes.get(),size,digest))return false;
    for(unsigned i=0;i<32;++i)std::snprintf(hex+i*2,3,"%02x",digest[i]);
    return same(hex,expected);
}
}

bool validTileId(const char* id) noexcept {
    if(!id||std::strlen(id)!=9||(std::strncmp(id,"PPSA",4)&&
       std::strncmp(id,"CUSA",4)&&std::strncmp(id,"LAPY",4)))return false;
    for(unsigned i=4;i<9;++i)if(id[i]<'0'||id[i]>'9')return false;
    const char* reserved[]={targetTitleId,"PPSA99100","PPSA99008","PPSA99764","PPSA99203","PPSA50011","PPSA00000"};
    for(const char* value:reserved)if(same(id,value))return false;
    return true;
}

bool forwarderJson(const Game& game,char* out,std::size_t size) noexcept {
    if(!validGame(game))return false;
    auto root=json(cJSON_CreateObject());
    if(!root||!cJSON_AddStringToObject(root.get(),"format","ps5-forwarder/1")||
       !cJSON_AddStringToObject(root.get(),"target",targetTitleId)||
       !cJSON_AddStringToObject(root.get(),"creator","OpenNOW PS5"))return false;
    auto* args=cJSON_AddArrayToObject(root.get(),"args");
    if(!args)return false;
    const char* values[]={"--gfn-app-id",game.id,"--store",game.store};
    for(unsigned i=0;i<(game.store[0]?4U:2U);++i) {
        auto* value=cJSON_CreateString(values[i]);
        if(!value)return false;
        if(!cJSON_AddItemToArray(args,value)){cJSON_Delete(value);return false;}
    }
    return saveJson(root.get(),out,size);
}

bool paramJson(const Game& game,const char* titleId,char* out,std::size_t size) noexcept {
    if(!validGame(game)||!validTileId(titleId))return false;
    auto root=json(cJSON_Parse(R"({"ageLevel":{"default":0},"applicationCategoryType":0,"applicationDrmType":"free",
"attribute":0,"attribute2":0,"attribute3":0,"conceptId":"","contentBadgeType":1,"contentId":"",
"contentVersion":"01.000.000","downloadDataSize":256,"gameIntent":{"permittedIntents":[{"intentType":"launchActivity"}]},
"localizedParameters":{"defaultLanguage":"en-US","en-US":{"titleName":""}},"masterVersion":"01.00",
"pubtools":{"creationDate":"2026-08-22 00:00:00","loudnessSnd0":"-28.00","toolVersion":"2.00"},
"requiredSystemSoftwareVersion":"0x0000000000000000","sdkVersion":"0x0000000000000000","titleId":"","versionFileUri":""})"));
    if(!root)return false;
    char content[96];std::snprintf(content,sizeof(content),"UP9000-%s_00-PS5FORWARDER0000",titleId);
    auto* localized=cJSON_GetObjectItemCaseSensitive(root.get(),"localizedParameters");
    auto* english=cJSON_GetObjectItemCaseSensitive(localized,"en-US");
    return set(root.get(),"titleId",titleId)&&set(root.get(),"conceptId",titleId+4)&&
        set(root.get(),"contentId",content)&&set(english,"titleName",game.title)&&saveJson(root.get(),out,size);
}

Result create(const Game& game,const Options& requested,const Job& job) noexcept {
    Options options=requested;
    if(!options.paths.templateRoot)options.paths.templateRoot=opennow_app_file("assets/forwarder-template");
    Guard guard;
    if(!validGame(game)||!bounded(game.art,sizeof(game.art))||!bounded(game.hero,sizeof(game.hero))||!pathValid(options.paths))return result(Code::invalidGame);
    if(cancelled(job))return result(Code::cancelled);
    progress(job,Phase::checking,0);
    if(!files::mkdir(options.paths.root))return result(Code::unavailable,nullptr,errno);
    bool occupied[700]{};
    Entry existing;
    if(!inventory(options.paths.root,occupied,&game,existing))return result(Code::ioError,nullptr,errno);
    if(existing.titleId[0])return result(Code::alreadyExists,existing.titleId);
    if(!verifyProgram(options.paths.templateRoot,"eboot.bin",143974,"3944ed19d258d348be0dda445959740919be4f727d6e6766e783b8a01c298331")||
       !verifyProgram(options.paths.templateRoot,"sce_module/libc.prx",1284674,"e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036"))return result(Code::templateError);
    const char* defaults[]={"/user/appmeta","/system_data/priv/appmeta","/system_ex/app","/user/app","/data/etaHEN/games"};
    if(!options.paths.collisionRoots) {
        for(const char* path:defaults)if(!inventory(path,occupied,nullptr,existing))return result(Code::ioError,nullptr,errno);
        for(unsigned i=0;i<10;++i)for(unsigned j=0;j<3;++j) {
            char path[64];std::snprintf(path,sizeof(path),"/mnt/%s%u%s",i<8?"usb":"ext",i<8?i:i-8,j==0?"":(j==1?"/homebrew":"/etaHEN/games"));
            if(!inventory(path,occupied,nullptr,existing))return result(Code::ioError,nullptr,errno);
        }
    } else for(unsigned i=0;i<options.paths.collisionRootCount;++i)
        if(!inventory(options.paths.collisionRoots[i],occupied,nullptr,existing))return result(Code::ioError,nullptr,errno);
    std::uint32_t hash=2166136261U;
    for(const char* value:{game.id,game.store}) {
        for(const char* p=value;*p;++p){hash^=static_cast<unsigned char>(*p);hash*=16777619U;}
        hash^=0;hash*=16777619U;
    }
    char titleId[10]{},final[1024];
    bool reserved=false;
    for(unsigned i=0;i<700;++i) {
        const unsigned index=(hash%700+i)%700;
        std::snprintf(titleId,sizeof(titleId),"PPSA%05u",99200+index);
        if(occupied[index]||!validTileId(titleId))continue;
        if(!files::join(final,options.paths.root,titleId))return result(Code::ioError);
        if(accountFile::makeDirectory(final,0755)==0){reserved=true;break;}
        if(errno!=EEXIST)return result(Code::ioError,nullptr,errno);
    }
    if(!reserved)return result(Code::noFreeTitleId);
    char stage[1024],name[80];
    bool staged=false;
    for(unsigned i=0;i<32;++i) {
        std::snprintf(name,sizeof(name),".opennow-stage-%s-%u-%u",titleId,static_cast<unsigned>(getpid()),i);
        if(!files::join(stage,options.paths.root,name))break;
        if(accountFile::makeDirectory(stage,0755)==0){staged=true;break;}
        if(errno!=EEXIST)break;
    }
    Code code=Code::ioError;
    bool ok=false;
    if(staged) {
        char body[4096],path[1024];
        ok=forwarderJson(game,body,sizeof(body))&&files::join(path,stage,"forwarder.json")&&files::writeFile(path,body,std::strlen(body));
        if(ok) {
            ok=writeStage(stage,game,options,job);
            if(!ok)code=Code::artworkError;
        }
        if(ok)ok=paramJson(game,titleId,body,sizeof(body))&&files::join(path,stage,"sce_sys/param.json")&&
            files::writeFile(path,body,std::strlen(body))&&accountFile::syncParent(path)&&files::syncDirectory(stage);
        if(ok&&!cancelled(job)) {
            progress(job,Phase::publishing,5);
            ok=!cancelled(job)&&accountFile::rename(stage,final)==0&&files::syncDirectory(options.paths.root);
            if(ok)code=Code::created;
        }
    }
    const int error=errno;
    if(staged&&code!=Code::created)files::erase(stage);
    if(code!=Code::created) {
#ifdef OPENNOW_PS5
        (void)sceKernelRmdir(final);
#else
        (void)::rmdir(final);
#endif
    }
    if(cancelled(job)&&code!=Code::created)code=Code::cancelled;
    if(code==Code::created)progress(job,Phase::complete,6);
    return result(code,titleId,code==Code::created?0:error);
}

Result list(Visit visit,void* context,const Paths& paths) noexcept {
    Guard guard;
    if(!pathValid(paths))return result(Code::invalidGame);
    files::Directory directory(paths.root);
    if(!directory.valid())return result(Code::unavailable,nullptr,directory.error());
    auto value=result(Code::listed);
    while(const char* name=directory.next()) {
        Entry entry;
        if(!readEntry(paths.root,name,entry))continue;
        ++value.count;
        if(visit&&!visit(context,entry))break;
    }
    if(directory.error())return result(Code::ioError,nullptr,directory.error());
    return value;
}

Result remove(const char* titleId,const Paths& paths) noexcept {
    Guard guard;
    if(!pathValid(paths)||!validTileId(titleId))return result(Code::invalidGame);
    char directory[1024],tomb[1024],name[80],owner[1024],marker[128],stored[128];
    if(!files::join(directory,paths.root,titleId))return result(Code::invalidGame);
    std::snprintf(name,sizeof(name),".opennow-remove-%s",titleId);
    if(!files::join(tomb,paths.root,name))return result(Code::invalidGame);
    std::snprintf(name,sizeof(name),".opennow-remove-%s.owner",titleId);
    if(!files::join(owner,paths.root,name))return result(Code::invalidGame);
    std::snprintf(marker,sizeof(marker),"OpenNOW PS5 removal %s\n",titleId);
    if(!files::exists(directory)) {
        if(!files::exists(tomb)&&!files::exists(owner))return result(Code::notFound,titleId);
        if(!files::readText(owner,stored,sizeof(stored))||!same(marker,stored))return result(Code::conflict,titleId);
        if(!files::erase(tomb)||accountFile::remove(owner)!=0||!files::syncDirectory(paths.root))return result(Code::ioError,titleId,errno);
        return result(Code::removed,titleId);
    }
    Entry entry;
    if(!readEntry(paths.root,titleId,entry))return result(Code::conflict,titleId);
    if(files::exists(tomb))return result(Code::conflict,titleId);
    if(files::exists(owner)) {
        if(!files::readText(owner,stored,sizeof(stored))||!same(marker,stored))return result(Code::conflict,titleId);
    } else if(!files::writeFile(owner,marker,std::strlen(marker)))return result(Code::ioError,titleId,errno);
    if(!files::syncDirectory(paths.root)||accountFile::rename(directory,tomb)!=0||!files::syncDirectory(paths.root)||!files::erase(tomb)||
       accountFile::remove(owner)!=0||!files::syncDirectory(paths.root))return result(Code::ioError,titleId,errno);
    return result(Code::removed,titleId);
}

const char* message(Code code) noexcept {
    switch(code) {
    case Code::created:return "Tile created. Rescan homebrew in ShadowMountPlus.";
    case Code::alreadyExists:return "This game's home-screen tile already exists.";
    case Code::removed:return "Tile folder removed. Refresh ShadowMountPlus to remove its registration.";
    case Code::notFound:return "The tile folder is already absent.";
    case Code::listed:return "Home-screen tiles loaded.";
    case Code::available:return "Home-screen forwarding is available.";
    case Code::unavailable:return "Home-screen storage unavailable. Check elfldr and filesystem elevation.";
    case Code::invalidGame:return "The game, title ID or forwarder path is invalid.";
    case Code::noFreeTitleId:return "No unused forwarder title ID is available.";
    case Code::conflict:return "The folder is not a recognized OpenNOW tile; nothing was removed.";
    case Code::cancelled:return "Tile creation cancelled.";
    case Code::artworkError:return "Could not download or write the game's artwork. Check connection and free space.";
    case Code::templateError:return "The shared forwarder template is missing or invalid.";
    case Code::ioError:return "Could not finish the tile operation. Check storage and free space.";
    case Code::unsafeElevation:return "Filesystem elevation was not confirmed. Close OpenNOW and check elfldr.";
    case Code::sandboxLost:return "OpenNOW could not find its own files after filesystem elevation. Close OpenNOW";
    }
    return "Forwarder operation failed.";
}
}
