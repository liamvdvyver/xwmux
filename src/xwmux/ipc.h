#ifndef IPC_H
#define IPC_H

#include <cstdint>

struct Resolution {
    std::size_t rows;
    std::size_t cols;
};

struct Msg {
    enum class MsgType { RESOLUTION } type;
    union MsgBody {
        Resolution res;
    } msg;

    Msg(Resolution res) : type(MsgType::RESOLUTION), msg(res) {};
};

enum class Resp : uint8_t { OK, ERR };

const char *SOCK_PATH = "/tmp/wxmux.sock";

#endif
