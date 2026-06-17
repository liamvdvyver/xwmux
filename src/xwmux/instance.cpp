#include "instance.h"

#include "layout.h"
#include "log.h"
#include "tmux.h"

#include <X11/X.h>
#include <X11/Xlib.h>
#include <unistd.h>

bool WMInstance::m_existing_wm = false;

namespace {

std::string x_event_name(const int type) {
    switch (type) {
    case ConfigureNotify:
        return "ConfigureNotify";
    case ConfigureRequest:
        return "ConfigureRequest";
    case MapRequest:
        return "MapRequest";
    case UnmapNotify:
        return "UnmapNotify";
    case DestroyNotify:
        return "DestroyNotify";
    case KeyPress:
        return "KeyPress";
    case KeyRelease:
        return "KeyRelease";
    case ClientMessage:
        return "ClientMessage";
    case PropertyNotify:
        return "PropertyNotify";
    default:
        return std::format("XEvent({})", type);
    }
}

std::string msg_type_name(Display *display, const Atom atom) {
    if (atom == msg_type_atom(display, MsgType::RESOLUTION)) {
        return "RESOLUTION";
    }
    if (atom == msg_type_atom(display, MsgType::PREFIX)) {
        return "PREFIX";
    }
    if (atom == msg_type_atom(display, MsgType::EXIT)) {
        return "EXIT";
    }
    if (atom == msg_type_atom(display, MsgType::TMUX_POSITION)) {
        return "TMUX_POSITION";
    }
    if (atom == msg_type_atom(display, MsgType::KILL_PANE)) {
        return "KILL_PANE";
    }
    if (atom == msg_type_atom(display, MsgType::KILL_ORPHANS)) {
        return "KILL_ORPHANS";
    }
    if (atom == msg_type_atom(display, MsgType::DUMP_STATE)) {
        return "DUMP_STATE";
    }
    return std::format("Atom({})", atom);
}

std::string msg_summary(Display *display, const Msg &msg,
                        const XClientMessageEvent &ev) {
    const std::string type = msg_type_name(display, ev.message_type);
    if (ev.message_type == msg_type_atom(display, MsgType::TMUX_POSITION)) {
        const WindowPosition pos = msg.window_position();
        return std::format(
            "{} window=@{} pane=%{} focused={} zoomed={} dead={} pos=({},{})->({},{})",
            type, msg.tm_window(), msg.tm_pane(), msg.focused(), msg.zoomed(),
            msg.dead(), pos.start.x, pos.start.y, pos.end.x, pos.end.y);
    }
    if (ev.message_type == msg_type_atom(display, MsgType::RESOLUTION)) {
        const Resolution chars = msg.res_chars();
        const Resolution px = msg.res_px();
        return std::format("{} chars={}x{} px={}x{} bar_pos={}", type,
                           chars.x, chars.y, px.x, px.y, msg.bar_pos());
    }
    if (ev.message_type == msg_type_atom(display, MsgType::PREFIX)) {
        return std::format("{} keycode={} mods={}", type, msg.keycode(),
                           msg.mods());
    }
    if (ev.message_type == msg_type_atom(display, MsgType::KILL_PANE)) {
        return std::format("{} pane=%{}", type, msg.tm_pane());
    }
    return type;
}

} // namespace

template <>
void WMInstance::handle_x_event<ConfigureNotify>(XConfigureEvent &ev) {
    xwmux::log_debug(std::format("Handling X event: ConfigureNotify window={} size={}x{}\n",
                                 static_cast<unsigned long>(ev.window), ev.width,
                                 ev.height));
    if (ev.window == m_xstate.root) {
        m_xstate.set_resolution(
            {static_cast<size_t>(ev.width), static_cast<size_t>(ev.height)});
        m_xstate.close_term();
    }
}

template <>
void WMInstance::handle_x_event<ConfigureRequest>(XConfigureRequestEvent &ev) {
    xwmux::log_debug(std::format(
        "Handling X event: ConfigureRequest window={} mask={} pos=({}, {}) size={}x{} border={} above={} detail={}\n",
        static_cast<unsigned long>(ev.window), ev.value_mask, ev.x, ev.y,
        ev.width, ev.height, ev.border_width,
        static_cast<unsigned long>(ev.above), ev.detail));

    if (!m_tmux_mapping.has_window(ev.window)) {
        XWindowChanges changes{};
        changes.x = ev.x;
        changes.y = ev.y;
        changes.width = ev.width;
        changes.height = ev.height;
        changes.border_width = ev.border_width;
        changes.sibling = ev.above;
        changes.stack_mode = ev.detail;
        XConfigureWindow(m_xstate.display, ev.window, ev.value_mask, &changes);
    }
}

template <> void WMInstance::handle_x_event<MapRequest>(XMapRequestEvent &ev) {
    xwmux::log_debug(std::format("Handling X event: MapRequest window={}\n",
                                 static_cast<unsigned long>(ev.window)));

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
            xwmux::log_warn("Window started in iconic state\n");
        }

        m_window_q.push(w);
        m_pending_windows.insert(w);
        m_tmux.split_window();

        // Watch for name changes
        XSelectInput(m_xstate.display, w, PropertyChangeMask);
    }
}

template <> void WMInstance::handle_x_event<UnmapNotify>(XUnmapEvent &ev) {
    xwmux::log_debug(std::format("Handling X event: UnmapNotify window={}\n",
                                 static_cast<unsigned long>(ev.window)));
    if (m_tmux_mapping.has_window(ev.window)) {
        WindowPane &wp = m_tmux_mapping.get(ev.window);
        if (wp.unmap_pending()) {
            wp.notify_unmapped();
        } else {
            m_tmux_mapping.remove_window(m_tmux, ev.window);
            m_xstate.focus_term();
        }
    } else {
        m_pending_windows.erase(ev.window);
    }
}

template <>
void WMInstance::handle_x_event<DestroyNotify>(XDestroyWindowEvent &ev) {
    xwmux::log_debug(std::format("Handling X event: DestroyNotify window={}\n",
                                 static_cast<unsigned long>(ev.window)));
    if (ev.window == m_xstate.term) {
        m_xstate.term = {};
        m_xstate.open_term();
        m_xstate.focus_term();
    } else if (!m_tmux_mapping.has_window(ev.window)) {
        m_pending_windows.erase(ev.window);
        // Not focused yet, do not focus terminal
    } else {
        m_tmux_mapping.remove_window(m_tmux, ev.window);
        m_xstate.focus_term();
    }
}

template <> void WMInstance::handle_x_event<KeyPress>(XEvent &ev) {
    XKeyPressedEvent &k_ev = ev.xkey;
    xwmux::log_debug(std::format("Handling X event: KeyPress window={} keycode={} state={}\n",
                                 static_cast<unsigned long>(k_ev.window),
                                 k_ev.keycode, k_ev.state));
    if (m_xstate.prefix_initialized &&
        k_ev.keycode == m_xstate.prefix.keycode &&
        k_ev.state == m_xstate.prefix.modifiers) {
        // Release the active grab, and generate focus in event
        XUngrabKeyboard(m_xstate.display, CurrentTime);

        // If terminal is focused (prefix active), overriding gui,
        // redirect the event to the gui window
        if (m_tmux_mapping.overridden()) {

            m_tmux.send_escape();

            m_tmux_mapping.release_override(m_xstate);

            // HACK: ensure the focused window has input focus before the keys
            // are sent.
            m_xstate.sync();

            // TODO: Replace this XSendEvent-based passthrough with XTEST so
            // clients like xterm receive input as real keyboard events. For
            // now, kitty is the main supported terminal for the double-prefix
            // passthrough path.
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

        // If gui has focus, activate temporary override and move focus to the
        // terminal so tmux can consume the prefix sequence.
        if (m_tmux_mapping.is_filled()) {
            m_tmux_mapping.override();
            m_xstate.focus_term();
        }

        // Ensure tmux gets focus
        m_xstate.sync();

        m_tmux.send_prefix();
    }
}

template <>
void WMInstance::handle_x_event<PropertyNotify>(XPropertyEvent &ev) {
    xwmux::log_debug(std::format("Handling X event: PropertyNotify window={} atom={}\n",
                                 static_cast<unsigned long>(ev.window),
                                 ev.atom));
    if (m_tmux_mapping.has_window(ev.window) &&
        ev.atom == XInternAtom(m_xstate.display, "WM_NAME", 0)) {
        TmuxPaneID pane = m_tmux_mapping.find(ev.window).second;
        name_client(ev.window, pane);
    }
}

template <>
void WMInstance::handle_client_msg<MsgType::RESOLUTION>(const Msg &msg) {
    xwmux::log_debug(std::format(
        "Handling IPC: RESOLUTION chars={}x{} px={}x{} bar_pos={} layout_initialized_before={}\n",
        msg.res_chars().x, msg.res_chars().y, msg.res_px().x, msg.res_px().y,
        msg.bar_pos(), m_layout_initialized));
    m_xstate.term_layout.set_term_resolution(msg.res_chars(), msg.res_px());
     

    m_xstate.term_layout.set_bar_position(
        static_cast<TmuxBarPosition>(msg.bar_pos()));
    m_layout_initialized = true;
    xwmux::log_debug(std::format(
        "Handling IPC: RESOLUTION complete layout_initialized_after={} screen={}x{}\n",
        m_layout_initialized, m_xstate.resolution.x, m_xstate.resolution.y));
}

template <>
void WMInstance::handle_client_msg<MsgType::PREFIX>(const Msg &msg) {
    xwmux::log_debug(std::format("Handling IPC: PREFIX keycode={} mods={}\n",
                                 msg.keycode(), msg.mods()));
    m_xstate.set_prefix(msg.mod_kc());
}

template <>
void WMInstance::handle_client_msg<MsgType::KILL_PANE>(const Msg &msg) {
    xwmux::log_debug(std::format("Handling IPC: KILL_PANE pane=%{}\n",
                                 msg.tm_pane()));
    if (m_tmux_mapping.is_filled()) {
        Window w = m_tmux_mapping.current_window();
        m_tmux_mapping.kill_client(w, m_xstate.display);
        m_tmux_mapping.remove_window(m_tmux, w);
    }
    // Pane should be killed normally on unmap notify.
}

template <>
void WMInstance::handle_client_msg<MsgType::KILL_ORPHANS>(const Msg &msg) {
    (void)msg;
    xwmux::log_debug("Handling IPC: KILL_ORPHANS\n");
    for (Window w : m_tmux_mapping.find_orphans(m_tmux)) {
        m_tmux_mapping.kill_client(w, m_xstate.display);
        m_tmux_mapping.remove_window(m_tmux, w);
    }
}

template <> void WMInstance::handle_client_msg<MsgType::EXIT>(const Msg &msg) {
    (void)msg;
    xwmux::log_debug("Handling IPC: EXIT\n");
    stop();
}

template <>
void WMInstance::handle_client_msg<MsgType::DUMP_STATE>(const Msg &msg) {
    (void)msg;
    xwmux::log_debug("Handling IPC: DUMP_STATE\n");
    write_state_json();
}

template <>
void WMInstance::handle_client_msg<MsgType::TMUX_POSITION>(const Msg &msg) {
    xwmux::log_debug(std::format(
        "Handling IPC: TMUX_POSITION window=@{} pane=%{} focused={} zoomed={} dead={} pos=({},{})->({},{}) layout_initialized={} screen={}x{}\n",
        msg.tm_window(), msg.tm_pane(), msg.focused(), msg.zoomed(),
        msg.dead(), msg.window_position().start.x, msg.window_position().start.y,
        msg.window_position().end.x, msg.window_position().end.y,
        m_layout_initialized, m_xstate.resolution.x, m_xstate.resolution.y));
    m_tmux_mapping.move_pane(msg.tm_location());

    if (msg.focused()) {

        // Have window to add
        if (!m_window_q.empty() &&
            !m_tmux_mapping.is_filled(msg.tm_location()) && msg.dead()) {

            xwmux::log_debug(std::format(
                "Handling IPC: TMUX_POSITION attaching_pending_window layout_initialized={} pending_queue_size={} target=(@{}, %{})\n",
                m_layout_initialized, m_window_q.size(), msg.tm_window(),
                msg.tm_pane()));

            Window window = m_window_q.front();
            m_window_q.pop();

            // Already destroyed or window already mapped, so kill the pane
            // TODO: avoid WMInstance::adding to queue twice instead?
            if (!m_pending_windows.count(window) ||
                m_tmux_mapping.has_window(window)) {
                m_tmux.kill_pane(msg.tm_location().second);

            } else {
                xwmux::log_debug(std::format(
                    "Handling IPC: TMUX_POSITION add_window window={} target=(@{}, %{})\n",
                    static_cast<unsigned long>(window), msg.tm_window(),
                    msg.tm_pane()));
                if (!m_layout_initialized) {
                    xwmux::log_warn(std::format(
                        "Deferring initial GUI geometry until layout initialization for window={} target=(@{}, %{})\n",
                        static_cast<unsigned long>(window), msg.tm_window(),
                        msg.tm_pane()));
                }
                m_tmux_mapping.add_window(m_xstate, window, msg.tm_location(),
                                          m_layout_initialized);
                m_pending_windows.erase(window);
                name_client(window, msg.tm_location().second);
            }
        }

        const bool same_active_location =
            m_tmux_mapping.get_active().has_value() &&
            m_tmux_mapping.get_active().value() == msg.tm_location();
        const bool redundant_refocus =
            msg.zoomed() && same_active_location &&
            m_tmux_mapping.is_filled(msg.tm_location());

        if (m_tmux_mapping.overridden()) {
            const bool prefix_active = m_tmux.client_prefix_active();
            xwmux::log_debug(std::format(
                "Handling IPC: TMUX_POSITION override_state prefix_active={} same_active_location={} redundant_refocus={}\n",
                prefix_active, same_active_location, redundant_refocus));

            if (prefix_active) {
                m_tmux_mapping.set_active_without_focus(
                    m_xstate, msg.tm_location(), msg.zoomed());
            } else {
                m_tmux_mapping.clear_override();
                m_tmux_mapping.set_active(m_xstate, msg.tm_location(),
                                          msg.zoomed(), true);
            }
        } else {
            m_tmux_mapping.set_active(m_xstate, msg.tm_location(),
                                      msg.zoomed(), redundant_refocus);
        }
    }

    // set_position(msg.tm_location(), msg.window_position());
    // Moves the (possible window) at location to term_position
    // If pane doesn't have a window, do nothing
    // If pane is in seperate window, moves the pane
    if (!m_layout_initialized) {
        xwmux::log_warn(std::format(
            "Skipping pane geometry update before layout initialization for target=(@{}, %{})\n",
            msg.tm_window(), msg.tm_pane()));
        return;
    }

    WindowPosition gui_position = m_xstate.term_layout.term_to_screen_pos(
        m_xstate.term_layout.add_bar(msg.window_position()));

    if (m_tmux_mapping.is_filled(msg.tm_location())) {
        m_tmux_mapping[msg.tm_location()].set_position(m_xstate, gui_position);
    }
}

template <>
void WMInstance::handle_x_event<ClientMessage>(XClientMessageEvent &ev) {
    const Msg msg{ev};
    xwmux::log_debug(std::format("Handling X event: {} {}\n",
                                 x_event_name(ClientMessage),
                                 msg_summary(m_xstate.display, msg, ev)));
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
    } else if (ev.message_type ==
               msg_type_atom(m_xstate.display, MsgType::DUMP_STATE)) {
        handle_client_msg<MsgType::DUMP_STATE>(msg);
    }
}

void WMInstance::handle_event(XEvent &ev) {
    xwmux::log_debug(std::format("Dispatching X event: {}\n", x_event_name(ev.type)));
    switch (ev.type) {
    case ConfigureNotify:
        handle_x_event<ConfigureNotify>(ev.xconfigure);
        break;
    case ConfigureRequest:
        handle_x_event<ConfigureRequest>(ev.xconfigurerequest);
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
