#pragma once

#include <cstdint>
#include <layout.h>

#include "tmux.h"

#define SOCK_PATH "/tmp/xwmux.sock"

enum class MsgType { RESOLUTION, EXIT, TMUX_POSITION, KILL_PANE };

struct TermInitLayout {
    Resolution term_resolution;
    TmuxBarPosition bar_position;
};

struct TmuxPanePosition {
    TmuxLocation location;
    WindowPosition position;
    bool focused = false;
};

union MsgBody {
    int null_msg;
    TermInitLayout term_init_layout;
    TmuxPanePosition pane_position;
    TmuxPaneID kill_pane;
};

struct Msg {

    Msg(MsgType type) : type(type) {};
    Msg(TermInitLayout init_layout) : type(MsgType::RESOLUTION) {
        msg.term_init_layout = init_layout;
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
