#pragma once

#include <iostream>
#include <string_view>

constexpr void log_msg(const std::string_view msg) {
    std::cerr << "[XWMUX]: " << msg;
}
