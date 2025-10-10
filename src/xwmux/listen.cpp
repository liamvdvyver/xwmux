#include <X11/X.h>
#include <X11/Xlib.h>
#include <cstdlib>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "instance.h"
#include "ipc.h"
#include "listen.h"

void *Listener::operator()(void *args) {

    WMInstance *instance = (WMInstance *)args;
    Display *display = XOpenDisplay(nullptr);

    Msg msg(MsgType::EXIT);

    struct sockaddr_un local = {.sun_family = AF_UNIX, .sun_path = SOCK_PATH};

    int sock_listen;
    if ((sock_listen = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    };

    if (unlink(local.sun_path) == -1) {
        if (errno != 2) {
            perror("unlink");
            exit(1);
        };
    };

    if (bind(sock_listen, (const struct sockaddr *)&local, sizeof(local)) ==
        -1) {
        perror("bind");
        exit(1);
    };

    // Receive

    struct sockaddr_un remote;
    remote.sun_family = AF_UNIX;
    strncpy(remote.sun_path, SOCK_PATH, sizeof(remote.sun_path) - 1);

    if ((listen(sock_listen, MAX_CONNECTIONS)) == -1) {
        perror("connect");
        exit(EXIT_FAILURE);
    };

    int sock_con;
    socklen_t slen = sizeof(remote);
    int exit_status = EXIT_FAILURE;
    while ((sock_con =
                accept(sock_listen, (struct sockaddr *)&remote, &slen)) != -1) {

        // Connection loop
        Resp resp;
        if (recv(sock_con, &msg, sizeof(msg), 0) == -1) {
            perror("recv");
            resp = Resp::ERR;
        } else {
            resp = Resp::OK;
        }

        if ((send(sock_con, &resp, sizeof(Resp), 0)) == -1) {
            perror("send");
        };

        close(sock_con);

        // Handle
        instance->push_msg(msg);

        XEvent ev;
        ev.type = ClientMessage;
        ev.xclient.type = ClientMessage;
        ev.xclient.display = display;
        ev.xclient.window = instance->get_xstate().root;
        ev.xclient.message_type = XInternAtom(display, "_XWMUX_Q", 0);
        ev.xclient.format = 32;

        if (XSendEvent(display, instance->get_xstate().root, false,
                       SubstructureRedirectMask, &ev)) {
            // send_message("failed to send event message");
        };
        XFlush(display);

        if (msg.type == MsgType::EXIT) {
            exit_status = EXIT_SUCCESS;
            break;
        }
    };

    if (exit_status) {
        perror("accept");
        exit_status = EXIT_FAILURE;
    }

    XCloseDisplay(display);
    close(sock_listen);
    pthread_exit(&exit_status);
}
