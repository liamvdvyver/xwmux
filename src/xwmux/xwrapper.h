#pragma once

#include <X11/Xlib.h>
#include <optional>

#include "layout.h"

struct XState {
    XState()
        : display(XOpenDisplay(nullptr)), root(XDefaultRootWindow(display)),
          screen(XDefaultScreenOfDisplay(display)), resolution(display),
          term_layout(display, Resolution()) {}

    void set_resolution(Resolution res) {
        resolution = res;
        term_layout.set_screen_resolution(resolution);
        if (term.has_value()) {
            res.fullscreen().resize_to(display, term.value());
        }
        set_term(term.value());
    }

    void set_term(Window term) { this->term = term; }
    void focus_term() { XSetInputFocus(display, term.value_or(root), 0, 0); }

    Display *display;
    Window root;
    Screen *screen;
    Resolution resolution;
    WindowLayouts term_layout;
    std::optional<Window> term;
};
