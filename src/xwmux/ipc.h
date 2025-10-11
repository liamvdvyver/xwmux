#pragma once

#include <cstdint>
#include <layout.h>

#include "tmux.h"

#define SOCK_PATH "/tmp/xwmux.sock"

enum class MsgType { RESOLUTION, EXIT, TMUX_NOTIFY, TMUX_POSITION, KILL_PANE };

struct TmuxPanePosition {
    TmuxLocation location;
    WindowPosition position;
};

union MsgBody {
    int null_msg;
    Resolution resolution;
    TmuxLocation focus_location;
    TmuxPanePosition pane_position;
    TmuxPaneID kill_pane;
};

struct Msg {

    Msg(MsgType type) : type(type) {};
    Msg(Resolution res) : type(MsgType::RESOLUTION) { msg.resolution = res; };

    Msg(TmuxLocation focus_location) : type(MsgType::TMUX_NOTIFY) {
        msg.focus_location = focus_location;
    };

    Msg(TmuxPanePosition pane_position) : type(MsgType::TMUX_POSITION) {
        msg.pane_position = pane_position;
    };

    Msg(TmuxPaneID tmux_pane) : type(MsgType::KILL_PANE) {
        msg.kill_pane = tmux_pane;
    };

    MsgType type;
    MsgBody msg{};
};

enum class Resp : uint8_t { OK, ERR };
