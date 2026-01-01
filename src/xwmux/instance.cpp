#include "instance.h"

#include "layout.h"
#include "log.h"
#include "tmux.h"

#include <X11/X.h>
#include <X11/Xlib.h>

bool WMInstance::m_existing_wm = false;

template <>
void WMInstance::handle_x_event<ConfigureNotify>(XConfigureEvent &ev) {
    if (ev.window == m_xstate.root) {
        m_xstate.set_resolution(
            {static_cast<size_t>(ev.width), static_cast<size_t>(ev.height)});
        m_xstate.close_term();
    }
}

template <> void WMInstance::handle_x_event<MapRequest>(XMapRequestEvent &ev) {

    Window w = ev.window;
    if (m_xstate.override_redirect(w)) {
    } else if (m_xstate.is_root_term(w)) {
        m_xstate.resolution.fullscreen().resize_to(m_xstate.display, w);
        XLowerWindow(m_xstate.display, w);
        XMapWindow(m_xstate.display, w);
        m_xstate.set_term(w);
        m_xstate.focus_term();
    } else if (!m_pending_windows.count(w)) {

        if (m_xstate.init_state(w).value_or(NormalState) != NormalState) {
            log_msg("Window started in iconic state\n");
        }

        m_window_q.push(w);
        m_pending_windows.insert(w);
        split_window();

        // Watch for name changes
        XSelectInput(m_xstate.display, w, PropertyChangeMask);
    }
}

template <> void WMInstance::handle_x_event<UnmapNotify>(XUnmapEvent &ev) {
    if (m_tmux_mapping.has_window(ev.window)) {
        WindowPane &wp = m_tmux_mapping.get(ev.window);
        if (wp.unmap_pending()) {
            wp.notify_unmapped();
        } else {
            m_tmux_mapping.remove_window(ev.window);
            m_xstate.focus_term();
        }
    } else {
        m_pending_windows.erase(ev.window);
    }
}

template <>
void WMInstance::handle_x_event<DestroyNotify>(XDestroyWindowEvent &ev) {
    if (ev.window == m_xstate.term) {
        m_xstate.term = {};
        m_xstate.open_term();
        m_xstate.focus_term();
    } else if (!m_tmux_mapping.has_window(ev.window)) {
        m_pending_windows.erase(ev.window);
        // Not focused yet, do not focus terminal
    } else {
        m_tmux_mapping.remove_window(ev.window);
        m_xstate.focus_term();
    }
}

template <> void WMInstance::handle_x_event<KeyPress>(XEvent &ev) {
    XKeyPressedEvent &k_ev = ev.xkey;
    if (k_ev.keycode == m_xstate.prefix->keycode &&
        k_ev.state == m_xstate.prefix->modifiers) {
        // Release the active grab, and generate focus in event
        XUngrabKeyboard(m_xstate.display, CurrentTime);

        // If terminal is focused (prefix active), overriding gui,
        // redirect the event to the gui window
        if (m_tmux_mapping.overridden()) {

            if (std::system("tmux send-keys -K escape")) {
                log_msg("Failed to send escape.\n");
            };

            m_tmux_mapping.release_override(m_xstate);

            // Send to current window
            k_ev.display = m_xstate.display;
            k_ev.root = XDefaultRootWindow(m_xstate.display);
            k_ev.same_screen = True;
            k_ev.time = CurrentTime;
            k_ev.subwindow = None;
            k_ev.window = m_tmux_mapping.current_window();
            XSendEvent(m_xstate.display, k_ev.window, 0, 0, &ev);
            return;
        }

        if (!m_xstate.term.has_value()) {
            return;
        }

        // If gui has focus, move to terminal, send prefix and mark
        // overridden
        if (m_tmux_mapping.is_filled()) {

            // Focus terminal, ignore incoming focus notifications
            // to avoid WMInstance::re-focusing gui window
            m_ignore_focus = true;
            m_xstate.focus_term();
            m_ignore_focus = false;

            m_tmux_mapping.override();
        }

        // Ensure tmux gets focus
        m_xstate.sync();

        send_prefix();
    }
}

template <>
void WMInstance::handle_x_event<PropertyNotify>(XPropertyEvent &ev) {
    if (m_tmux_mapping.has_window(ev.window) &&
        ev.atom == XInternAtom(m_xstate.display, "WM_NAME", 0)) {
        TmuxPaneID pane = m_tmux_mapping.find(ev.window).second;
        name_client(ev.window, pane);
    }
}

template <>
void WMInstance::handle_client_msg<MsgType::RESOLUTION>(const Msg &msg) {
    m_xstate.term_layout.set_term_resolution(msg.res_chars(), msg.res_px());

    m_xstate.term_layout.set_bar_position(
        static_cast<TmuxBarPosition>(msg.bar_pos()));
}

template <>
void WMInstance::handle_client_msg<MsgType::PREFIX>(const Msg &msg) {
    m_xstate.set_prefix(msg.mod_kc());
}

template <>
void WMInstance::handle_client_msg<MsgType::KILL_PANE>(const Msg &msg) {
    (void)msg;
    if (m_tmux_mapping.is_filled()) {
        Window w = m_tmux_mapping.current_window();
        m_tmux_mapping.kill_client(w, m_xstate.display);
        m_tmux_mapping.remove_window(w);
    }
    // Pane should be killed normally on unmap notify.
}

template <>
void WMInstance::handle_client_msg<MsgType::KILL_ORPHANS>(const Msg &msg) {
    (void)msg;
    for (Window w : m_tmux_mapping.find_orphans()) {
        m_tmux_mapping.kill_client(w, m_xstate.display);
        m_tmux_mapping.remove_window(w);
    }
}

template <> void WMInstance::handle_client_msg<MsgType::EXIT>(const Msg &msg) {
    (void)msg;
    stop();
}

template <>
void WMInstance::handle_client_msg<MsgType::TMUX_POSITION>(const Msg &msg) {
    m_tmux_mapping.move_pane(msg.tm_location());

    if (msg.focused() && !m_ignore_focus) {

        // Have window to add
        if (!m_window_q.empty() &&
            !m_tmux_mapping.is_filled(msg.tm_location()) && msg.dead()) {

            Window window = m_window_q.front();
            m_window_q.pop();

            // Already destroyed or window already mapped, so kill the pane
            // TODO: avoid WMInstance::adding to queue twice instead?
            if (!m_pending_windows.count(window) ||
                m_tmux_mapping.has_window(window)) {
                kill_pane(msg.tm_location().second);

            } else {
                m_tmux_mapping.add_window(m_xstate, window, msg.tm_location());
                m_pending_windows.erase(window);
                name_client(window, msg.tm_location().second);
            }
        }

        m_tmux_mapping.set_active(m_xstate, msg.tm_location(), msg.zoomed());
    }

    // set_position(msg.tm_location(), msg.window_position());
    // Moves the (possible window) at location to term_position
    // If pane doesn't have a window, do nothing
    // If pane is in seperate window, moves the pane
    WindowPosition gui_position = m_xstate.term_layout.term_to_screen_pos(
        m_xstate.term_layout.add_bar(msg.window_position()));

    if (m_tmux_mapping.is_filled(msg.tm_location())) {
        m_tmux_mapping[msg.tm_location()].set_position(m_xstate, gui_position);
    }
}

template <>
void WMInstance::handle_x_event<ClientMessage>(XClientMessageEvent &ev) {
    const Msg msg{ev};
    if (ev.message_type ==
        msg_type_atom(m_xstate.display, MsgType::RESOLUTION)) {
        handle_client_msg<MsgType::RESOLUTION>(msg);
    } else if (ev.message_type ==
               msg_type_atom(m_xstate.display, MsgType::PREFIX)) {
        handle_client_msg<MsgType::PREFIX>(msg);
    } else if (ev.message_type ==
               msg_type_atom(m_xstate.display, MsgType::EXIT)) {
        handle_client_msg<MsgType::EXIT>(msg);
    } else if (ev.message_type ==
               msg_type_atom(m_xstate.display, MsgType::KILL_PANE)) {
        handle_client_msg<MsgType::KILL_PANE>(msg);
    } else if (ev.message_type ==
               msg_type_atom(m_xstate.display, MsgType::KILL_ORPHANS)) {
        handle_client_msg<MsgType::KILL_ORPHANS>(msg);
    } else if (ev.message_type ==
               msg_type_atom(m_xstate.display, MsgType::TMUX_POSITION)) {
        handle_client_msg<MsgType::TMUX_POSITION>(msg);
    }
}

void WMInstance::handle_event(XEvent &ev) {
    switch (ev.type) {
    case ConfigureNotify:
        handle_x_event<ConfigureNotify>(ev.xconfigure);
        break;
    case MapRequest:
        handle_x_event<MapRequest>(ev.xmaprequest);
        break;
    case UnmapNotify:
        handle_x_event<UnmapNotify>(ev.xunmap);
        break;
    case DestroyNotify:
        handle_x_event<DestroyNotify>(ev.xdestroywindow);
        break;
    case KeyRelease:
        break;
    case KeyPress:
        handle_x_event<KeyPress>(ev);
        break;
    case ClientMessage:
        handle_x_event<ClientMessage>(ev.xclient);
        break;
    case PropertyNotify:
        handle_x_event<PropertyNotify>(ev.xproperty);
        break;
    default:
        break;
    }
}
