#ifndef LAYOUT_H
#define LAYOUT_H

#include <X11/X.h>
#include <X11/Xlib.h>
#include <cstddef>

struct Resolution {
    std::size_t rows;
    std::size_t cols;
};

struct Dims {

  public:
    Dims(Display *display);

    // Dims
    Resolution term_chars;
    Resolution display_px;
};

#endif
