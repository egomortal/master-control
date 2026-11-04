/* System libraries loaded at run time instead of at start-up.
 *
 * On firmware 13.60 with the Relapse loader, a payload linked against
 * libScePad / libSceUserService / libSceSystemService / libSceAppInstUtil
 * died before main(): the only trace was a SceLncUtil getAppStatus error
 * (0x80940004) printed while those libraries were being loaded. Here the
 * payload links none of them; each function below loads its library on
 * first use (after main, after the credentials are raised), logs what
 * happened, and fails softly if the library cannot be loaded, so one
 * unavailable library costs one feature instead of the whole payload. */
#include "log.h"

#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>

#define LIB_FAIL ((int32_t)0x80020012)

enum { L_PAD, L_USER, L_SYS, L_APPINST, L_N };
static const char *const k_lib[L_N] = {
    "libScePad.sprx", "libSceUserService.sprx", "libSceSystemService.sprx", "libSceAppInstUtil.sprx",
};
static void *g_h[L_N];
static int g_tried[L_N];

static void *sym(int lib, const char *name)
{
    void *f;

    if (!g_h[lib] && !g_tried[lib]) {
        g_tried[lib] = 1;
        log_line("libs: loading %s", k_lib[lib]);
        g_h[lib] = dlopen(k_lib[lib], RTLD_NOW | RTLD_GLOBAL);
        if (g_h[lib]) log_line("libs: %s loaded", k_lib[lib]);
        else log_line("libs: %s could not be loaded: %s", k_lib[lib], dlerror());
    }
    if (!g_h[lib]) return NULL;
    f = dlsym(g_h[lib], name);
    if (!f) log_line("libs: %s has no %s", k_lib[lib], name);
    return f;
}

#define FWD(lib, ret, name, params, args)                                   \
    ret name params                                                         \
    {                                                                       \
        static ret (*p) params;                                             \
        static int looked;                                                  \
        if (!looked) { p = (ret (*) params)sym(lib, #name); looked = 1; }   \
        return p ? p args : (ret)LIB_FAIL;                                  \
    }

FWD(L_PAD, int32_t, scePadInit, (void), ())
FWD(L_PAD, int32_t, scePadSetProcessPrivilege, (int32_t a), (a))
FWD(L_PAD, int32_t, scePadGetHandle, (int32_t a, int32_t b, int32_t c), (a, b, c))
FWD(L_PAD, int32_t, scePadOpen, (int32_t a, int32_t b, int32_t c, const void *d), (a, b, c, d))
FWD(L_PAD, int32_t, scePadClose, (int32_t a), (a))
FWD(L_PAD, int32_t, scePadReadState, (int32_t a, void *b), (a, b))
FWD(L_PAD, int32_t, scePadVirtualDeviceAddDevice, (void *a, int32_t b), (a, b))
FWD(L_PAD, int32_t, scePadVirtualDeviceInsertData, (int32_t a, const void *b), (a, b))
FWD(L_PAD, int32_t, scePadVirtualDeviceDeleteDevice, (int32_t a), (a))

FWD(L_USER, int32_t, sceUserServiceInitialize, (void *a), (a))
FWD(L_USER, int32_t, sceUserServiceGetInitialUser, (int32_t *a), (a))
FWD(L_USER, int32_t, sceUserServiceGetForegroundUser, (int32_t *a), (a))
FWD(L_USER, int32_t, sceUserServiceGetLoginUserIdList, (int32_t *a), (a))

FWD(L_SYS, int, sceSystemServiceLaunchWebBrowser, (const char *a, void *b), (a, b))

FWD(L_APPINST, int, sceAppInstUtilInitialize, (void), ())
FWD(L_APPINST, int, sceAppInstUtilTerminate, (void), ())
FWD(L_APPINST, int, sceAppInstUtilAppInstallTitleDir, (const char *a, const char *b, void *c), (a, b, c))
FWD(L_APPINST, int, sceAppInstUtilAppInstallAll, (void *a), (a))
FWD(L_APPINST, int, sceAppInstUtilAppUnInstall, (const char *a), (a))
