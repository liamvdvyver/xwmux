#pragma once

#include "layout.h"
#include "tmux.h"
#include "xwrapper.h"

#include <X11/Xlib.h>
#include <cassert>
#include <utility>

#define SOCK_PATH "/tmp/xwmux.sock"

enum class MsgType { RESOLUTION, PREFIX, EXIT, TMUX_POSITION, KILL_PANE };

constexpr Atom msg_type_atom(Display *dpy, const MsgType type) {
    switch (type) {
    case MsgType::RESOLUTION:
        return XInternAtom(dpy, "_XW_RESOUTION", 0);
    case MsgType::PREFIX:
        return XInternAtom(dpy, "_XW_PREFIX", 0);
    case MsgType::EXIT:
        return XInternAtom(dpy, "_XW_EXIT", 0);
    case MsgType::TMUX_POSITION:
        return XInternAtom(dpy, "_XW_TMUX_POSITION", 0);
    case MsgType::KILL_PANE:
        return XInternAtom(dpy, "_XW_KILL_PANE", 0);
    }
    std::unreachable();
};

struct Msg {

    constexpr Msg(const XClientMessageEvent &ev) : m_ev({.xclient = ev}) { assert(ev.type = ClientMessage); }

    // Static factory methods

    constexpr static Msg report_resolution(Display *const dpy,
                                           const Resolution res_chars,
                                           const Resolution res_px,
                                           TmuxBarPosition const bar_position) {
        Msg ret(dpy, MsgType::RESOLUTION);
        ret.cols() = res_chars.width;
        ret.rows() = res_chars.height;
        ret.px_w() = res_px.width;
        ret.px_h() = res_px.height;
        ret.bar_pos() = static_cast<long>(bar_position);
        return ret;
    }

    constexpr static Msg report_prefix(Display *const dpy,
                                       ModifiedKeyCode const prefix) {
        Msg ret(dpy, MsgType::PREFIX);
        ret.keycode() = prefix.keycode;
        ret.mods() = prefix.modifiers;
        return ret;
    }

    constexpr static Msg report_position(Display *const dpy,
                                         const TmuxLocation location,
                                         const WindowPosition position,
                                         const bool focused,
                                         const bool zoomed) {
        Msg ret(dpy, MsgType::TMUX_POSITION);
        ret.position_start() = position.start.pack();
        ret.position_end() = position.end.pack();
        ret.tm_window() = location.first;
        ret.tm_pane() = location.second;
        ret.zoom_focus() = (focused | (zoomed << 1));
        return ret;
    }

    constexpr static Msg kill_pane(Display *const dpy,
                                   const TmuxPaneID tm_pane) {
        Msg ret(dpy, MsgType::KILL_PANE);
        ret.tm_pane() = tm_pane;
        return ret;
    }

    constexpr static Msg exit(Display *const dpy) {
        return Msg(dpy, MsgType::EXIT);
    }

    // Layout definition/accessors

    // Pane positions
    long &position_start() { return m_ev.xclient.data.l[0]; }
    const long &position_start() const { return m_ev.xclient.data.l[0]; }
    long &position_end() { return m_ev.xclient.data.l[1]; }
    const long &position_end() const { return m_ev.xclient.data.l[1]; }
    long &tm_window() { return m_ev.xclient.data.l[2]; }
    const long &tm_window() const { return m_ev.xclient.data.l[2]; }
    long &tm_pane() { return m_ev.xclient.data.l[3]; }
    const long &tm_pane() const { return m_ev.xclient.data.l[3]; }
    long &zoom_focus() { return m_ev.xclient.data.l[4]; }
    const long &zoom_focus() const { return m_ev.xclient.data.l[4]; }

    // Resolution
    long &cols() { return m_ev.xclient.data.l[0]; }
    const long &cols() const { return m_ev.xclient.data.l[0]; }
    long &rows() { return m_ev.xclient.data.l[1]; }
    const long &rows() const { return m_ev.xclient.data.l[1]; }
    long &px_w() { return m_ev.xclient.data.l[2]; }
    const long &px_w() const { return m_ev.xclient.data.l[2]; }
    long &px_h() { return m_ev.xclient.data.l[3]; }
    const long &px_h() const { return m_ev.xclient.data.l[3]; }

    long &bar_pos() { return m_ev.xclient.data.l[4]; }
    const long &bar_pos() const { return m_ev.xclient.data.l[4]; }

    // Prefix
    long &keycode() { return m_ev.xclient.data.l[0]; }
    const long &keycode() const { return m_ev.xclient.data.l[0]; }
    long &mods() { return m_ev.xclient.data.l[1]; }
    const long &mods() const { return m_ev.xclient.data.l[1]; }

    // Other accessors

    bool focused() const { return zoom_focus() & 0b1; }
    bool zoomed() const { return zoom_focus() & 0b10; }

    WindowPosition window_position() const {
        return {.start = {Point::unpack(position_start())},
                .end = {Point::unpack(static_cast<size_t>(position_end()))}};
    }

    TmuxLocation tm_location() const { return {tm_window(), tm_pane()}; }

    Resolution res_chars() const { return Resolution(cols(), rows()); }

    Resolution res_px() const { return Resolution(px_w(), px_h()); }

    ModifiedKeyCode mod_kc() const { return ModifiedKeyCode(keycode(), mods()); }

    XEvent &get_event() { return m_ev; }
    const XEvent &get_event() const { return m_ev; }

  private:
    Msg(Display *const dpy, const MsgType type) {
        m_ev.type = ClientMessage;
        m_ev.xclient.type = ClientMessage;
        m_ev.xclient.message_type = msg_type_atom(dpy, type);
        m_ev.xclient.display = dpy;
        m_ev.xclient.format = 32;
    }

    XEvent m_ev;
};
