#!/usr/bin/env python3
"""Builds web/index.html (the Master Control menu) from web/index.src.html by
embedding the bundled OFL fonts in web/fonts/ as base64 @font-face rules and the
Reactor artwork (web/art/reactor-1024.webp, made from Astra's master-reactor-art.png)
as a data URI, so the console page needs nothing from the internet. Also writes the C
byte arrays the payload serves: src/web_page.c (the menu), src/web_classic.c (the
previous EeveeLution menu, from web/classic.html) and src/web_overlay.c (the quick
overlay at /overlay, from web/overlay.src.html as is), in xxd -i's layout."""
import base64, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTS = [("Inter", 400, "inter-latin-400-normal.woff2"), ("Inter", 600, "inter-latin-600-normal.woff2"),
         ("Rajdhani", 600, "rajdhani-latin-600-normal.woff2"), ("Rajdhani", 700, "rajdhani-latin-700-normal.woff2")]
ART = ("@@REACTOR_ART@@", "image/webp", os.path.join("web", "art", "reactor-1024.webp"))


def b64(path):
    return base64.b64encode(open(os.path.join(ROOT, path), "rb").read()).decode()


def faces():
    out = []
    for fam, w, f in FONTS:
        out.append("@font-face{font-family:%s;font-style:normal;font-weight:%d;font-display:swap;"
                   "src:url(data:font/woff2;base64,%s) format('woff2')}" % (fam, w, b64(os.path.join("web", "fonts", f))))
    return "\n".join(out)


def c_array(data, name, src):
    lines = ["/* Generated from %s by tools/build_page.py. */" % src, "const unsigned char %s[] = {" % name]
    for i in range(0, len(data), 12):
        lines.append("  " + ", ".join("0x%02x" % b for b in data[i:i + 12]) + ("," if i + 12 < len(data) else ""))
    lines.append("};")
    lines.append("const unsigned int %s_len = sizeof %s;" % (name, name))
    return "\n".join(lines) + "\n"


def main():
    src = open(os.path.join(ROOT, "web", "index.src.html"), encoding="utf-8").read()
    if "/*@@FONTS@@*/" not in src:
        sys.exit("index.src.html has no /*@@FONTS@@*/ marker")
    if src.count(ART[0]) != 1:
        sys.exit("index.src.html must reference %s exactly once" % ART[0])
    page = src.replace("/*@@FONTS@@*/", faces(), 1).replace(ART[0], "data:%s;base64,%s" % (ART[1], b64(ART[2])), 1).encode("utf-8")
    open(os.path.join(ROOT, "web", "index.html"), "wb").write(page)
    open(os.path.join(ROOT, "src", "web_page.c"), "w").write(c_array(page, "web_page_html", "web/index.html"))
    classic = open(os.path.join(ROOT, "web", "classic.html"), "rb").read()
    open(os.path.join(ROOT, "src", "web_classic.c"), "w").write(c_array(classic, "web_classic_html", "web/classic.html"))
    overlay = open(os.path.join(ROOT, "web", "overlay.src.html"), "rb").read()
    open(os.path.join(ROOT, "src", "web_overlay.c"), "w").write(c_array(overlay, "web_overlay_html", "web/overlay.src.html"))
    print("web/index.html %d bytes, classic %d bytes, overlay %d bytes" % (len(page), len(classic), len(overlay)))
    build_tools()


MIME = {".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8", ".css": "text/css; charset=utf-8",
        ".woff2": "font/woff2", ".txt": "text/plain; charset=utf-8", ".json": "application/json", ".svg": "image/svg+xml",
        ".png": "image/png", ".webp": "image/webp"}


def build_tools():
    """src/web_tools.c: every file under web/tools/ (the 0.4.4 Controller tools pages), served at /tools/<path>.
    Identical files (the fonts each page bundles) are stored once. A folder is served by its index.html at
    "/tools/<dir>/"; "/tools/<dir>" without the slash is listed as a redirect so relative links keep working."""
    base = os.path.join(ROOT, "web", "tools")
    files, dirs = [], set()
    for d, _, names in os.walk(base):
        for n in sorted(names):
            full = os.path.join(d, n)
            rel = os.path.relpath(full, base).replace(os.sep, "/")
            ext = os.path.splitext(n)[1].lower()
            if ext not in MIME:
                sys.exit("web/tools: no MIME type for " + rel)
            files.append((rel, MIME[ext], open(full, "rb").read()))
        reld = os.path.relpath(d, base).replace(os.sep, "/")
        if reld != "." and os.path.isfile(os.path.join(d, "index.html")):
            dirs.add(reld)
    files.sort()
    blobs, index, out = [], {}, ["/* Generated from web/tools/ by tools/build_page.py. */", "#include \"web_tools.h\"", ""]
    for rel, mime, data in files:
        if data not in index:
            index[data] = len(blobs)
            blobs.append(data)
    for i, data in enumerate(blobs):
        out.append("static const unsigned char tools_blob_%d[] = {" % i)
        for k in range(0, len(data), 16):
            out.append("  " + ", ".join("0x%02x" % b for b in data[k:k + 16]) + ("," if k + 16 < len(data) else ""))
        out.append("};")
    out.append("")
    out.append("const web_tool_file web_tools_files[] = {")
    for rel, mime, data in files:
        i = index[data]
        url = "/tools/" + (rel[:-len("index.html")] if rel.endswith("index.html") else rel)
        out.append('  { "%s", "%s", tools_blob_%d, sizeof tools_blob_%d, 0 },' % (url, mime, i, i))
    for d in sorted(dirs):
        out.append('  { "/tools/%s", "", 0, 0, 1 },' % d)
    out.append('  { "/tools", "", 0, 0, 1 },')
    out.append("};")
    out.append("const unsigned int web_tools_count = sizeof web_tools_files / sizeof web_tools_files[0];")
    open(os.path.join(ROOT, "src", "web_tools.c"), "w").write("\n".join(out) + "\n")
    total = sum(len(b) for b in blobs)
    print("web/tools: %d files, %d unique, %d bytes" % (len(files), len(blobs), total))


if __name__ == "__main__":
    main()
