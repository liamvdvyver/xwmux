#include <X11/Xlib.h>

#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
    Display *display = XOpenDisplay(NULL);
    if (display == NULL) {
        return 2;
    }

    Window window = XCreateSimpleWindow(display, DefaultRootWindow(display),
                                        0, 0, 1, 1, 0, 0, 0);
    XMapWindow(display, window);
    XSync(display, False);
    if (argc > 1) {
        usleep(strtoul(argv[1], NULL, 10));
    }
    XDestroyWindow(display, window);
    XSync(display, False);
    XCloseDisplay(display);
    return 0;
}
