#pragma once

#include <cstdint>
#include <layout.h>

#include "tmux.h"

#define SOCK_PATH "/tmp/xwmux.sock"

enum class MsgType { RESOLUTION, EXIT, TMUX_NOTIFY, KILL_PANE };

struct TmuxPosition {
    uint32_t session_id;
    TmuxLocation location;
};


union MsgBody {
    int null_msg;
    Resolution resolution;
    TmuxLocation focus_location;
    TmuxPaneID kill_pane;
};

struct Msg {

    Msg(MsgType type) : type(type) {};
    Msg(Resolution res) : type(MsgType::RESOLUTION) { msg.resolution = res; };

    Msg(TmuxLocation focus_location) : type(MsgType::TMUX_NOTIFY) {
        msg.focus_location = focus_location;
    };

    Msg(TmuxPaneID tmux_pane) : type(MsgType::KILL_PANE) {
        msg.kill_pane = tmux_pane;
    };

    MsgType type;
    MsgBody msg{};
};

enum class Resp : uint8_t { OK, ERR };
