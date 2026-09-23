#pragma once
#include "core/usage.hpp"
#include "ui/pixels.hpp"
#include <memory>
#include <string>

// The only part of the Linux host that sees Xlib. Its headers define macros
// such as None, Status and Font that collide with raylib and Clay, so they stay
// behind this interface. Every window is 32-bit ARGB and takes the renderer's
// premultiplied pixels as they are, as Windows' layered windows do; on Wayland
// desktops it all runs through XWayland.
namespace usage::x11 {
using WindowId = unsigned long;

enum class Role {
    // The floating widget: override-redirect, so the window manager neither
    // places it nor keeps it out of desktop panels - it can sit in one - and it
    // is on every workspace, out of the taskbar and alt-tab, and never focused.
    // The app moves it itself while dragged.
    Widget,
    // The hover card: override-redirect so it sits exactly where it is put and
    // never takes focus.
    Card,
    // The confetti overlay: override-redirect and click-through.
    Overlay,
    // Settings: managed, undecorated, focusable, listed like any other window.
    Dialog,
    // The context menu: override-redirect, and while grabbed it receives every
    // key and every click, including those outside it, which dismiss it.
    Menu,
};
enum class Key { Other, Tab, Left, Right, Up, Down, Home, End, Enter, Space, Escape };
struct Event {
    // Names avoid Xlib's macros (None, Expose), which this header must survive.
    enum class Type { Ignored, Paint, Motion, Enter, Leave, Press, Release, Key, Configure, Close };
    Type type{Type::Ignored};
    WindowId window{};
    // Pointer position in the window and on the screen.
    int x{}, y{}, root_x{}, root_y{};
    // 1 left, 2 middle, 3 right, 4/5 wheel up/down.
    int button{};
    bool shift{}, buttons_held{};
    Key key{Key::Other};
    // Configure: the window's new screen bounds.
    Rect bounds{};
    unsigned long time{};
};
struct Monitor {
    Rect bounds, work;
};

class Connection {
  public:
    // Throws when no X server is reachable.
    Connection();
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    // For poll(): readable when events are waiting.
    int descriptor() const;
    bool pending();
    Event next();
    void flush();

    WindowId create(Role role, Rect bounds, const std::string& title);
    void destroy(WindowId window);
    void show(WindowId window);
    void hide(WindowId window);
    bool visible(WindowId window) const;
    void place(WindowId window, Rect bounds);
    // Screen bounds as the X server has them now.
    Rect bounds(WindowId window);
    // Copies premultiplied pixels to the window, `y` rows down.
    void present(WindowId window, const ui::Pixels& pixels, int y = 0);
    // Asks the window manager to move the window with the pointer, as a title
    // bar drag would; it ends when the button is released.
    void begin_move(WindowId window, const Event& press);
    // Raises and focuses a Dialog, using the triggering input's timestamp so
    // focus-stealing prevention lets it through.
    void activate(WindowId window, unsigned long time);
    void set_hand_cursor(WindowId window, bool hand);
    // Restacks the window above its siblings.
    void raise(WindowId window);
    // Routes all keys, and clicks outside the app's windows, to `window`, as a
    // menu needs. Returns false when another client holds a grab.
    bool grab(WindowId window);
    void ungrab();

    // The pointer's screen position.
    void pointer(int& x, int& y);
    // The monitor containing (x, y), or the nearest one. `work` excludes panels.
    Monitor monitor_at(int x, int y);
    // Logical-to-physical pixel scale: Xft.dpi / 96, or USAGETRACKER_SCALE.
    float scale();
    unsigned long last_input_time() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace usage::x11
