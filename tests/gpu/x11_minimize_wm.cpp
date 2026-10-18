#include <array>
#include <cstdio>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

// Test-only window manager for an isolated rootful Xwayland server. It uses
// actual map/unmap requests and EWMH notifications, never SDL flag injection.
int main(int argc, char** argv) {
    if (argc != 2) {
        std::fputs("usage: x11_minimize_wm <isolated-display>\n", stderr);
        return 2;
    }
    auto* display = XOpenDisplay(argv[1]);
    if (!display) return 2;
    const auto root = DefaultRootWindow(display);
    // Fail with BadAccess before touching properties if another WM owns this
    // display. The runner supplies a separately spawned display explicitly.
    XSelectInput(display, root, SubstructureRedirectMask | SubstructureNotifyMask);
    XSync(display, False);
    const auto wm_state = XInternAtom(display, "WM_STATE", False);
    const auto change_state = XInternAtom(display, "WM_CHANGE_STATE", False);
    const auto net_state = XInternAtom(display, "_NET_WM_STATE", False);
    const auto hidden = XInternAtom(display, "_NET_WM_STATE_HIDDEN", False);
    const auto supported = XInternAtom(display, "_NET_SUPPORTED", False);
    const auto check = XInternAtom(display, "_NET_SUPPORTING_WM_CHECK", False);
    const auto manager = XCreateSimpleWindow(display, root, 0, 0, 1, 1, 0, 0, 0);
    XChangeProperty(display,
                    root,
                    check,
                    XA_WINDOW,
                    32,
                    PropModeReplace,
                    reinterpret_cast<const unsigned char*>(&manager),
                    1);
    XChangeProperty(display,
                    manager,
                    check,
                    XA_WINDOW,
                    32,
                    PropModeReplace,
                    reinterpret_cast<const unsigned char*>(&manager),
                    1);
    const auto name = XInternAtom(display, "_NET_WM_NAME", False);
    const auto utf8 = XInternAtom(display, "UTF8_STRING", False);
    constexpr unsigned char K_MANAGER_NAME[] {"World Explorer isolated verification WM"};
    XChangeProperty(display,
                    manager,
                    name,
                    utf8,
                    8,
                    PropModeReplace,
                    K_MANAGER_NAME,
                    sizeof(K_MANAGER_NAME) - 1);
    const std::array<Atom, 3> supported_atoms { check, net_state, hidden };
    XChangeProperty(display,
                    root,
                    supported,
                    XA_ATOM,
                    32,
                    PropModeReplace,
                    reinterpret_cast<const unsigned char*>(supported_atoms.data()),
                    static_cast<int>(supported_atoms.size()));
    XSync(display, False);
    std::puts("ready");
    std::fflush(stdout);
    for (;;) {
        XEvent event;
        XNextEvent(display, &event);
        if (event.type == MapRequest) {
            const auto window = event.xmaprequest.window;
            const std::array<long, 2> state { NormalState, None };
            XChangeProperty(display,
                            window,
                            wm_state,
                            wm_state,
                            32,
                            PropModeReplace,
                            reinterpret_cast<const unsigned char*>(state.data()),
                            2);
            XMapWindow(display, window);
            XChangeProperty(display, window, net_state, XA_ATOM, 32, PropModeReplace, nullptr, 0);
            std::puts("mapped");
        } else if (event.type == ConfigureRequest) {
            const auto& request = event.xconfigurerequest;
            XWindowChanges changes {request.x,
                                    request.y,
                                    request.width,
                                    request.height,
                                    request.border_width,
                                    request.above,
                                    request.detail};
            XConfigureWindow(
                display, request.window, static_cast<unsigned>(request.value_mask), &changes);
        } else if (event.type == ClientMessage && event.xclient.message_type == change_state &&
                   event.xclient.data.l[0] == IconicState) {
            const auto window = event.xclient.window;
            const std::array<long, 2> state { IconicState, None };
            XChangeProperty(display,
                            window,
                            wm_state,
                            wm_state,
                            32,
                            PropModeReplace,
                            reinterpret_cast<const unsigned char*>(state.data()),
                            2);
            XUnmapWindow(display, window);
            XChangeProperty(display,
                            window,
                            net_state,
                            XA_ATOM,
                            32,
                            PropModeReplace,
                            reinterpret_cast<const unsigned char*>(&hidden),
                            1);
            std::puts("minimized");
        }
        XFlush(display);
        std::fflush(stdout);
    }
}
