#pragma once

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
    void focus_term() const { XSetInputFocus(display, term.value_or(root), 0, 0); }

    void set_prefix(const ModifiedKeyCode new_prefix) {
        if (prefix.has_value()) {
            XUngrabKey(display, prefix->keycode, prefix->modifiers, root);
        }

        prefix = new_prefix;
        if ((prefix = new_prefix).has_value()) {
            XGrabKey(display, prefix->keycode, prefix->modifiers, root, 0,
                     GrabModeAsync, GrabModeAsync);
        }
    }

    Display *display;
    Window root;
    Screen *screen;
    Resolution resolution;
    WindowLayouts term_layout;

    std::optional<Window> term;
    std::optional<ModifiedKeyCode> prefix;
};
