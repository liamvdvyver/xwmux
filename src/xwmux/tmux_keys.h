#include <string>
#include <unordered_map>

#include <X11/X.h>
#include <X11/Xlib.h>

#include "xwrapper.h"

#ifndef XK_MISCELLANY
#define XK_MISCELLANY
#endif

#ifndef XK_LATIN1
#define XK_LATIN1
#endif

#include <X11/keysymdef.h>

static const std::unordered_map<std::string, KeySym> tmux_key_maps{
    {"F1", XK_F1},
    {"F2", XK_F2},
    {"F3", XK_F3},
    {"F4", XK_F4},
    {"F5", XK_F5},
    {"F6", XK_F6},
    {"F7", XK_F7},
    {"F8", XK_F8},
    {"F9", XK_F9},
    {"F10", XK_F10},
    {"F11", XK_F11},
    {"F12", XK_F12},
    {"IC", XK_Insert},
    {"DC", XK_Delete},
    {"Home", XK_Home},
    {"End", XK_End},
    {"NPage", XK_Page_Down},
    {"PageDown", XK_Page_Down},
    {"PgDn", XK_Page_Down},
    {"PPage", XK_Page_Up},
    {"PageUp", XK_Page_Up},
    {"PgUp", XK_Page_Up},
    {"Tab", XK_Tab},
    {"BTab", XK_VoidSymbol},
    {"Space", XK_space},
    {"BSpace", XK_BackSpace},
    {"Enter", XK_Return},
    {"Escape", XK_Escape},

    {"Up", XK_Up},
    {"Down", XK_Down},
    {"Left", XK_Left},
    {"Right", XK_Right},

    {"KP/", XK_KP_Divide},
    {"KP*", XK_KP_Multiply},
    {"KP-", XK_KP_Subtract},
    {"KP7", XK_KP_7},
    {"KP8", XK_KP_8},
    {"KP9", XK_KP_9},
    {"KP+", XK_KP_Add},
    {"KP4", XK_KP_4},
    {"KP5", XK_KP_5},
    {"KP6", XK_KP_6},
    {"KP1", XK_KP_1},
    {"KP2", XK_KP_2},
    {"KP3", XK_KP_3},
    {"KPEnter", XK_KP_Enter},
    {"KP0", XK_KP_0},
    {"KP.", XK_KP_Decimal},

    {"MouseDown1Pane", 0},
    {"MouseDown1Status", 0},
    {"MouseDown1Border", 0},
    {"MouseDown2Pane", 0},
    {"MouseDown2Status", 0},
    {"MouseDown2Border", 0},
    {"MouseDown3Pane", 0},
    {"MouseDown3Status", 0},
    {"MouseDown3Border", 0},
    {"MouseUp1Pane", 0},
    {"MouseUp1Status", 0},
    {"MouseUp1Border", 0},
    {"MouseUp2Pane", 0},
    {"MouseUp2Status", 0},
    {"MouseUp2Border", 0},
    {"MouseUp3Pane", 0},
    {"MouseUp3Status", 0},
    {"MouseUp3Border", 0},
    {"MouseDrag1Pane", 0},
    {"MouseDrag1Status", 0},
    {"MouseDrag1Border", 0},
    {"MouseDrag2Pane", 0},
    {"MouseDrag2Status", 0},
    {"MouseDrag2Border", 0},
    {"MouseDrag3Pane", 0},
    {"MouseDrag3Status", 0},
    {"MouseDrag3Border", 0},
    {"MouseDragEnd1Pane", 0},
    {"MouseDragEnd1Status", 0},
    {"MouseDragEnd1Border", 0},
    {"MouseDragEnd2Pane", 0},
    {"MouseDragEnd2Status", 0},
    {"MouseDragEnd2Border", 0},
    {"MouseDragEnd3Pane", 0},
    {"MouseDragEnd3Status", 0},
    {"MouseDragEnd3Border", 0},
    {"WheelUpPane", 0},
    {"WheelUpStatus", 0},
    {"WheelUpBorder", 0},
    {"WheelDownPane", 0},
    {"WheelDownStatus", 0},
    {"WheelDownBorder", 0},
};

constexpr KeySym tmux_to_keysym(const std::string &key) {
    if (tmux_key_maps.count(key))
        return tmux_key_maps.at(key);
    else
        return XStringToKeysym(key.c_str());
}

constexpr ModifiedKeyCode tmux_to_keycode(Display *dpy,
                                          const std::string &key) {
    uint mod_mask = 0;
    size_t n = 0;
    while (n + 1 < key.length() && key[1] == '-') {
        uint new_mod = 0;
        switch (key[0]) {
        case 'M':
        case 'm':
            new_mod = Mod1Mask;
            break;
        case 'S':
        case 's':
            new_mod = ShiftMask;
            break;
        case 'C':
        case 'c':
            new_mod = ControlMask;
            break;
        default:
            break;
        }
        if (!new_mod)
            break;
        mod_mask |= new_mod;
        n += 2;
    }
    KeySym sym = tmux_to_keysym(key.substr(n));
    return ModifiedKeyCode(XKeysymToKeycode(dpy, sym), mod_mask);
}
