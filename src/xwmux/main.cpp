#include "instance.h"
#include "log.h"

#include <algorithm>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

void print_usage(std::ostream &out) {
    out << "Usage: xwmux [--tmux-socket <path>] [-v|-vv|-vvv]" << std::endl;
}

} // namespace

int main(int argc, char **argv) {
    int verbosity = 0;
    std::optional<std::string> tmux_socket;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--help") {
            print_usage(std::cout);
            return EXIT_SUCCESS;
        }
        if (arg == "--tmux-socket") {
            if (tmux_socket.has_value() || ++i == argc) {
                print_usage(std::cerr);
                return EXIT_FAILURE;
            }
            tmux_socket = argv[i];
        } else if (arg == "-v") {
            verbosity = std::max(verbosity, 1);
        } else if (arg == "-vv") {
            verbosity = std::max(verbosity, 2);
        } else if (arg == "-vvv") {
            verbosity = std::max(verbosity, 3);
        } else {
            print_usage(std::cerr);
            return EXIT_FAILURE;
        }
    }

    xwmux::set_verbosity(verbosity);
    WMInstance instance = WMInstance(tmux_socket);
    return EXIT_SUCCESS;
}
