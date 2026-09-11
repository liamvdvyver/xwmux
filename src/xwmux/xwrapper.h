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
#include "log.h"

const std::string ROOT_CLASS = "xwmux_root";

struct ModifiedKeyCode {
    ModifiedKeyCode(const KeyCode keycode, const uint modifiers)
        : keycode(keycode), modifiers(modifiers) {}

    ModifiedKeyCode() : keycode(0), modifiers(0) {}

    KeyCode keycode;
    uint modifiers;
};

struct XState {
    XState()
        : display(XOpenDisplay(nullptr)), root(XDefaultRootWindow(display)),
          screen(XDefaultScreenOfDisplay(display)), resolution(display),
          term_layout(display, Resolution()), term(std::nullopt), prefix(),
          prefix_initialized(false), grabbed(false) {}

    void set_resolution(const Resolution res) {
        resolution = res;
        term_layout.set_screen_resolution(resolution);
        if (term.has_value()) {
            res.fullscreen().resize_to(display, term.value());
            set_term(term.value());
        }
    }

    void set_term(const Window term) {
        xwmux::log_debug(std::format("XState::set_term window={}\n",
                                     static_cast<unsigned long>(term)));
        this->term = term;
    }

    void sync() { XSync(display, 0); }

    void focus_term() const {
        const Window target = term.value_or(root);
        xwmux::log_debug(std::format("XState::focus_term target={} has_term={}\n",
                                     static_cast<unsigned long>(target),
                                     term.has_value()));
        XSetInputFocus(display, target, 0, 0);
    }

    void set_prefix(const ModifiedKeyCode new_prefix) {
        xwmux::log_debug(std::format(
            "XState::set_prefix keycode={} modifiers={} grabbed={} had_prefix={}\n",
            new_prefix.keycode, new_prefix.modifiers, grabbed,
            prefix_initialized));
        if (grabbed && prefix_initialized) {
            XUngrabKey(display, prefix.keycode, prefix.modifiers, root);
            XGrabKey(display, new_prefix.keycode, new_prefix.modifiers, root,
                     true, GrabModeAsync, GrabModeSync);
        }
        prefix = new_prefix;
        prefix_initialized = true;
    }

    void grab_prefix() {
        if (!prefix_initialized) {
            xwmux::log_debug(
                "XState::grab_prefix skipped because prefix is not initialized\n");
            return;
        }

        xwmux::log_debug(std::format(
            "XState::grab_prefix keycode={} modifiers={} already_grabbed={}\n",
            prefix.keycode, prefix.modifiers, grabbed));
        if (!grabbed) {
            XGrabKey(display, prefix.keycode, prefix.modifiers, root, true,
                     GrabModeAsync, GrabModeSync);
        }
        grabbed = true;
    }

    void ungrab_prefix() {
        if (!prefix_initialized) {
            xwmux::log_debug(
                "XState::ungrab_prefix skipped because prefix is not initialized\n");
            grabbed = false;
            return;
        }

        xwmux::log_debug(std::format(
            "XState::ungrab_prefix keycode={} modifiers={} was_grabbed={}\n",
            prefix.keycode, prefix.modifiers, grabbed));
        if (grabbed) {
            XUngrabKey(display, prefix.keycode, prefix.modifiers, root);
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

    constexpr std::optional<int> init_state(Window id) {
        WrappedHints whints(display, id);
        if (whints.get() && whints.get()->flags & StateHint)
            return whints.get()->initial_state;
        return std::nullopt;
    }

    std::optional<bool> override_redirect(Window id) {
        XWindowAttributes attr;
        // A MapRequest can remain queued after its client destroys the window.
        // XGetWindowAttributes synchronously reports that expected BadWindow;
        // suppress it only for this existence check, then restore xwmux's
        // runtime handler for all other X requests.
        XErrorHandler previous_handler =
            XSetErrorHandler(ignore_expected_bad_window);
        const Status status = XGetWindowAttributes(display, id, &attr);
        XSetErrorHandler(previous_handler);
        if (!status) {
            return std::nullopt;
        }
        return attr.override_redirect;
    }

    Display *display;
    Window root;
    Screen *screen;
    Resolution resolution;
    WindowLayouts term_layout;

    std::optional<Window> term;

    ModifiedKeyCode prefix;
    bool prefix_initialized = false;
    bool grabbed{};

  private:
    static int ignore_expected_bad_window(Display *, XErrorEvent *) {
        return 0;
    }

    struct WrappedHints {
      public:
        WrappedHints(Display *display, Window id)
            : hints(XGetWMHints(display, id)) {}
        ~WrappedHints() {
            XFree(hints);
        }
        WrappedHints(const WrappedHints &other) = delete;
        WrappedHints &operator=(const WrappedHints &other) = delete;
        const XWMHints *get() const { return hints; };

      private:
        XWMHints *const hints;
    };
};
