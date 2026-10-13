/* mini-xdotool — the handful of xdotool commands scripts/ui-test/ui.sh uses,
 * for sandboxes where the xdotool package cannot be installed.
 *
 *   cc -O2 mini-xdotool.c -o xdotool -lX11 -lXtst
 *
 * Supported: search --name TEXT | windowmove W X Y | windowsize W W H |
 * mousemove X Y | mousemove_relative DX DY | click [--repeat N] [--delay MS] B |
 * mousedown B | mouseup B | key CHORD... (e.g. ctrl+s Escape shift+Tab) | type TEXT
 * Commands can be chained like xdotool's (one per invocation is what ui.sh does).
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* XTest prototypes (the header is not always installed; the library is). */
extern int XTestFakeMotionEvent(Display*, int, int, int, unsigned long);
extern int XTestFakeButtonEvent(Display*, unsigned int, Bool, unsigned long);
extern int XTestFakeKeyEvent(Display*, unsigned int, Bool, unsigned long);
extern int XTestFakeRelativeMotionEvent(Display*, int, int, unsigned long);

static Display* d;

static int Search(Window w, const char* needle, int depth) {
    int found = 0;
    /* _NET_WM_NAME (UTF-8, what GLFW sets), then the legacy WM_NAME. */
    static Atom netName = None, utf8 = None;
    if (netName == None) {
        netName = XInternAtom(d, "_NET_WM_NAME", False);
        utf8 = XInternAtom(d, "UTF8_STRING", False);
    }
    Atom type;
    int format;
    unsigned long count, after;
    unsigned char* name = NULL;
    if (XGetWindowProperty(d, w, netName, 0, 1024, False, utf8, &type, &format, &count, &after, &name) != Success || !name) {
        char* legacy = NULL;
        if (XFetchName(d, w, &legacy) && legacy) name = (unsigned char*)legacy;
    }
    if (name) {
        if (strstr((const char*)name, needle)) {
            printf("%lu\n", (unsigned long)w);
            found = 1;
        }
        XFree(name);
    }
    if (depth > 6) return found;
    Window root, parent, *kids = NULL;
    unsigned n = 0;
    if (XQueryTree(d, w, &root, &parent, &kids, &n)) {
        for (unsigned i = 0; i < n; ++i) found |= Search(kids[i], needle, depth + 1);
        if (kids) XFree(kids);
    }
    return found;
}

static KeySym Sym(const char* s) {
    if (!strcasecmp(s, "ctrl")) return XK_Control_L;
    if (!strcasecmp(s, "shift")) return XK_Shift_L;
    if (!strcasecmp(s, "alt")) return XK_Alt_L;
    if (!strcasecmp(s, "super")) return XK_Super_L;
    if (!strcasecmp(s, "Return") || !strcasecmp(s, "enter")) return XK_Return;
    if (!strcasecmp(s, "Escape") || !strcasecmp(s, "esc")) return XK_Escape;
    return XStringToKeysym(s);
}

static void Key(const char* chord) {
    char buf[256];
    snprintf(buf, sizeof buf, "%s", chord);
    KeyCode codes[8];
    int n = 0;
    for (char* part = strtok(buf, "+"); part && n < 8; part = strtok(NULL, "+")) {
        const KeySym ks = Sym(part);
        if (ks == NoSymbol) { fprintf(stderr, "unknown key %s\n", part); continue; }
        codes[n++] = XKeysymToKeycode(d, ks);
    }
    for (int i = 0; i < n; ++i) XTestFakeKeyEvent(d, codes[i], True, CurrentTime);
    for (int i = n - 1; i >= 0; --i) XTestFakeKeyEvent(d, codes[i], False, CurrentTime);
    XFlush(d);
}

int main(int argc, char** argv) {
    d = XOpenDisplay(NULL);
    if (!d) { fprintf(stderr, "no display\n"); return 1; }
    int i = 1;
    while (i < argc) {
        const char* c = argv[i++];
        if (!strcmp(c, "search")) {
            if (i < argc && !strcmp(argv[i], "--name")) ++i;
            if (i >= argc) return 2;
            if (!Search(DefaultRootWindow(d), argv[i++], 0)) return 1;
        } else if (!strcmp(c, "windowmove") && i + 2 < argc) {
            XMoveWindow(d, strtoul(argv[i], 0, 10), atoi(argv[i + 1]), atoi(argv[i + 2]));
            i += 3;
        } else if (!strcmp(c, "windowsize") && i + 2 < argc) {
            XResizeWindow(d, strtoul(argv[i], 0, 10), atoi(argv[i + 1]), atoi(argv[i + 2]));
            i += 3;
        } else if (!strcmp(c, "mousemove") && i + 1 < argc) {
            XTestFakeMotionEvent(d, -1, atoi(argv[i]), atoi(argv[i + 1]), CurrentTime);
            i += 2;
        } else if (!strcmp(c, "mousemove_relative") && i + 1 < argc) {
            XTestFakeRelativeMotionEvent(d, atoi(argv[i]), atoi(argv[i + 1]), CurrentTime);
            i += 2;
        } else if (!strcmp(c, "click")) {
            int repeat = 1, delay = 100;
            while (i < argc && argv[i][0] == '-') {
                if (!strcmp(argv[i], "--repeat")) repeat = atoi(argv[i + 1]);
                if (!strcmp(argv[i], "--delay")) delay = atoi(argv[i + 1]);
                i += 2;
            }
            const unsigned b = i < argc ? (unsigned)atoi(argv[i++]) : 1;
            for (int r = 0; r < repeat; ++r) {
                XTestFakeButtonEvent(d, b, True, CurrentTime);
                XTestFakeButtonEvent(d, b, False, CurrentTime);
                XFlush(d);
                if (r + 1 < repeat) usleep((useconds_t)delay * 1000);
            }
        } else if (!strcmp(c, "mousedown") || !strcmp(c, "mouseup")) {
            XTestFakeButtonEvent(d, (unsigned)atoi(argv[i++]), c[5] == 'd', CurrentTime);
        } else if (!strcmp(c, "key")) {
            while (i < argc && argv[i][0] != '\0' && strcmp(argv[i], "--") != 0) {
                if (argv[i][0] == '-') { i += 2; continue; }   /* --delay N etc. */
                Key(argv[i++]);
                usleep(30000);
            }
        } else if (!strcmp(c, "type")) {
            while (i < argc && argv[i][0] == '-') i += 2;
            if (i >= argc) return 2;
            for (const char* p = argv[i++]; *p; ++p) {
                char k[2] = {*p, 0};
                const char* name = *p == ' ' ? "space" : *p == '.' ? "period" : *p == ',' ? "comma"
                                 : *p == '-' ? "minus" : *p == '/' ? "slash" : *p == '*' ? "asterisk"
                                 : *p == '+' ? "plus" : *p == '(' ? "parenleft" : *p == ')' ? "parenright" : k;
                Key(name);
                usleep(40000);
            }
        } else {
            fprintf(stderr, "mini-xdotool: unsupported '%s'\n", c);
            return 2;
        }
        XFlush(d);
    }
    XSync(d, False);
    XCloseDisplay(d);
    return 0;
}
