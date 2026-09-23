#include "linux/x11.hpp"
#include "host/platform.hpp"
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/shape.h>
#include <X11/keysym.h>
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
#include <unistd.h>
#include <vector>

namespace usage::x11 {
namespace {
// A window destroyed while requests for it were in flight is routine here;
// Xlib's default handler would end the process over it.
int ignore_error(Display* display, XErrorEvent* error) {
    char text[256]{};
    XGetErrorText(display, error->error_code, text, sizeof(text));
    host::log(std::string("X11 error ignored: ") + text);
    return 0;
}
Rect intersect(const Rect& a, const Rect& b) {
    const int left = std::max(a.x, b.x), top = std::max(a.y, b.y);
    const int right = std::min(a.right(), b.right()), bottom = std::min(a.bottom(), b.bottom());
    return right > left && bottom > top ? Rect{left, top, right - left, bottom - top} : Rect{};
}
} // namespace

struct Connection::Impl {
    Display* display{};
    int screen{};
    Window root{};
    Visual* visual{};
    Colormap colormap{};
    Cursor arrow{}, hand{};
    unsigned long last_time{CurrentTime};
    struct State {
        Role role;
        GC gc{};
        bool mapped{};
    };
    std::map<Window, State> windows;

    Atom atom(const char* name) const {
        return XInternAtom(display, name, False);
    }
    void set_atoms(Window window, const char* property, std::initializer_list<const char*> names) const {
        std::vector<Atom> values;
        for (const char* name : names)
            values.push_back(atom(name));
        XChangeProperty(display, window, atom(property), XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(values.data()),
                        static_cast<int>(values.size()));
    }
    void set_cardinals(Window window, const char* property, const std::vector<long>& values) const {
        XChangeProperty(display, window, atom(property), XA_CARDINAL, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(values.data()),
                        static_cast<int>(values.size()));
    }
    // Pins a managed window's size and asks the window manager to keep the
    // position the program chose.
    void set_size_hints(Window window, const Rect& bounds) const {
        XSizeHints* hints = XAllocSizeHints();
        hints->flags = PPosition | USPosition | PMinSize | PMaxSize;
        hints->x = bounds.x;
        hints->y = bounds.y;
        hints->min_width = hints->max_width = bounds.width;
        hints->min_height = hints->max_height = bounds.height;
        XSetWMNormalHints(display, window, hints);
        XFree(hints);
    }
    void client_message(Window window, const char* type, std::initializer_list<long> data) const {
        XEvent event{};
        event.xclient.type = ClientMessage;
        event.xclient.window = window;
        event.xclient.message_type = atom(type);
        event.xclient.format = 32;
        int i = 0;
        for (const long value : data)
            event.xclient.data.l[i++] = value;
        XSendEvent(display, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &event);
    }
    // Reads a CARDINAL array property from the root window.
    std::vector<long> root_cardinals(const char* property) const {
        Atom type{};
        int format{};
        unsigned long count{}, remaining{};
        unsigned char* data = nullptr;
        std::vector<long> result;
        if (XGetWindowProperty(display, root, atom(property), 0, 4096, False, XA_CARDINAL, &type, &format,
                               &count, &remaining, &data) == Success &&
            data && format == 32) {
            const auto* values = reinterpret_cast<const long*>(data);
            result.assign(values, values + count);
        }
        if (data)
            XFree(data);
        return result;
    }
};

Connection::Connection() : impl_(std::make_unique<Impl>()) {
    auto& x = *impl_;
    x.display = XOpenDisplay(nullptr);
    if (!x.display)
        throw std::runtime_error(
            "Could not connect to an X server. On Wayland desktops BarTab runs through "
            "XWayland; check that DISPLAY is set.");
    XSetErrorHandler(ignore_error);
    x.screen = DefaultScreen(x.display);
    x.root = RootWindow(x.display, x.screen);
    XVisualInfo info{};
    if (!XMatchVisualInfo(x.display, x.screen, 32, TrueColor, &info)) {
        XCloseDisplay(x.display);
        throw std::runtime_error("The X server offers no 32-bit visual for transparent windows.");
    }
    x.visual = info.visual;
    x.colormap = XCreateColormap(x.display, x.root, x.visual, AllocNone);
    x.arrow = XCreateFontCursor(x.display, XC_left_ptr);
    x.hand = XCreateFontCursor(x.display, XC_hand2);
}

Connection::~Connection() {
    auto& x = *impl_;
    for (auto& [window, state] : x.windows) {
        if (state.gc)
            XFreeGC(x.display, state.gc);
        XDestroyWindow(x.display, window);
    }
    XFreeCursor(x.display, x.arrow);
    XFreeCursor(x.display, x.hand);
    XFreeColormap(x.display, x.colormap);
    XCloseDisplay(x.display);
}

int Connection::descriptor() const {
    return ConnectionNumber(impl_->display);
}
bool Connection::pending() {
    return XPending(impl_->display) > 0;
}
void Connection::flush() {
    XFlush(impl_->display);
}
unsigned long Connection::last_input_time() const {
    return impl_->last_time;
}

WindowId Connection::create(Role role, Rect bounds, const std::string& title) {
    auto& x = *impl_;
    const bool unmanaged = role != Role::Dialog;
    XSetWindowAttributes attributes{};
    attributes.colormap = x.colormap;
    attributes.border_pixel = 0;
    attributes.background_pixel = 0;
    attributes.override_redirect = unmanaged ? True : False;
    attributes.event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask | ButtonReleaseMask |
                            PointerMotionMask | EnterWindowMask | LeaveWindowMask | KeyPressMask;
    const Window window = XCreateWindow(
        x.display, x.root, bounds.x, bounds.y, static_cast<unsigned>(std::max(1, bounds.width)),
        static_cast<unsigned>(std::max(1, bounds.height)), 0, 32, InputOutput, x.visual,
        CWColormap | CWBorderPixel | CWBackPixel | CWOverrideRedirect | CWEventMask, &attributes);
    XStoreName(x.display, window, title.c_str());
    XChangeProperty(x.display, window, x.atom("_NET_WM_NAME"), x.atom("UTF8_STRING"), 8, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(title.data()), static_cast<int>(title.size()));
    XClassHint* hint = XAllocClassHint();
    hint->res_name = const_cast<char*>("bartab");
    hint->res_class = const_cast<char*>("BarTab");
    XSetClassHint(x.display, window, hint);
    XFree(hint);
    x.set_cardinals(window, "_NET_WM_PID", {static_cast<long>(getpid())});
    switch (role) {
    case Role::Widget:
        x.set_atoms(window, "_NET_WM_WINDOW_TYPE", {"_NET_WM_WINDOW_TYPE_DOCK"});
        break;
    case Role::Dialog:
        x.set_atoms(window, "_NET_WM_WINDOW_TYPE", {"_NET_WM_WINDOW_TYPE_NORMAL"});
        break;
    case Role::Card:
        x.set_atoms(window, "_NET_WM_WINDOW_TYPE", {"_NET_WM_WINDOW_TYPE_TOOLTIP"});
        break;
    case Role::Menu:
        x.set_atoms(window, "_NET_WM_WINDOW_TYPE", {"_NET_WM_WINDOW_TYPE_POPUP_MENU"});
        break;
    case Role::Overlay: {
        x.set_atoms(window, "_NET_WM_WINDOW_TYPE", {"_NET_WM_WINDOW_TYPE_DND"});
        // An empty input shape: every click falls through to what is below.
        XShapeCombineRectangles(x.display, window, ShapeInput, 0, 0, nullptr, 0, ShapeSet, Unsorted);
        break;
    }
    }
    if (!unmanaged) {
        // No title bar or borders: the views draw their own.
        const long motif[5] = {2 /* decorations */, 0, 0, 0, 0};
        XChangeProperty(x.display, window, x.atom("_MOTIF_WM_HINTS"), x.atom("_MOTIF_WM_HINTS"), 32,
                        PropModeReplace, reinterpret_cast<const unsigned char*>(motif), 5);
        XWMHints* hints = XAllocWMHints();
        hints->flags = InputHint;
        hints->input = True;
        XSetWMHints(x.display, window, hints);
        XFree(hints);
        Atom close = x.atom("WM_DELETE_WINDOW");
        XSetWMProtocols(x.display, window, &close, 1);
        x.set_size_hints(window, bounds);
    }
    XDefineCursor(x.display, window, x.arrow);
    x.windows[window] = {role, XCreateGC(x.display, window, 0, nullptr), false};
    return window;
}

void Connection::destroy(WindowId window) {
    auto& x = *impl_;
    const auto found = x.windows.find(window);
    if (found == x.windows.end())
        return;
    if (found->second.gc)
        XFreeGC(x.display, found->second.gc);
    XDestroyWindow(x.display, window);
    x.windows.erase(found);
}

void Connection::show(WindowId window) {
    auto& x = *impl_;
    const auto found = x.windows.find(window);
    if (found == x.windows.end())
        return;
    XMapRaised(x.display, window);
    found->second.mapped = true;
}

void Connection::hide(WindowId window) {
    auto& x = *impl_;
    const auto found = x.windows.find(window);
    if (found == x.windows.end() || !found->second.mapped)
        return;
    XUnmapWindow(x.display, window);
    found->second.mapped = false;
}

bool Connection::visible(WindowId window) const {
    const auto found = impl_->windows.find(window);
    return found != impl_->windows.end() && found->second.mapped;
}

void Connection::place(WindowId window, Rect bounds) {
    auto& x = *impl_;
    const auto found = x.windows.find(window);
    if (found == x.windows.end() || bounds.empty())
        return;
    if (found->second.role == Role::Dialog)
        x.set_size_hints(window, bounds);
    XMoveResizeWindow(x.display, window, bounds.x, bounds.y, static_cast<unsigned>(bounds.width),
                      static_cast<unsigned>(bounds.height));
}

Rect Connection::bounds(WindowId window) {
    auto& x = *impl_;
    Window child{}, ignored{};
    int left{}, top{}, unused_x{}, unused_y{};
    unsigned width{}, height{}, border{}, depth{};
    if (!XGetGeometry(x.display, window, &ignored, &unused_x, &unused_y, &width, &height, &border, &depth) ||
        !XTranslateCoordinates(x.display, window, x.root, 0, 0, &left, &top, &child))
        return {};
    return {left, top, static_cast<int>(width), static_cast<int>(height)};
}

void Connection::present(WindowId window, const ui::Pixels& pixels, int y) {
    auto& x = *impl_;
    const auto found = x.windows.find(window);
    if (found == x.windows.end() || pixels.data.empty())
        return;
    XImage* image = XCreateImage(x.display, x.visual, 32, ZPixmap, 0,
                                 reinterpret_cast<char*>(const_cast<std::uint32_t*>(pixels.data.data())),
                                 static_cast<unsigned>(pixels.width), static_cast<unsigned>(pixels.height),
                                 32, pixels.width * 4);
    if (!image)
        return;
    // The pixels are host-order 32-bit words; Xlib swaps if the server differs.
    const std::uint32_t probe = 1;
    image->byte_order = *reinterpret_cast<const unsigned char*>(&probe) ? LSBFirst : MSBFirst;
    XPutImage(x.display, window, found->second.gc, image, 0, 0, 0, y, static_cast<unsigned>(pixels.width),
              static_cast<unsigned>(pixels.height));
    image->data = nullptr; // Owned by `pixels`.
    XDestroyImage(image);
}

void Connection::begin_move(WindowId window, const Event& press) {
    auto& x = *impl_;
    XUngrabPointer(x.display, CurrentTime);
    constexpr long move = 8; // _NET_WM_MOVERESIZE_MOVE
    x.client_message(window, "_NET_WM_MOVERESIZE", {press.root_x, press.root_y, move, press.button, 1});
    XFlush(x.display);
}

void Connection::activate(WindowId window, unsigned long time) {
    auto& x = *impl_;
    x.set_cardinals(window, "_NET_WM_USER_TIME", {static_cast<long>(time)});
    x.client_message(window, "_NET_ACTIVE_WINDOW", {1, static_cast<long>(time), 0});
    XRaiseWindow(x.display, window);
}

void Connection::set_hand_cursor(WindowId window, bool hand) {
    XDefineCursor(impl_->display, window, hand ? impl_->hand : impl_->arrow);
}

void Connection::raise(WindowId window) {
    XRaiseWindow(impl_->display, window);
}

bool Connection::grab(WindowId window) {
    auto& x = *impl_;
    // The pointer grab reports the app's own windows as usual and everything
    // else to `window`; the keyboard grab sends every key there.
    const unsigned mask =
        ButtonPressMask | ButtonReleaseMask | PointerMotionMask | EnterWindowMask | LeaveWindowMask;
    const bool pointer = XGrabPointer(x.display, window, True, mask, GrabModeAsync, GrabModeAsync, None, None,
                                      CurrentTime) == GrabSuccess;
    const bool keyboard =
        XGrabKeyboard(x.display, window, False, GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess;
    return pointer && keyboard;
}

void Connection::ungrab() {
    auto& x = *impl_;
    XUngrabPointer(x.display, CurrentTime);
    XUngrabKeyboard(x.display, CurrentTime);
}

void Connection::pointer(int& px, int& py) {
    auto& x = *impl_;
    Window root_return{}, child{};
    int window_x{}, window_y{};
    unsigned mask{};
    px = py = 0;
    XQueryPointer(x.display, x.root, &root_return, &child, &px, &py, &window_x, &window_y, &mask);
}

Monitor Connection::monitor_at(int px, int py) {
    auto& x = *impl_;
    std::vector<Rect> monitors;
    int count = 0;
    if (XRRMonitorInfo* info = XRRGetMonitors(x.display, x.root, True, &count)) {
        for (int i = 0; i < count; ++i)
            monitors.push_back({info[i].x, info[i].y, info[i].width, info[i].height});
        XRRFreeMonitors(info);
    }
    if (monitors.empty())
        monitors.push_back({0, 0, DisplayWidth(x.display, x.screen), DisplayHeight(x.display, x.screen)});
    // The monitor containing the point, else the nearest by distance to its edges.
    const Rect* best = &monitors.front();
    long best_distance = LONG_MAX;
    for (const auto& monitor : monitors) {
        const long dx = std::max({0, monitor.x - px, px - monitor.right() + 1});
        const long dy = std::max({0, monitor.y - py, py - monitor.bottom() + 1});
        if (dx * dx + dy * dy < best_distance) {
            best_distance = dx * dx + dy * dy;
            best = &monitor;
        }
    }
    Monitor result{*best, *best};
    // Mutter publishes one work area per monitor; other window managers one
    // for the whole desktop, which each monitor intersects.
    const auto areas = x.root_cardinals("_GTK_WORKAREAS_D0");
    for (std::size_t i = 0; i + 3 < areas.size(); i += 4) {
        const Rect area{static_cast<int>(areas[i]), static_cast<int>(areas[i + 1]),
                        static_cast<int>(areas[i + 2]), static_cast<int>(areas[i + 3])};
        const auto inside = intersect(area, result.bounds);
        if (!inside.empty())
            return {result.bounds, inside};
    }
    const auto desktop = x.root_cardinals("_NET_WORKAREA");
    if (desktop.size() >= 4) {
        const auto inside = intersect({static_cast<int>(desktop[0]), static_cast<int>(desktop[1]),
                                       static_cast<int>(desktop[2]), static_cast<int>(desktop[3])},
                                      result.bounds);
        if (!inside.empty())
            result.work = inside;
    }
    return result;
}

float Connection::scale() {
    if (const char* forced = std::getenv("BARTAB_SCALE")) {
        const float value = std::strtof(forced, nullptr);
        if (value >= 0.5f && value <= 4.f)
            return value;
    }
    // Read RESOURCE_MANAGER fresh each time: desktops update Xft.dpi when the
    // user changes text scaling, and Xlib's copy is from connection time.
    auto& x = *impl_;
    Atom type{};
    int format{};
    unsigned long count{}, remaining{};
    unsigned char* data = nullptr;
    float dpi = 96.f;
    if (XGetWindowProperty(x.display, x.root, XA_RESOURCE_MANAGER, 0, 65536, False, XA_STRING, &type, &format,
                           &count, &remaining, &data) == Success &&
        data) {
        const std::string resources(reinterpret_cast<const char*>(data), count);
        const auto key = resources.find("Xft.dpi:");
        if (key != std::string::npos) {
            const float value = std::strtof(resources.c_str() + key + 8, nullptr);
            if (value >= 48.f && value <= 384.f)
                dpi = value;
        }
    }
    if (data)
        XFree(data);
    return dpi / 96.f;
}

Event Connection::next() {
    auto& x = *impl_;
    XEvent raw{};
    XNextEvent(x.display, &raw);
    Event event;
    event.window = raw.xany.window;
    switch (raw.type) {
    case Expose:
        if (raw.xexpose.count == 0)
            event.type = Event::Type::Paint;
        break;
    case MotionNotify: {
        // Only the latest position matters.
        while (XCheckTypedWindowEvent(x.display, raw.xmotion.window, MotionNotify, &raw)) {
        }
        event.type = Event::Type::Motion;
        event.x = raw.xmotion.x;
        event.y = raw.xmotion.y;
        event.root_x = raw.xmotion.x_root;
        event.root_y = raw.xmotion.y_root;
        event.buttons_held = (raw.xmotion.state & (Button1Mask | Button2Mask | Button3Mask)) != 0;
        event.time = raw.xmotion.time;
        break;
    }
    case EnterNotify:
    case LeaveNotify:
        // Crossings into or out of our own child windows are not real ones.
        if (raw.xcrossing.detail == NotifyInferior)
            break;
        event.type = raw.type == EnterNotify ? Event::Type::Enter : Event::Type::Leave;
        event.x = raw.xcrossing.x;
        event.y = raw.xcrossing.y;
        event.root_x = raw.xcrossing.x_root;
        event.root_y = raw.xcrossing.y_root;
        event.buttons_held = (raw.xcrossing.state & (Button1Mask | Button2Mask | Button3Mask)) != 0;
        break;
    case ButtonPress:
    case ButtonRelease:
        event.type = raw.type == ButtonPress ? Event::Type::Press : Event::Type::Release;
        event.x = raw.xbutton.x;
        event.y = raw.xbutton.y;
        event.root_x = raw.xbutton.x_root;
        event.root_y = raw.xbutton.y_root;
        event.button = static_cast<int>(raw.xbutton.button);
        event.shift = (raw.xbutton.state & ShiftMask) != 0;
        event.time = x.last_time = raw.xbutton.time;
        break;
    case KeyPress: {
        event.type = Event::Type::Key;
        event.shift = (raw.xkey.state & ShiftMask) != 0;
        event.time = x.last_time = raw.xkey.time;
        switch (XLookupKeysym(&raw.xkey, 0)) {
        case XK_Tab:
        case XK_ISO_Left_Tab:
            event.key = Key::Tab;
            break;
        case XK_Left:
        case XK_KP_Left:
            event.key = Key::Left;
            break;
        case XK_Right:
        case XK_KP_Right:
            event.key = Key::Right;
            break;
        case XK_Up:
        case XK_KP_Up:
            event.key = Key::Up;
            break;
        case XK_Down:
        case XK_KP_Down:
            event.key = Key::Down;
            break;
        case XK_Home:
        case XK_KP_Home:
            event.key = Key::Home;
            break;
        case XK_End:
        case XK_KP_End:
            event.key = Key::End;
            break;
        case XK_Return:
        case XK_KP_Enter:
            event.key = Key::Enter;
            break;
        case XK_space:
            event.key = Key::Space;
            break;
        case XK_Escape:
            event.key = Key::Escape;
            break;
        default:
            break;
        }
        break;
    }
    case ConfigureNotify:
        event.type = Event::Type::Configure;
        event.bounds = bounds(raw.xconfigure.window);
        break;
    case ClientMessage:
        if (static_cast<Atom>(raw.xclient.data.l[0]) == x.atom("WM_DELETE_WINDOW"))
            event.type = Event::Type::Close;
        break;
    default:
        break;
    }
    return event;
}
} // namespace usage::x11
