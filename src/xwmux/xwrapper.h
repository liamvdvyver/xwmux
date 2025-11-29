#pragma once

extern "C" {
#include <X11/Xlib.h>
#include <X11/Xutil.h>
}

#include <cassert>
#include <cstring>
#include <optional>
#include <string>

#include "layout.h"

const std::string ROOT_CLASS = "xwmux_root";

struct ModifiedKeyCode {
    ModifiedKeyCode(const KeyCode keycode, const uint modifiers)
        : keycode(keycode), modifiers(modifiers) {}

    KeyCode keycode;
    uint modifiers;
};

struct XState {
    XState()
        : display(XOpenDisplay(nullptr)), root(XDefaultRootWindow(display)),
          screen(XDefaultScreenOfDisplay(display)), resolution(display),
          term_layout(display, Resolution()) {}

    void set_resolution(const Resolution res) {
        resolution = res;
        term_layout.set_screen_resolution(resolution);
        if (term.has_value()) {
            res.fullscreen().resize_to(display, term.value());
            set_term(term.value());
        }
    }

    void set_term(const Window term) { this->term = term; }

    void sync() { XSync(display, 0); }

    void focus_term() const {
        XSetInputFocus(display, term.value_or(root), 0, 0);
    }

    void set_prefix(const ModifiedKeyCode new_prefix) {
        if (grabbed) {
            XUngrabKey(display, prefix->keycode, prefix->modifiers, root);
            XGrabKey(display, new_prefix.keycode, new_prefix.modifiers, root,
                     true, GrabModeAsync, GrabModeSync);
        }
        prefix = new_prefix;
    }

    void grab_prefix() {
        assert(prefix.has_value());
        if (!grabbed && prefix.has_value()) {
            XGrabKey(display, prefix->keycode, prefix->modifiers, root, true,
                     GrabModeAsync, GrabModeSync);
        }
        grabbed = true;
    }

    void ungrab_prefix() {
        assert(prefix.has_value());
        if (grabbed && prefix.has_value()) {
            XUngrabKey(display, prefix->keycode, prefix->modifiers, root);
        }
        grabbed = false;
    }

    constexpr bool is_root_term(Window id) {
        XClassHint *hint = XAllocClassHint();
        bool ret = XGetClassHint(display, id, hint)
                       ? std::strcmp(hint->res_class, ROOT_CLASS.c_str()) == 0
                       : false;
        XFree(hint);
        return ret;
    }

    int open_term() { return std::system("xwmux-launch-term.sh"); }

    void close_term() {
        // Close current window
        if (term.has_value()) {
            XKillClient(display, term.value());
        }
    }

    void kill_client(Window window) {
        // Close window
        // https://nachtimwald.com/2009/11/08/sending-wm_delete_window-client-messages/
        XEvent ev;
        ev.xclient.type = ClientMessage;
        ev.xclient.window = window;
        ev.xclient.message_type = XInternAtom(display, "WM_PROTOCOLS", true);
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = XInternAtom(display, "WM_DELETE_WINDOW", false);
        ev.xclient.data.l[1] = CurrentTime;
        XSendEvent(display, window, False, NoEventMask, &ev);
    }

    constexpr bool iconic(Window id) {
        XWindowAttributes attr;
        XGetWindowAttributes(display, id, &attr);
        return attr.map_state == IconicState;
    }

    constexpr bool override_redirect(Window id) {
        XWindowAttributes attr;
        XGetWindowAttributes(display, id, &attr);
        return attr.override_redirect;
    }

    Display *display;
    Window root;
    Screen *screen;
    Resolution resolution;
    WindowLayouts term_layout;

    std::optional<Window> term;

    std::optional<ModifiedKeyCode> prefix;
    bool grabbed;
};
