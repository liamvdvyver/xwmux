#ifndef LISTEN_H
#define LISTEN_H

const int MAX_CONNECTIONS = 64;

// (WMInstance *)args
struct Listener {
    static void *operator()(void *args);
};

#endif
