/* One-time PS5 media-row shortcut installer. Run separately from PadBridge so
 * a firmware-specific installer failure cannot interrupt controller input. */
#include "launcher.h"
#include "hubcard.h"

#include <ps5/kernel.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>

extern const unsigned char icon_png_data[];
extern const unsigned int icon_png_data_len;

static void *load(const char *name)
{
    void *h;
    printf("icon install: loading %s\n", name); fflush(stdout);
    h = dlopen(name, RTLD_NOW | RTLD_GLOBAL);
    if (!h) printf("icon install: %s: %s\n", name, dlerror());
    fflush(stdout);
    return h;
}

int main(void)
{
    void *lib;
    int (*init)(void), (*term)(void), (*install_all)(void *);
    int (*install_dir)(const char *, const char *, void *) = NULL;
    uint32_t handle = 0;
    int r, port, source;
    const char *why = "";

    printf("icon install: starting\n"); fflush(stdout);
    /* This load order succeeded in the firmware 13.60 library probe. */
    if (!load("libScePad.sprx") || !load("libSceUserService.sprx")) return 1;
    if (!(lib = load("libSceAppInstUtil.sprx"))) return 1;
    init = (void *)dlsym(lib, "sceAppInstUtilInitialize");
    term = (void *)dlsym(lib, "sceAppInstUtilTerminate");
    install_all = (void *)dlsym(lib, "sceAppInstUtilAppInstallAll");
    if (kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &handle) == 0)
        install_dir = (void *)kernel_dynlib_resolve(-1, handle, "Wudg3Xe3heE");
    printf("icon install: init=%d term=%d title_dir=%d install_all=%d\n",
           !!init, !!term, !!install_dir, !!install_all); fflush(stdout);
    if (!init || !term || (!install_dir && !install_all)) return 1;
    r = init();
    printf("icon install: initialize -> %#x\n", (unsigned)r); fflush(stdout);
    if (r != 0) return 1;
    /* The tile opens the menu where Master Control listens: the port on its
     * Bloom Core card, else the suite default (never the old 8095). */
    port = hubcard_resolve_port("/data/mastercontrol/port", HUB_CARD_PATH, &source, &why);
    printf("icon install: menu port %d (%s%s%s)\n", port,
           source == HUB_PORT_CARD ? "from the card" : source == HUB_PORT_OVERRIDE ? "from /data/mastercontrol/port" : "default",
           source == HUB_PORT_DEFAULT ? ": " : "", source == HUB_PORT_DEFAULT ? why : ""); fflush(stdout);
    r = launcher_write(LAUNCHER_APP_ROOT, icon_png_data, icon_png_data_len, port);
    printf("icon install: wrote shortcut files -> %d\n", r); fflush(stdout);
    if (r < 0) { term(); return 1; }
    r = install_dir ? install_dir(LAUNCHER_TITLE_ID, LAUNCHER_APP_ROOT "/", NULL)
                    : install_all(NULL);
    printf("icon install: register -> %#x\n", (unsigned)r); fflush(stdout);
    term();
    if (r != 0) return 1;
    {
        FILE *marker = fopen("/data/mastercontrol/icon_installed", "wb");
        if (marker) fclose(marker);
    }
    printf("icon install: success\n"); fflush(stdout);
    return 0;
}
