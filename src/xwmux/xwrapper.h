#pragma once

#include <cassert>
extern "C" {
#include <X11/Xlib.h>
}

#include <optional>

#include "layout.h"

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
    void focus_term() const {
        XSetInputFocus(display, term.value_or(root), 0, 0);
    }

    void set_prefix(const ModifiedKeyCode new_prefix) {
        if (grabbed) {
            XUngrabKey(display, new_prefix.keycode, new_prefix.modifiers, root);
            XGrabKey(display, new_prefix.modifiers, new_prefix.modifiers, root,
                     true, GrabModeAsync, GrabModeSync);
        }

        prefix = new_prefix;
    }

    void grab_prefix() {
        assert(prefix.has_value());
        if (!grabbed && prefix.has_value()) {
            XGrabKey(display, prefix->keycode, prefix->modifiers, root, true,
                     GrabModeAsync, GrabModeSync);
            grabbed = true;
        }
    }

    void ungrab_prefix() {
        assert(prefix.has_value());
        if (grabbed && prefix.has_value()) {
            XUngrabKey(display, prefix->keycode, prefix->modifiers, root);
            grabbed = false;
        }
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
