#pragma once

#include <X11/Xlib.h>
#include <optional>

#include "layout.h"

struct XState {
    XState()
        : display(XOpenDisplay(nullptr)), root(XDefaultRootWindow(display)),
          screen(XDefaultScreenOfDisplay(display)), resolution(display),
          term_layout(display, Resolution()) {}

    void refresh_resolution() { resolution = {display}; }

    void set_term(Window term) { this->term = term; }
    void focus_term() { XSetInputFocus(display, term.value_or(root), 0, 0); }

    Display *display;
    Window root;
    Screen *screen;
    Resolution resolution;
    WindowLayouts term_layout;
    std::optional<Window> term;
};
