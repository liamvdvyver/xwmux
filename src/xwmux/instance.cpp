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
    if (atom == msg_type_atom(display, MsgType::SYNC)) {
        return "SYNC";
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
    if (ev.message_type == msg_type_atom(display, MsgType::SYNC)) {
        return std::format("{} sequence={}", type, msg.sync_sequence());
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

void WMInstance::remove_orphaned_windows(const TmuxSnapshot &snapshot) {
    std::unordered_set<TmuxPaneID> live_panes;
    for (const TmuxPaneSnapshot &pane : snapshot.panes) {
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
        request_window_close(orphan);
        m_tmux_mapping.remove_window(m_tmux, orphan, false);
    }
}

void WMInstance::apply_snapshot_panes(const TmuxSnapshot &snapshot) {
    // Phase 2a: update mapping membership before visibility/focus decisions.
    move_snapshot_panes(snapshot);

    // Phase 2b: attachment and focus are defined only for the focused pane.
    if (const TmuxPaneSnapshot *pane = focused_snapshot_pane(snapshot)) {
        apply_focused_snapshot_pane(*pane);
    }

    // Phase 2c: apply geometry after the mapping is fully reconciled.
    apply_snapshot_geometry(snapshot);
}

void WMInstance::refresh_from_tmux_snapshot() {
    xwmux::log_debug("Refreshing WM state from tmux snapshot\n");
    const std::optional<TmuxSnapshot> snapshot = m_tmux.snapshot();
    if (!snapshot.has_value()) {
        xwmux::log_warn("Skipping refresh: failed to read tmux snapshot\n");
        return;
    }

    // Phase 1: prune mappings for panes that no longer exist in tmux.
    remove_orphaned_windows(snapshot.value());

    // Phase 2: apply pane-driven mapping/focus/geometry updates.
    apply_snapshot_panes(snapshot.value());
}

void WMInstance::acknowledge_sync(const long sequence) {
    const unsigned long acknowledged = static_cast<unsigned long>(sequence);
    XChangeProperty(m_xstate.display, m_xstate.root,
                    sync_sequence_atom(m_xstate.display), XA_CARDINAL, 32,
                    PropModeReplace,
                    reinterpret_cast<const unsigned char *>(&acknowledged), 1);
    xwmux::log_debug(
        std::format("Acknowledged sync sequence={}\n", sequence));
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
            const TmuxLocation active_location =
                m_tmux_mapping.get_active().value();
            apply_active_workspace_visibility(active_location, false);
            focus_active_pane(active_location, true);
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
        // Keep the synchronous grab until the captured prefix key is replayed
        // to its new focus target below.

        // If terminal is focused (prefix active), overriding gui,
        // redirect the event to the gui window
        if (m_tmux_mapping.overridden()) {

            m_tmux.send_escape();

            m_tmux_mapping.clear_override();
            focus_active_pane(m_tmux_mapping.get_active().value(), true);

            // Focus is processed before replaying the originally captured key
            // to the GUI client.
            m_xstate.sync();
            XAllowEvents(m_xstate.display, ReplayKeyboard, CurrentTime);
            m_xstate.sync();
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

        // Focus is processed before replaying the originally captured key.
        m_xstate.sync();
        XAllowEvents(m_xstate.display, ReplayKeyboard, CurrentTime);
        m_xstate.sync();
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
    request_window_close(w);
    m_tmux_mapping.remove_window(m_tmux, w);
    // Pane should be killed normally on unmap notify.
}

template <>
void WMInstance::handle_client_msg<MsgType::KILL_ORPHANS>(const Msg &msg) {
    (void)msg;
    xwmux::log_debug("Handling IPC: KILL_ORPHANS\n");
    for (Window w : m_tmux_mapping.find_orphans(m_tmux)) {
        request_window_close(w);
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
void WMInstance::handle_client_msg<MsgType::SYNC>(const Msg &msg) {
    xwmux::log_debug(
        std::format("Handling IPC: SYNC sequence={}\n", msg.sync_sequence()));
    refresh_from_tmux_snapshot();
    acknowledge_sync(msg.sync_sequence());
}

template <>
void WMInstance::handle_client_msg<MsgType::DUMP_STATE>(const Msg &msg) {
    (void)msg;
    xwmux::log_debug("Handling IPC: DUMP_STATE\n");
    write_state_json();
}

std::optional<Window> WMInstance::take_pending_window() {
    if (m_window_q.empty()) {
        return std::nullopt;
    }

    const Window window = m_window_q.front();
    m_window_q.pop();
    return window;
}

bool WMInstance::can_attach_pending_window(const Window window) const {
    return m_pending_windows.contains(window) &&
           !m_tmux_mapping.has_window(window);
}

void WMInstance::attach_window_to_pane(const Window window,
                                       const TmuxPaneSnapshot &pane) {
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

    m_tmux_mapping.add_window(window, pane.location);
    if (m_layout_initialized) {
        initialize_gui_geometry(window);
    }
    m_pending_windows.erase(window);
    name_client(window, pane.location.second);
}

void WMInstance::maybe_attach_pending_window(const TmuxPaneSnapshot &pane) {
    if (!should_attach_pending_window(pane)) {
        return;
    }

    xwmux::log_debug(std::format(
        "Refresh attaching_pending_window layout_initialized={} pending_queue_size={} target=(@{}, %{})\n",
        m_layout_initialized, m_window_q.size(), pane.location.first,
        pane.location.second));

    const std::optional<Window> window = take_pending_window();
    if (!window.has_value()) {
        return;
    }

    // Already destroyed or window already mapped, so kill the pane.
    // TODO: avoid WMInstance::adding to queue twice instead?
    if (!can_attach_pending_window(window.value())) {
        m_tmux.kill_pane(pane.location.second);
        return;
    }

    attach_window_to_pane(window.value(), pane);
}

bool WMInstance::is_same_active_location(const TmuxPaneSnapshot &pane) const {
    return m_tmux_mapping.get_active().has_value() &&
           m_tmux_mapping.get_active().value() == pane.location;
}

bool WMInstance::pane_has_gui_window(const TmuxPaneSnapshot &pane) const {
    return m_tmux_mapping.is_filled(pane.location);
}

bool WMInstance::should_attach_pending_window(
    const TmuxPaneSnapshot &pane) const {
    return !m_window_q.empty() && !pane_has_gui_window(pane) &&
           pane.pane_pid == 0;
}

bool WMInstance::needs_redundant_refocus(
    const TmuxPaneSnapshot &pane, const bool same_active_location) const {
    return (pane.zoomed && same_active_location &&
            pane_has_gui_window(pane)) ||
           (m_force_active_refocus && same_active_location);
}

void WMInstance::maybe_clear_forced_refocus(const TmuxPaneSnapshot &pane,
                                            const bool same_active_location) {
    if (same_active_location || m_tmux_mapping.get_active().value() == pane.location) {
        m_force_active_refocus = false;
    }
}

void WMInstance::show_window(WindowPane &pane) {
    XMapWindow(m_xstate.display, pane.get_window());
    pane.mark_shown();
}

void WMInstance::hide_window(WindowPane &pane) {
    XUnmapWindow(m_xstate.display, pane.get_window());
    pane.mark_hidden();
}

void WMInstance::request_window_close(const Window window) {
    if (!m_tmux_mapping.begin_close(window)) {
        return;
    }

    XEvent ev{};
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

void WMInstance::initialize_gui_geometry(const Window window) {
    m_xstate.term_layout.fullscreen_term_position().resize_to(m_xstate.display,
                                                              window);
}

void WMInstance::apply_active_workspace_visibility(
    const TmuxLocation &location, const bool zoomed) {
    const std::optional<TmuxLocation> active_location =
        m_tmux_mapping.get_active();
    xwmux::log_debug(std::format(
        "Applying active workspace visibility target_window=@{} previous_active=({}, {}) zoomed_pane={}\n",
        location.first,
        active_location.has_value() ? active_location->first : -1,
        active_location.has_value() ? active_location->second : -1,
        zoomed ? std::to_string(location.second) : std::string("none")));

    if (active_location.has_value() &&
        active_location->first != location.first) {
        for (auto &[pane_id, window_pane] :
             m_tmux_mapping.workspace(active_location->first).get_windows()) {
            (void)pane_id;
            hide_window(window_pane);
        }
    }

    for (auto &[pane_id, window_pane] :
         m_tmux_mapping.workspace(location.first).get_windows()) {
        if (zoomed && pane_id != location.second) {
            hide_window(window_pane);
        } else {
            show_window(window_pane);
        }
    }
    m_tmux_mapping.set_active_window(location.first);
}

void WMInstance::focus_active_pane(const TmuxLocation &location,
                                   const bool redundant_refocus) {
    const std::optional<TmuxLocation> active_location =
        m_tmux_mapping.get_active();
    xwmux::log_debug(std::format(
        "Focusing active pane target=(@{}, %{}) current=({}, {}) redundant_refocus={}\n",
        location.first, location.second,
        active_location.has_value() ? active_location->first : -1,
        active_location.has_value() ? active_location->second : -1,
        redundant_refocus));

    if (!active_location.has_value() || active_location.value() != location ||
        redundant_refocus) {
        const bool has_gui_window = m_tmux_mapping.is_filled(location);
        const Window target =
            has_gui_window ? m_tmux_mapping[location].get_window()
                           : m_xstate.term.value_or(m_xstate.root);

        xwmux::log_debug(std::format(
            "Focusing active pane applying_focus has_gui_window={} target={}\n",
            has_gui_window, static_cast<unsigned long>(target)));
        if (has_gui_window) {
            m_xstate.grab_prefix();
        } else {
            m_xstate.ungrab_prefix();
        }
        XSetInputFocus(m_xstate.display, target, 0, 0);
        m_tmux_mapping.set_active_pane(location.second);
    } else {
        xwmux::log_debug(
            "Focusing active pane skipping focus; location unchanged\n");
    }
}

void WMInstance::apply_active_pane_with_focus(const TmuxPaneSnapshot &pane,
                                              const bool redundant_refocus) {
    apply_active_workspace_visibility(pane.location, pane.zoomed);
    focus_active_pane(pane.location, redundant_refocus);
}

void WMInstance::apply_active_pane_without_focus(const TmuxPaneSnapshot &pane) {
    apply_active_workspace_visibility(pane.location, pane.zoomed);
    m_tmux_mapping.set_active_pane(pane.location.second);
}

void WMInstance::update_overridden_active_focus(
    const TmuxPaneSnapshot &pane, const bool same_active_location,
    const bool redundant_refocus) {
    const bool prefix_active = m_tmux.client_prefix_active();
    xwmux::log_debug(std::format(
        "Refresh override_state prefix_active={} same_active_location={} redundant_refocus={}\n",
        prefix_active, same_active_location, redundant_refocus));

    if (prefix_active) {
        apply_active_pane_without_focus(pane);
        return;
    }

    m_tmux_mapping.clear_override();
    apply_active_pane_with_focus(pane, true);
    m_force_active_refocus = false;
}

void WMInstance::update_active_focus_from_pane(const TmuxPaneSnapshot &pane) {
    const bool same_active_location = is_same_active_location(pane);
    const bool redundant_refocus =
        needs_redundant_refocus(pane, same_active_location);

    if (m_tmux_mapping.overridden()) {
        update_overridden_active_focus(pane, same_active_location,
                                       redundant_refocus);
        return;
    }

    apply_active_pane_with_focus(pane, redundant_refocus);
    maybe_clear_forced_refocus(pane, same_active_location);
}

std::optional<WindowPosition>
WMInstance::desired_gui_position(const TmuxPaneSnapshot &pane) const {
    if (!m_layout_initialized) {
        xwmux::log_warn(std::format(
            "Skipping pane geometry update before layout initialization for target=(@{}, %{})\n",
            pane.location.first, pane.location.second));
        return std::nullopt;
    }

    if (!m_tmux_mapping.is_filled(pane.location)) {
        return std::nullopt;
    }

    return m_xstate.term_layout.term_to_screen_pos(
        m_xstate.term_layout.add_bar(pane.position));
}

void WMInstance::apply_pane_geometry(const TmuxPaneSnapshot &pane) {
    const std::optional<WindowPosition> gui_position = desired_gui_position(pane);
    if (!gui_position.has_value()) {
        return;
    }

    gui_position.value().resize_to(m_xstate.display,
                                   m_tmux_mapping[pane.location].get_window());
}

void WMInstance::move_snapshot_panes(const TmuxSnapshot &snapshot) {
    for (const TmuxPaneSnapshot &pane : snapshot.panes) {
        xwmux::log_debug(std::format(
            "Refresh moving pane window=@{} pane=%{}\n", pane.location.first,
            pane.location.second));
        m_tmux_mapping.move_pane(pane.location);
    }
}

const TmuxPaneSnapshot *
WMInstance::focused_snapshot_pane(const TmuxSnapshot &snapshot) const {
    for (const TmuxPaneSnapshot &pane : snapshot.panes) {
        if (pane.focused) {
            return &pane;
        }
    }
    return nullptr;
}

void WMInstance::apply_focused_snapshot_pane(const TmuxPaneSnapshot &pane) {
    xwmux::log_debug(std::format(
        "Refresh applying focused pane window=@{} pane=%{} zoomed={} pane_pid={}\n",
        pane.location.first, pane.location.second, pane.zoomed, pane.pane_pid));
    maybe_attach_pending_window(pane);
    update_active_focus_from_pane(pane);
}

void WMInstance::apply_snapshot_geometry(const TmuxSnapshot &snapshot) {
    for (const TmuxPaneSnapshot &pane : snapshot.panes) {
        xwmux::log_debug(std::format(
            "Refresh applying geometry window=@{} pane=%{} pos=({},{})->({},{}) layout_initialized={} screen={}x{}\n",
            pane.location.first, pane.location.second, pane.position.start.x,
            pane.position.start.y, pane.position.end.x, pane.position.end.y,
            m_layout_initialized, m_xstate.resolution.x,
            m_xstate.resolution.y));
        apply_pane_geometry(pane);
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
               msg_type_atom(m_xstate.display, MsgType::REFRESH)) {
        handle_client_msg<MsgType::REFRESH>(msg);
    } else if (ev.message_type ==
               msg_type_atom(m_xstate.display, MsgType::SYNC)) {
        handle_client_msg<MsgType::SYNC>(msg);
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
