/* Master Control 0.4.4: the Controller tools pages, built into src/web_tools.c by tools/build_page.py. GPL-3.0-or-later. */
#ifndef MC_WEB_TOOLS_H
#define MC_WEB_TOOLS_H

typedef struct {
    const char *url;               /* "/tools/info/" or "/tools/info/app.js" */
    const char *mime;
    const unsigned char *data;
    unsigned int len;
    int redirect;                  /* 1: "/tools/info" -> "/tools/info/" */
} web_tool_file;

extern const web_tool_file web_tools_files[];
extern const unsigned int web_tools_count;

#endif
