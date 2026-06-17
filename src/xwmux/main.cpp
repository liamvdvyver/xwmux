#include "instance.h"
#include "log.h"

#include <algorithm>
#include <iostream>
#include <string_view>

int main(int argc, char **argv) {
    int verbosity = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "-v") {
            verbosity = std::max(verbosity, 1);
        } else if (arg == "-vv") {
            verbosity = std::max(verbosity, 2);
        } else if (arg == "-vvv") {
            verbosity = std::max(verbosity, 3);
        } else {
            std::cerr << "Usage: xwmux [-v|-vv|-vvv]" << std::endl;
            return EXIT_FAILURE;
        }
    }

    xwmux::set_verbosity(verbosity);
    WMInstance instance = WMInstance();
    return EXIT_SUCCESS;
}
