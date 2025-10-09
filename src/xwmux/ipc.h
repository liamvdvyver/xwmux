#ifndef IPC_H
#define IPC_H

#include <cstdint>
#include <layout.h>

#include "tmux.h"

#define SOCK_PATH "/tmp/xwmux.sock"

enum class TmuxEventType {
    FOCUS_PANE,
    FOCUS_WINDOW,
    FOCUS_SESSION,
    NEW_PANE,
    NEW_WINDOW,
    NEW_SESSION,
    DESTROY_PANE,
    DESTROY_WINDOW,
    DESTROY_SESSION,
    RESIZE_PANE, // any layout changes to current window, re-calculate window
                 // layout
    SWAP_PANE,
    SWAP_WINDOW,
};

struct TmuxEvent {
    TmuxEventType type;
    uint32_t session_id;
    TmuxLocation location;
};

enum class MsgType { RESOLUTION, EXIT, TMUX_NOTIFY, KILL_PANE };

union MsgBody {
    int null_msg;
    Resolution resolution;
    TmuxEvent tmux_event;
    TmuxPaneID kill_pane;
};

struct Msg {

    Msg(MsgType type) : type(type) {};
    Msg(Resolution res) : type(MsgType::RESOLUTION) { msg.resolution = res; };

    Msg(TmuxEvent tmux_ev) : type(MsgType::TMUX_NOTIFY) {
        msg.tmux_event = tmux_ev;
    };

    Msg(TmuxPaneID tmux_pane) : type(MsgType::KILL_PANE) {
        msg.kill_pane = tmux_pane;
    };

    MsgType type;
    MsgBody msg{};
};

enum class Resp : uint8_t { OK, ERR };

#endif
