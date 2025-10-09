#include <X11/Xlib.h>

#include "instance.h"

int main(void) {
    XInitThreads();
    WMInstance instance = WMInstance();
}

