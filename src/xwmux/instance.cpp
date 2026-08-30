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
    if (atom == msg_type_atom(display, MsgType::REFRESH)) {
        return "REFRESH";
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

bool WMInstance::is_refresh_event(const XEvent &ev) const {
    return ev.type == ClientMessage &&
           ev.xclient.message_type == msg_type_atom(m_xstate.display, MsgType::REFRESH);
}

void WMInstance::consume_adjacent_refresh_events() {
    size_t coalesced = 1;
    while (XPending(m_xstate.display) > 0) {
        XEvent next;
        XPeekEvent(m_xstate.display, &next);
        if (!is_refresh_event(next)) {
            break;
        }
        XNextEvent(m_xstate.display, &next);
        coalesced++;
    }
    xwmux::log_debug(std::format(
        "Coalesced refresh burst size={}\n", coalesced));
}

void WMInstance::refresh_from_tmux_snapshot() {
    xwmux::log_debug("Refreshing WM state from tmux snapshot\n");
    const std::optional<TmuxSnapshot> snapshot = m_tmux.snapshot();
    if (!snapshot.has_value()) {
        xwmux::log_warn("Skipping refresh: failed to read tmux snapshot\n");
        return;
    }

    std::unordered_set<TmuxPaneID> live_panes;
    for (const TmuxPaneSnapshot &pane : snapshot->panes) {
        live_panes.insert(pane.location.second);
    }

    std::vector<Window> orphan_windows;
    for (const auto &[tm_window, workspace] : m_tmux_mapping.get_workspaces()) {
        (void)tm_window;
        for (const auto &[tm_pane, window_pane] : workspace.get_windows()) {
            if (!live_panes.contains(tm_pane)) {
                orphan_windows.push_back(window_pane.get_window());
            }
        }
    }
    for (const Window orphan : orphan_windows) {
        xwmux::log_debug(std::format(
            "Refresh removing orphaned window={}\n",
            static_cast<unsigned long>(orphan)));
        m_tmux_mapping.kill_client(orphan, m_xstate.display);
        m_tmux_mapping.remove_window(m_tmux, orphan, false);
    }

    for (const TmuxPaneSnapshot &pane : snapshot->panes) {
        apply_tmux_pane_snapshot(pane);
    }
}

template <>
void WMInstance::handle_x_event<ConfigureNotify>(XConfigureEvent &ev) {
    xwmux::log_debug(std::format("Handling X event: ConfigureNotify window={} size={}x{}\n",
                                 static_cast<unsigned long>(ev.window), ev.width,
                                 ev.height));
    if (ev.window == m_xstate.root) {
        m_xstate.resolution = {static_cast<size_t>(ev.width),
                               static_cast<size_t>(ev.height)};
        m_xstate.term_layout.set_screen_resolution(m_xstate.resolution);

        if (m_xstate.term.has_value() && !m_term_restart_pending) {
            m_term_restart_pending = true;
            m_xstate.close_term();
        }
    }
}

template <>
void WMInstance::handle_x_event<ConfigureRequest>(XConfigureRequestEvent &ev) {
    xwmux::log_debug(std::format(
        "Handling X event: ConfigureRequest window={} mask={} pos=({}, {}) size={}x{} border={} above={} detail={}\n",
        static_cast<unsigned long>(ev.window), ev.value_mask, ev.x, ev.y,
        ev.width, ev.height, ev.border_width,
        static_cast<unsigned long>(ev.above), ev.detail));

    // Ignore reqests for managed windows or visible root terminal
    if (!m_tmux_mapping.has_window(ev.window) && !(m_xstate.term.has_value() && ev.window == m_xstate.term.value())) {
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
        m_xstate.set_term(w);
        m_xstate.resolution.fullscreen().resize_to(m_xstate.display, w);
        XLowerWindow(m_xstate.display, w);
        XMapWindow(m_xstate.display, w);

        if (m_tmux_mapping.get_active().has_value() &&
            m_tmux_mapping.is_filled(m_tmux_mapping.get_active().value())) {
            m_force_active_refocus = true;
            m_tmux_mapping.set_active(m_xstate,
                                      m_tmux_mapping.get_active().value(),
                                      false, true);
        } else {
            m_force_active_refocus = false;
            m_xstate.focus_term();
        }
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
    if (m_xstate.term.has_value() && ev.window == m_xstate.term.value()) {
        m_xstate.term = {};
        m_term_restart_pending = false;
        m_xstate.open_term();
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
    xwmux::log_debug(
        std::format("Handling IPC: KILL_PANE pane=%{}\n", msg.tm_pane()));
    Window w{};
    if (msg.tm_pane() >= 0) {
        if (!m_tmux_mapping.has_pane(msg.tm_pane())) {
            xwmux::log_warn(std::format("KILL_PANE: no window for pane: %{}\n",
                                        msg.tm_pane()));
            return;
        }
        w = m_tmux_mapping.window_for_pane(msg.tm_pane()).get_window();
    } else {
        if (!m_tmux_mapping.is_filled()) {
            xwmux::log_warn(
                std::format("KILL_PANE: no window for active pane\n"));
            return;
        }
        w = m_tmux_mapping.current_window();
    }
    m_tmux_mapping.kill_client(w, m_xstate.display);
    m_tmux_mapping.remove_window(m_tmux, w);
    // Pane should be killed normally on unmap notify.
}

template <>
void WMInstance::handle_client_msg<MsgType::KILL_ORPHANS>(const Msg &msg) {
    (void)msg;
    xwmux::log_debug("Handling IPC: KILL_ORPHANS\n");
    for (Window w : m_tmux_mapping.find_orphans(m_tmux)) {
        m_tmux_mapping.kill_client(w, m_xstate.display);
        m_tmux_mapping.remove_window(m_tmux, w, false);
    }
}

template <> void WMInstance::handle_client_msg<MsgType::EXIT>(const Msg &msg) {
    (void)msg;
    xwmux::log_debug("Handling IPC: EXIT\n");
    stop();
}

template <>
void WMInstance::handle_client_msg<MsgType::REFRESH>(const Msg &msg) {
    (void)msg;
    xwmux::log_debug("Handling IPC: REFRESH (unexpected direct dispatch)\n");
    refresh_from_tmux_snapshot();
}

template <>
void WMInstance::handle_client_msg<MsgType::DUMP_STATE>(const Msg &msg) {
    (void)msg;
    xwmux::log_debug("Handling IPC: DUMP_STATE\n");
    write_state_json();
}

void WMInstance::maybe_attach_pending_window(const TmuxPaneSnapshot &pane) {
    if (!m_window_q.empty() && !m_tmux_mapping.is_filled(pane.location) &&
        pane.dead) {

        xwmux::log_debug(std::format(
            "Refresh attaching_pending_window layout_initialized={} pending_queue_size={} target=(@{}, %{})\n",
            m_layout_initialized, m_window_q.size(), pane.location.first,
            pane.location.second));

        Window window = m_window_q.front();
        m_window_q.pop();

        // Already destroyed or window already mapped, so kill the pane
        // TODO: avoid WMInstance::adding to queue twice instead?
        if (!m_pending_windows.count(window) || m_tmux_mapping.has_window(window)) {
            m_tmux.kill_pane(pane.location.second);

        } else {
            xwmux::log_debug(std::format(
                "Refresh add_window window={} target=(@{}, %{})\n",
                static_cast<unsigned long>(window), pane.location.first,
                pane.location.second));
            if (!m_layout_initialized) {
                xwmux::log_warn(std::format(
                    "Deferring initial GUI geometry until layout initialization for window={} target=(@{}, %{})\n",
                    static_cast<unsigned long>(window), pane.location.first,
                    pane.location.second));
            }
            m_tmux_mapping.add_window(m_xstate, window, pane.location,
                                      m_layout_initialized);
            m_pending_windows.erase(window);
            name_client(window, pane.location.second);
        }
    }
}

void WMInstance::update_active_focus_from_pane(const TmuxPaneSnapshot &pane) {
    const bool same_active_location =
        m_tmux_mapping.get_active().has_value() &&
        m_tmux_mapping.get_active().value() == pane.location;
    const bool redundant_refocus =
        (pane.zoomed && same_active_location &&
         m_tmux_mapping.is_filled(pane.location)) ||
        (m_force_active_refocus && same_active_location);

    if (m_tmux_mapping.overridden()) {
        const bool prefix_active = m_tmux.client_prefix_active();
        xwmux::log_debug(std::format(
            "Refresh override_state prefix_active={} same_active_location={} redundant_refocus={}\n",
            prefix_active, same_active_location, redundant_refocus));

        if (prefix_active) {
            m_tmux_mapping.set_active_without_focus(m_xstate, pane.location,
                                                    pane.zoomed);
        } else {
            m_tmux_mapping.clear_override();
            m_tmux_mapping.set_active(m_xstate, pane.location, pane.zoomed,
                                      true);
            m_force_active_refocus = false;
        }
    } else {
        m_tmux_mapping.set_active(m_xstate, pane.location, pane.zoomed,
                                  redundant_refocus);
        if (same_active_location ||
            m_tmux_mapping.get_active().value() == pane.location) {
            m_force_active_refocus = false;
        }
    }
}

void WMInstance::apply_pane_geometry(const TmuxPaneSnapshot &pane) {
    if (!m_layout_initialized) {
        xwmux::log_warn(std::format(
            "Skipping pane geometry update before layout initialization for target=(@{}, %{})\n",
            pane.location.first, pane.location.second));
        return;
    }

    WindowPosition gui_position = m_xstate.term_layout.term_to_screen_pos(
        m_xstate.term_layout.add_bar(pane.position));

    if (m_tmux_mapping.is_filled(pane.location)) {
        m_tmux_mapping[pane.location].set_position(m_xstate, gui_position);
    }
}

void WMInstance::apply_tmux_pane_snapshot(const TmuxPaneSnapshot &pane) {
    xwmux::log_debug(std::format(
        "Applying tmux pane snapshot window=@{} pane=%{} focused={} zoomed={} dead={} pos=({},{})->({},{}) layout_initialized={} screen={}x{}\n",
        pane.location.first, pane.location.second, pane.focused, pane.zoomed,
        pane.dead, pane.position.start.x, pane.position.start.y,
        pane.position.end.x, pane.position.end.y, m_layout_initialized,
        m_xstate.resolution.x, m_xstate.resolution.y));
    m_tmux_mapping.move_pane(pane.location);

    if (pane.focused) {
        maybe_attach_pending_window(pane);
        update_active_focus_from_pane(pane);
    }

    apply_pane_geometry(pane);
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
               msg_type_atom(m_xstate.display, MsgType::REFRESH)) {
        handle_client_msg<MsgType::REFRESH>(msg);
    } else if (ev.message_type ==
               msg_type_atom(m_xstate.display, MsgType::KILL_PANE)) {
        handle_client_msg<MsgType::KILL_PANE>(msg);
    } else if (ev.message_type ==
               msg_type_atom(m_xstate.display, MsgType::KILL_ORPHANS)) {
        handle_client_msg<MsgType::KILL_ORPHANS>(msg);
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
