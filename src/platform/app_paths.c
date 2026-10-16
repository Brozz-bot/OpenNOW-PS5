// SPDX-License-Identifier: GPL-3.0-or-later
// Sandboxed paths stop resolving once filesystem elevation replaces the
// process root, so every app and storage path is derived from these roots.
// Rebasing happens once during single-threaded startup; later reads are shared.
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "../app_storage.h"

enum { root_size = 256, file_size = 320 };
static char app_root[root_size] = OPENNOW_SANDBOX_APP_ROOT;
static char storage_root[root_size] = OPENNOW_SANDBOX_STORAGE_ROOT;

#define APP_FILES(X) X("assets/cacert.pem") X("assets/forwarder-template") X("opennow-forwarder-elevator.elf")
#define STORAGE_FILES(X) X("account.bin") X("settings.bin") X("session.bin") X("artwork") \
    X("network.log") X("media.log") X("video-sps.bin") X("live-video.status")
#define NAME(n) n,
#define APP_PATH(n) OPENNOW_SANDBOX_APP_ROOT "/" n,
#define STORAGE_PATH(n) OPENNOW_SANDBOX_STORAGE_ROOT "/" n,
static const char *const app_names[] = {APP_FILES(NAME)};
static const char *const storage_names[] = {STORAGE_FILES(NAME)};
static char app_files[][file_size] = {APP_FILES(APP_PATH)};
static char storage_files[][file_size] = {STORAGE_FILES(STORAGE_PATH)};
enum {
    app_count = sizeof(app_names) / sizeof(app_names[0]),
    storage_count = sizeof(storage_names) / sizeof(storage_names[0])
};

const char *opennow_app_root(void) { return app_root; }
const char *opennow_storage_root(void) { return storage_root; }

const char *opennow_app_file(const char *name)
{
    for (unsigned i = 0; i < app_count; ++i)
        if (!strcmp(app_names[i], name))
            return app_files[i];
    return "";
}

const char *opennow_storage_file(const char *name)
{
    for (unsigned i = 0; i < storage_count; ++i)
        if (!strcmp(storage_names[i], name))
            return storage_files[i];
    return "";
}

int opennow_rebase_sandbox(const char *sandbox)
{
    char app[root_size], storage[root_size];
    char apps[app_count][file_size], stored[storage_count][file_size];
    if (!sandbox || sandbox[0] != '/')
        return EINVAL;
    int n = snprintf(app, sizeof(app), "%s" OPENNOW_SANDBOX_APP_ROOT, sandbox);
    if (n <= 0 || n >= root_size)
        return ENAMETOOLONG;
    n = snprintf(storage, sizeof(storage), "%s" OPENNOW_SANDBOX_STORAGE_ROOT, sandbox);
    if (n <= 0 || n >= root_size)
        return ENAMETOOLONG;
    for (unsigned i = 0; i < app_count; ++i) {
        n = snprintf(apps[i], file_size, "%s/%s", app, app_names[i]);
        if (n <= 0 || n >= file_size)
            return ENAMETOOLONG;
    }
    for (unsigned i = 0; i < storage_count; ++i) {
        n = snprintf(stored[i], file_size, "%s/%s", storage, storage_names[i]);
        if (n <= 0 || n >= file_size)
            return ENAMETOOLONG;
    }
    memcpy(app_root, app, sizeof(app));
    memcpy(storage_root, storage, sizeof(storage));
    memcpy(app_files, apps, sizeof(apps));
    memcpy(storage_files, stored, sizeof(stored));
    return 0;
}
