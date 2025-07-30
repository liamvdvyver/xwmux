#ifndef IPC_H
#define IPC_H

#include <cstdint>
#include <layout.h>

#define SOCK_PATH "/tmp/xwmux.sock"

struct Msg {

    Msg() : type(MsgType::EXIT), msg({}) {};

    enum class MsgType { RESOLUTION, EXIT } type;
    union MsgBody {
        Resolution res{};
    } msg;

    Msg(Resolution res) : type(MsgType::RESOLUTION), msg(res) {};
};

enum class Resp : uint8_t { OK, ERR };

#endif
