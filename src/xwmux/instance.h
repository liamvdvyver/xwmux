#pragma once

#include <unistd.h>
extern "C" {
#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/extensions/XTest.h>
}

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <queue>
#include <unordered_set>

#include "ipc.h"
#include "layout.h"
#include "tmux.h"

constexpr void notify(std::string_view msg) {
    std::system(std::format("notify-send '{}'", msg).c_str());
}

const std::string ROOT_CLASS = "xwmux_root";

class WMInstance {

  public:
    WMInstance() : m_xstate() {
        init();
        run();
    };

    ~WMInstance() { stop(); }

    void init() {

        if (!m_xstate.display) {
            std::cerr << "Failed to open display\n";
            exit(EXIT_FAILURE);
        }

        // set error handler: for startup
        XSetErrorHandler(*startup_error_handler);

        XSelectInput(m_xstate.display, m_xstate.root,
                     StructureNotifyMask | SubstructureRedirectMask |
                         SubstructureNotifyMask);

        sync();

        if (m_existing_wm) {
            std::cerr << "Another wm is running\n";
            exit(EXIT_FAILURE);
        }

        XSetErrorHandler(*runtime_handler);
    };

    void run() {

        // Open terminal
        open_term();

        XEvent ev;

        // set cursor
        XDefineCursor(m_xstate.display, m_xstate.root,
                      XCreateFontCursor(m_xstate.display, XC_left_ptr));

        while (!m_stop) {

            // Handle event
            XNextEvent(m_xstate.display, &ev);
            handle_event(ev);
            sync();
        }
    };

    //--- Handle IPC events --------------------------------------------------//

    const XState &get_xstate() { return m_xstate; }

    void set_term_layout(const Resolution term_resolution,
                         const Resolution pixel_grid_resolution,
                         TmuxBarPosition bar_position) {
        m_xstate.term_layout.set_term_resolution(term_resolution,
                                                 pixel_grid_resolution);

        m_xstate.term_layout.set_bar_position(bar_position);
    }

    void stop() {
        XCloseDisplay(m_xstate.display);
        for (auto [tm_window, workspace] : m_tmux_mapping.get_workspaces()) {
            for (auto [tm_pane, w] : workspace.get_windows()) {
                kill_pane(tm_pane);
            }
        }
        exit(EXIT_SUCCESS);
    }

    void focus_tmux(TmuxLocation location, bool zoomed = false) {

        if (m_ignore_focus) {
            return;
        }

        // Have window to add
        if (!m_window_q.empty() && !m_tmux_mapping.is_filled(location)) {

            Window window = m_window_q.front();
            m_window_q.pop();

            // Already destroyed or window already mapped, so kill the pane
            // TODO: avoid adding to queue twice instead?
            if (!m_pending_windows.count(window) ||
                m_tmux_mapping.has_window(window)) {
                kill_pane(location.second);

            } else {
                m_tmux_mapping.add_window(m_xstate, window, location);
                m_pending_windows.erase(window);
                name_client(window, location.second);
            }
        }

        m_tmux_mapping.set_active(m_xstate, location, zoomed);
    }

    void name_client(Window window, TmuxPaneID pane) {
        XTextProperty name;
        XGetWMName(m_xstate.display, window, &name);
        std::string_view name_v(reinterpret_cast<char *>(name.value),
                                name.nitems);
        name_pane(pane, name_v);
        XFree(name.value);
    }

    void kill_client(Window window) {
        // Close window
        // https://nachtimwald.com/2009/11/08/sending-wm_delete_window-client-messages/
        XEvent ev;
        ev.xclient.type = ClientMessage;
        ev.xclient.window = window;
        ev.xclient.message_type =
            XInternAtom(m_xstate.display, "WM_PROTOCOLS", true);
        ev.xclient.format = 32;
        ev.xclient.data.l[0] =
            XInternAtom(m_xstate.display, "WM_DELETE_WINDOW", false);
        ev.xclient.data.l[1] = CurrentTime;
        XSendEvent(m_xstate.display, window, False, NoEventMask, &ev);
    }

    // Kills the focused client, if any
    void kill_pane_client() {
        if (m_tmux_mapping.is_filled()) {
            kill_client(m_tmux_mapping.current_window());
        }
        // Pane should be killed normally on unmap notify.
    }

    // Moves the (possible window) at location to term_position
    // If pane doesn't have a window, do nothing
    // If pane is in seperate window, moves the pane
    void set_position(TmuxLocation location, WindowPosition term_position) {
        WindowPosition gui_position = m_xstate.term_layout.term_to_screen_pos(
            m_xstate.term_layout.add_bar(term_position));

        if (m_tmux_mapping.is_filled(location)) {
            m_tmux_mapping[location.first][location.second].set_position(
                m_xstate, gui_position);
        }
    }

    void forward_prefix(XEvent &ev) {

        // Release the active grab, and generate focus in event
        XUngrabKeyboard(m_xstate.display, CurrentTime);

        // Ensure tmux will have the event delivered before sending prefix
        sync();

        // If terminal is focused (prefix active), overriding gui,
        // redirect the event to the gui window
        if (m_tmux_mapping.overridden()) {

            std::system("tmux send-keys -K escape");

            m_tmux_mapping.release_override(m_xstate);

            // Send to current window
            ev.xkey.display = m_xstate.display;
            ev.xkey.root = XDefaultRootWindow(m_xstate.display);
            ev.xkey.same_screen = True;
            ev.xkey.time = CurrentTime;
            ev.xkey.subwindow = None;
            ev.xkey.window = m_tmux_mapping.current_window();
            XSendEvent(m_xstate.display, ev.xkey.window, 0, 0, &ev);
            return;
        }

        if (!m_xstate.term.has_value()) {
            return;
        }

        // Instead, release grab (without changing focus), and manually
        // send the keys
        // XAllowEvents(m_xstate.display, AsyncKeyboard, CurrentTime);
        // sync();

        // If gui has focus, move to terminal, send prefix and mark
        // overridden
        if (m_tmux_mapping.is_filled()) {

            // Focus terminal, ignore incoming focus notifications
            // to avoid re-focusing gui window
            m_ignore_focus = true;
            m_xstate.focus_term();
            m_ignore_focus = false;

            m_tmux_mapping.override();

            // Ensure tmux gets focus
            sync();
        }

        // Sending raw events causes trouble with some terminal emulators
        // This introduces ~5ms of delay, which is roughly what is needed to
        // avoid those issues anyway.
        // TODO: revisit this logic
        send_prefix();
    }

    void sync() { XSync(m_xstate.display, 0); }

  private:
    // State
    XState m_xstate;
    TmuxXWindowMapping m_tmux_mapping;

    std::queue<Window> m_window_q;
    std::unordered_set<Window> m_pending_windows;

    bool m_stop = false;
    static bool m_existing_wm;

    // When sending tmux commands to pane from gui focus
    bool m_ignore_focus = false;

    static int startup_error_handler(_XDisplay *display, XErrorEvent *err) {
        assert(err->error_code == BadAccess);
        m_existing_wm = true;
        (void)display;
        (void)err;
        return EXIT_FAILURE;
    }

    static int runtime_handler(_XDisplay *display, XErrorEvent *err) {

        size_t err_msg_len = 100;
        char *err_msg = (char *)malloc(err_msg_len + 1);
        XGetErrorText(display, err->error_code, err_msg, err_msg_len);

        send_message(err_msg);
        return EXIT_SUCCESS;
    }

    int open_term() { return std::system("xwmux-launch-term.sh"); }

    // Check if window is a root terminal.
    constexpr bool is_root_term(Window id) {
        XClassHint *hint = XAllocClassHint();
        bool ret = XGetClassHint(m_xstate.display, id, hint)
                       ? std::strcmp(hint->res_class, ROOT_CLASS.c_str()) == 0
                       : false;
        XFree(hint);
        return ret;
    }

    constexpr bool iconic(Window id) {
        XWindowAttributes attr;
        XGetWindowAttributes(m_xstate.display, id, &attr);
        return attr.map_state == IconicState;
    }

    constexpr bool override_redirect(Window id) {
        XWindowAttributes attr;
        XGetWindowAttributes(m_xstate.display, id, &attr);
        return attr.override_redirect;
    }

    void close_term() {
        // Close current window
        if (m_xstate.term.has_value()) {
            XKillClient(m_xstate.display, m_xstate.term.value());
        }
    }

    // If the window has appropriate class name,
    void set_term(Window id) {

        close_term();

        // Use new window as root
        m_xstate.term = id;
    }

    void handle_event(XEvent &ev) {
        switch (ev.type) {
        case CreateNotify:
            break;
        case ConfigureRequest:
            break;
        case ConfigureNotify:
            if (ev.xconfigure.window == m_xstate.root) {
                m_xstate.set_resolution(
                    {static_cast<size_t>(ev.xconfigure.width),
                     static_cast<size_t>(ev.xconfigure.height)});
                close_term();
            }
            break;
        case MapRequest:
            [&](Window w) {
                if (override_redirect(w)) {
                } else if (is_root_term(w)) {
                    m_xstate.resolution.fullscreen().resize_to(m_xstate.display,
                                                               w);
                    XLowerWindow(m_xstate.display, w);
                    XMapWindow(m_xstate.display, w);
                    m_xstate.set_term(w);
                    m_xstate.focus_term();
                } else if (!m_pending_windows.count(w) && !iconic(w)) {

                    m_window_q.push(w);
                    m_pending_windows.insert(w);
                    split_window();

                    // Watch for name changes
                    XSelectInput(m_xstate.display, w, PropertyChangeMask);
                }
            }(ev.xmaprequest.window);
            break;
        case ReparentNotify:
            break;
        case MapNotify:
            break;
        case Expose:
            break;
        case UnmapNotify:
            // If originated from xwmux
            {
                if (m_tmux_mapping.has_window(ev.xunmap.window)) {
                    WindowPane &wp = m_tmux_mapping.get(ev.xunmap.window);
                    if (wp.unmap_pending()) {
                        wp.notify_unmapped();
                    } else {
                        m_tmux_mapping.remove_window(ev.xunmap.window);
                        m_xstate.focus_term();
                    }
                } else {
                    m_pending_windows.erase(ev.xunmap.window);
                }
            }
            break;
        case DestroyNotify:
            if (ev.xdestroywindow.window == m_xstate.term) {
                m_xstate.term = {};
                open_term();
                m_xstate.focus_term();
            } else if (!m_tmux_mapping.has_window(ev.xdestroywindow.window)) {
                m_pending_windows.erase(ev.xdestroywindow.window);
                // Not focused yet, do not focus terminal
            } else {
                m_tmux_mapping.remove_window(ev.xdestroywindow.window);
                m_xstate.focus_term();
            }
            break;
        case KeyRelease:
            notify("release");
            break;
        case KeyPress:
            if (ev.xkey.keycode == m_xstate.prefix->keycode &&
                ev.xkey.state == m_xstate.prefix->modifiers) {
                forward_prefix(ev);
            }
            break;

        case ClientMessage:
            // Handle ipc message
            {
                Msg msg{ev};
                if (ev.xclient.message_type ==
                    msg_type_atom(m_xstate.display, MsgType::RESOLUTION)) {
                    set_term_layout(
                        msg.res_chars(), msg.res_px(),
                        static_cast<TmuxBarPosition>(msg.bar_pos()));
                } else if (ev.xclient.message_type ==
                           msg_type_atom(m_xstate.display, MsgType::PREFIX)) {
                    m_xstate.set_prefix(msg.mod_kc());
                } else if (ev.xclient.message_type ==
                           msg_type_atom(m_xstate.display, MsgType::EXIT)) {
                    stop();
                } else if (ev.xclient.message_type ==
                           msg_type_atom(m_xstate.display,
                                         MsgType::KILL_PANE)) {
                    kill_pane_client();
                } else if (ev.xclient.message_type ==
                           msg_type_atom(m_xstate.display,
                                         MsgType::TMUX_POSITION)) {

                    m_tmux_mapping.move_pane(msg.tm_location());
                    if (msg.focused()) {
                        focus_tmux(msg.tm_location(), msg.zoomed());
                    }
                    set_position(msg.tm_location(), msg.window_position());
                }

                // Check for transtion to iconic
                // if (static_cast<Atom>(ev.xclient.type) ==
                //     XInternAtom(m_xstate.display, "WM_CHANGE_STATE", false))
                //     { notify("Change state!!");
                // };
            }
            break;

        case PropertyNotify:
            if (m_tmux_mapping.has_window(ev.xproperty.window) &&
                ev.xproperty.atom ==
                    XInternAtom(m_xstate.display, "WM_NAME", 0)) {
                TmuxPaneID pane =
                    m_tmux_mapping.find(ev.xproperty.window).second;
                name_client(ev.xproperty.window, pane);
            }
            break;
        default:
            break;
        }
    }
};
