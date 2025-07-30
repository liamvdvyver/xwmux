#ifndef INSTANCE_H
#define INSTANCE_H

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <optional>
#include <ostream>
#include <pthread.h>
#include <vector>

#include "layout.h"
#include <ipc.h>
#include <listen.h>

const char EXIT_KEYSYM = 'q';
const std::string ROOT_CLASS = "xwmux_root";

class WMInstance {

  public:
    WMInstance()
        : m_display(XOpenDisplay(nullptr)),
          m_root(XDefaultRootWindow(m_display)) {
        if (!m_display) {
            // err
            exit(EXIT_FAILURE);
        }
    };

    void init() {

        // set error handler
        XSetErrorHandler(*error_handler);

        XSelectInput(m_display, m_root,
                     SubstructureRedirectMask | SubstructureNotifyMask);
        XSync(m_display, false);

        // grab q
        XGrabKey(m_display, XKeysymToKeycode(m_display, EXIT_KEYSYM), Mod4Mask,
                 m_root, 1, 1, 1);
    };

    int run() {

        // Open terminal
        open_term();

        XEvent ev;

        // run listener
        pthread_t listener;
        pthread_create(&listener, nullptr, Listener::operator(), (void *)this);

        while (1) {

            if (!m_term.has_value()) {
                open_term();
            }

            // Get event
            if (XNextEvent(m_display, &ev)) {
                // TODO: need check?
            };

            // Handle
            handle_event(ev);
        }
    };

    ~WMInstance() { XCloseDisplay(m_display); }

    void set_resolution(Resolution res) {
        m_setting_lk.lock();
        m_res = res;
        m_setting_lk.unlock();
    }

  private:
    Display *m_display;
    Window m_root;
    std::optional<Window> m_term{};
    std::vector<Window> m_windows{};
    Resolution m_res;
    std::mutex m_setting_lk;

    static int error_handler(_XDisplay *display, XErrorEvent *err) {
        (void)display;
        (void)err;
        return EXIT_FAILURE;
    };

    void handle_keypress(XEvent ev) {
        std::cout << ev.xkey.keycode << std::endl;
        if (ev.xkey.keycode == XKeysymToKeycode(m_display, EXIT_KEYSYM)) {
            exit(EXIT_SUCCESS);
        }
        return;
    }

    void open_term() {
        // TODO: make configurable
        std::string root_term_cmd = "kitty --detach";
        root_term_cmd.append(" --class ");
        root_term_cmd.append(ROOT_CLASS);
        root_term_cmd.append(" --exec tmux-sessioniser default");

        std::system(root_term_cmd.c_str());
    }

    // Check if window is a root terminal.
    constexpr bool is_root_term(XMapRequestEvent req) {
        XClassHint *hint = XAllocClassHint();
        XGetClassHint(m_display, req.window, hint);
        return std::strcmp(hint->res_class, ROOT_CLASS.c_str()) == 0;
        XFree(hint);
    }

    // If the window has appropriate class name,
    void set_term(XMapRequestEvent req) {

        // Close current window
        if (m_term.has_value()) {
            XKillClient(m_display, m_term.value());
        }

        // Use new window as root
        m_term = req.window;
        XMoveResizeWindow(m_display, req.window, 0, 0,
                          DefaultScreenOfDisplay(m_display)->width,
                          DefaultScreenOfDisplay(m_display)->height);
    }

    void handle_event(XEvent ev) {
        switch (ev.type) {
        case CreateNotify:
            break;
        case ConfigureRequest:
            break;
        case ConfigureNotify:
            break;
        case MapRequest:
            XMapWindow(m_display, ev.xmaprequest.window);
            XSetInputFocus(m_display, ev.xmaprequest.window, 2, 0);
            if (is_root_term(ev.xmaprequest)) {
                set_term(ev.xmaprequest);
            }
            XSync(m_display, false);
            break;
        case ReparentNotify:
            break;
        case MapNotify:
            break;
        case Expose:
            break;
        case UnmapNotify:
            break;
        case DestroyNotify:
            if (ev.xdestroywindow.window == m_term) {
                m_term = {};

                // Reopen terminal
                open_term();
            }
            break;
        case KeyPress:
            handle_keypress(ev);
            break;
        default:
            // std::unreachable();
            break;
        }
    }
};

#endif
