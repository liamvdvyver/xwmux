#ifndef LAYOUT_H
#define LAYOUT_H

#include <X11/Xlib.h>
#include <cstddef>
#include <X11/X.h>

struct Dims {

  public:

    Dims(Display *display);

    // Dims
    std::size_t rows;
    std::size_t cols;

    std::size_t h_px;
    std::size_t v_px
}

#endif
