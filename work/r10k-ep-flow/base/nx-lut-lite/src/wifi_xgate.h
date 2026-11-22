/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef NXL_WIFI_XGATE_H
#define NXL_WIFI_XGATE_H
#include <X11/Xlib.h>

/* XTest.h is absent from the small build SDK. These are libXtst's public ABI. */
extern Bool XTestQueryExtension(Display *, int *, int *, int *, int *);
extern int XTestFakeKeyEvent(Display *, unsigned int, Bool, unsigned long);

static unsigned long key_ticks(void);
static int wifi_native_idle(void);
static void event_log(const char *, int, int);
static Display *wifi_display;
static int wifi_xerror, wifi_xheld, wifi_xtest, wifi_click_pending;
static unsigned long wifi_click_since;
static Window wifi_relay_target;

static int wifi_xerr(Display *display, XErrorEvent *err) {
    (void)display;
    wifi_xerror = err->error_code;
    return 0;
}
static int wifi_grab(void) {
    wifi_xerror = 0;
    XGrabKey(wifi_display, NXL_WIFI + 8, AnyModifier, DefaultRootWindow(wifi_display),
             False, GrabModeAsync, GrabModeAsync);
    XSync(wifi_display, False);
    return !wifi_xerror;
}
static int wifi_gate_open(void) {
    wifi_display = XOpenDisplay(NULL);
    if (!wifi_display) return 0;
    XSetErrorHandler(wifi_xerr);
    if (!wifi_grab()) {
        XCloseDisplay(wifi_display);
        wifi_display = NULL;
        return 0;
    }
    int event, error, major, minor;
    wifi_xtest = XTestQueryExtension(wifi_display, &event, &error, &major, &minor);
    if (!wifi_xtest) event_log("WIFI_NATIVE_NO_XTEST", 0, 0);
    return 1;
}
static void wifi_gate_close(void) {
    if (wifi_display) {
        XUngrabKey(wifi_display, NXL_WIFI + 8, AnyModifier, DefaultRootWindow(wifi_display));
        XCloseDisplay(wifi_display);
        wifi_display = NULL;
    }
    wifi_xheld = wifi_click_pending = 0;
    wifi_relay_target = None;
}
static void wifi_cancel_click(void) {
    if (wifi_click_pending) event_log("WIFI_NATIVE_INTERRUPTED", 0, 0);
    wifi_click_pending = 0;
}
static void wifi_request_click(unsigned long now) {
    if (!wifi_display || !wifi_xtest) {
        event_log("WIFI_NATIVE_UNAVAILABLE", !!wifi_display, wifi_xtest);
        return;
    }
    wifi_click_since = now;
    wifi_click_pending = 1;
}
/* XTEST uses the same X key event as the original UI. It does not feed evdev. */
static int wifi_native_click(void) {
    unsigned code = NXL_WIFI + 8;
    wifi_xerror = 0;
    XUngrabKey(wifi_display, code, AnyModifier, DefaultRootWindow(wifi_display));
    int down = XTestFakeKeyEvent(wifi_display, code, True, 0);
    int up = XTestFakeKeyEvent(wifi_display, code, False, 0);
    XSync(wifi_display, False);
    int click_error = wifi_xerror;
    int restored = wifi_grab();
    event_log(down && up && !click_error ? "WIFI_NATIVE_CLICK_SENT" : "WIFI_NATIVE_CLICK_FAILED",
              code, click_error);
    if (!restored) event_log("WIFI_NATIVE_REGRAB_FAILED", wifi_xerror, 0);
    return restored;
}

static int wifi_deliver_click(unsigned long now) {
    if (!wifi_click_pending || now - wifi_click_since < 50) return 1;
    char keys[32];
    unsigned code = NXL_WIFI + 8;
    int mapped = XQueryKeymap(wifi_display, keys);
    if (!mapped || wifi_xheld || ((unsigned char)keys[code / 8] & (1u << (code % 8)))) {
        if (now - wifi_click_since >= 1000) {
            wifi_click_pending = 0;
            event_log("WIFI_NATIVE_RELEASE_TIMEOUT", wifi_xheld, mapped);
        }
        return 1;
    }
    wifi_click_pending = 0;
    if (!wifi_native_idle()) { event_log("WIFI_NATIVE_BUSY", 0, 0); return 1; }
    Window focus;
    int revert;
    XGetInputFocus(wifi_display, &focus, &revert);
    if (focus == None) { event_log("WIFI_NATIVE_NO_FOCUS", 0, 0); return 1; }

    return wifi_native_click();
}
static int wifi_gate_drain(void) {
    while (XPending(wifi_display)) {
        XEvent e;
        XNextEvent(wifi_display, &e);
        if (e.type != KeyPress && e.type != KeyRelease) continue;
        if (e.xkey.keycode == NXL_WIFI + 8) {
            if (e.type == KeyPress && !wifi_xheld) {
                int revert;
                XGetInputFocus(wifi_display, &wifi_relay_target, &revert);
                wifi_xheld = 1;
            } else if (e.type == KeyRelease) {
                wifi_xheld = 0;
                wifi_relay_target = None;
            }
            continue;
        }
        Window target = wifi_relay_target;
        int revert;
        if (target <= PointerRoot) XGetInputFocus(wifi_display, &target, &revert);
        if (target == None) continue;
        if (target == PointerRoot) {
            Window root, child;
            int rx, ry, wx, wy;
            unsigned mask;
            if (!XQueryPointer(wifi_display, DefaultRootWindow(wifi_display), &root, &child,
                               &rx, &ry, &wx, &wy, &mask) || child == None) continue;
            target = child;
        }
        e.xkey.window = target;
        e.xkey.subwindow = None;
        e.xkey.send_event = True;
        wifi_xerror = 0;
        XSendEvent(wifi_display, target, False, e.type == KeyPress ? KeyPressMask : KeyReleaseMask, &e);
        XSync(wifi_display, False);
        if (wifi_xerror) return 0;
    }
    return 1;
}
#endif
