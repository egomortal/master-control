#include "ps5_apps.h"
#include "launcher.h"
#include "log.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <ps5/kernel.h>

#define INSTALL_MARKER "/data/mastercontrol/icon_installed"

extern const unsigned char icon_png_data[];
extern const unsigned int icon_png_data_len;

int sceAppInstUtilInitialize(void);
int sceAppInstUtilTerminate(void);
int sceAppInstUtilAppInstallTitleDir(const char *title_id, const char *dir, void *reserved);
int sceAppInstUtilAppInstallAll(void *reserved);
int sceAppInstUtilAppUnInstall(const char *title_id);

static int install_title(const char *title_id)
{
    int (*install_dir)(const char *, const char *, void *) = NULL;
    uint32_t handle = 0;

    /* The title-dir entry is exported by NID on newer firmware rather than
     * by its C name. Payload Manager uses this NID and InstallAll fallback. */
    if (kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &handle) == 0)
        install_dir = (void *)kernel_dynlib_resolve(-1, handle, "Wudg3Xe3heE");
    if (install_dir) return install_dir(title_id, LAUNCHER_APP_ROOT "/", NULL);
    log_line("launcher: title-dir install unavailable; trying InstallAll");
    return sceAppInstUtilAppInstallAll(NULL);
}

int apps_install_launcher(int port)
{
    struct stat st;
    int w, r;

    if ((r = sceAppInstUtilInitialize()) != 0) {
        log_line("launcher: sceAppInstUtilInitialize -> %#x", (unsigned)r);
        return -1;
    }
    w = launcher_write(LAUNCHER_APP_ROOT, icon_png_data, icon_png_data_len, port);
    if (w < 0) {
        log_line("launcher: could not write the files under %s", LAUNCHER_APP_ROOT);
        sceAppInstUtilTerminate();
        return -1;
    }
    if (w == 0 && stat(INSTALL_MARKER, &st) == 0) {
        log_line("launcher: already installed and up to date");
        sceAppInstUtilTerminate();
        return 0;
    }
    r = install_title(LAUNCHER_TITLE_ID);
    sceAppInstUtilTerminate();
    log_line("launcher: installed %s in the media row -> %#x", LAUNCHER_TITLE_ID, (unsigned)r);
    if (r == 0) {
        FILE *marker = fopen(INSTALL_MARKER, "wb");
        if (marker) fclose(marker);
    }
    return r == 0 ? 0 : -1;
}

int apps_remove_launcher(void)
{
    int r = sceAppInstUtilInitialize();

    if (r == 0) {
        r = sceAppInstUtilAppUnInstall(LAUNCHER_TITLE_ID);
        sceAppInstUtilTerminate();
    }
    log_line("launcher: removed %s -> %#x", LAUNCHER_TITLE_ID, (unsigned)r);
    launcher_remove_files(LAUNCHER_APP_ROOT);
    remove(INSTALL_MARKER);
    return r == 0 ? 0 : -1;
}
