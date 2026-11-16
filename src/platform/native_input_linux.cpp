// clang-format off
// Qt 头必须先于 Xlib：Xlib 宏会污染 QEvent、CursorShape 等 Qt 标识符。
#include "platform/native_input.h"
#include <QGuiApplication>
#include <QWindow>
#include <qpa/qplatformnativeinterface.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <wayland-client.h>
#include <poll.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <thread>
#include "pointer-constraints-client.h"
#include "relative-pointer-client.h"
// clang-format on

struct NativeInput::Impl {
    std::shared_ptr<InputState> state;
    std::jthread thread;
    ::Window target = 0;
    wl_display* display = nullptr;  // 借用 Qt 连接，不能 disconnect 或销毁 Qt 的 proxy。
    wl_surface* surface = nullptr;
    wl_event_queue* queue = nullptr;
    wl_registry* registry = nullptr;
    wl_seat* seat = nullptr;
    wl_pointer* pointer = nullptr;
    wl_keyboard* keyboard = nullptr;
    zwp_relative_pointer_manager_v1* relative_manager = nullptr;
    zwp_pointer_constraints_v1* constraints = nullptr;
    zwp_relative_pointer_v1* relative = nullptr;
    zwp_locked_pointer_v1* lock = nullptr;
    bool keyboard_focus = false, pointer_focus = false, locked = false;
    uint32_t pointer_serial = 0;
    bool pending_mouse_report = false, has_discrete_wheel = false;
    double wheel_axis = 0, wheel_discrete = 0;

    explicit Impl(std::shared_ptr<InputState> input) : state(std::move(input)) {}
    ~Impl() {
        thread.request_stop();
        if (thread.joinable()) thread.join();
    }

    bool ownsFocus(Display* connection) const {
        ::Window focus = 0;
        int revert = 0;
        XGetInputFocus(connection, &focus, &revert);
        for (int depth = 0; focus > PointerRoot && depth < 16; ++depth) {
            if (focus == target) return true;
            ::Window root, parent, *children = nullptr;
            unsigned count = 0;
            if (!XQueryTree(connection, focus, &root, &parent, &children, &count)) break;
            if (children) XFree(children);
            focus = parent;
        }
        return false;
    }
    void runX11(std::stop_token stop) {
        // 本连接只由输入线程使用，不能在 Qt 的 Xlib 连接上抢读事件。
        Display* connection = XOpenDisplay(nullptr);
        if (!connection) {
            state->backend("X11 XI2", "无法打开 X11 输入连接");
            return;
        }
        int opcode, event, error;
        // XI 2.1 才保证其他客户端/本线程指针 grab 期间仍向根窗口交付 raw 事件。
        int major = 2, minor = 1;
        if (!XQueryExtension(connection, "XInputExtension", &opcode, &event, &error) ||
            XIQueryVersion(connection, &major, &minor) != Success || major < 2 ||
            (major == 2 && minor < 1)) {
            state->backend("X11 XI2", "显示服务器不支持捕获所需的 XI 2.1 原始输入");
            XCloseDisplay(connection);
            return;
        }
        std::array<unsigned char, XIMaskLen(XI_LASTEVENT)> mask{};
        for (int kind : {XI_RawKeyPress, XI_RawKeyRelease, XI_RawButtonPress, XI_RawButtonRelease,
                         XI_RawMotion, XI_HierarchyChanged})
            XISetMask(mask.data(), kind);
        XIEventMask selection{XIAllMasterDevices, int(mask.size()), mask.data()};
        // 层级变化只对 XIAllDevices 有效，单独注册；raw 只选 master，避免 slave/master 重复。
        XIClearMask(mask.data(), XI_HierarchyChanged);
        XISelectEvents(connection, DefaultRootWindow(connection), &selection, 1);
        unsigned char hierarchy[XIMaskLen(XI_LASTEVENT)]{};
        XISetMask(hierarchy, XI_HierarchyChanged);
        XIEventMask changes{XIAllDevices, sizeof(hierarchy), hierarchy};
        XISelectEvents(connection, DefaultRootWindow(connection), &changes, 1);
        char empty[] = {0};
        const auto bitmap = XCreateBitmapFromData(connection, target, empty, 1, 1);
        XColor color{};
        const auto cursor = XCreatePixmapCursor(connection, bitmap, bitmap, &color, &color, 0, 0);
        XFreePixmap(connection, bitmap);
        XFlush(connection);
        state->backend("X11 XI2 / 独立连接");
        bool grabbed = false, was_active = false;
        while (!stop.stop_requested()) {
            const auto [mode, wants_lock] = state->requested();
            const bool active = mode != InputState::Mode::Off && ownsFocus(connection);
            if (active && wants_lock && !grabbed) {
                grabbed = XGrabPointer(connection, target, True, 0, GrabModeAsync, GrabModeAsync,
                                       target, cursor, CurrentTime) == GrabSuccess;
                XFlush(connection);
            } else if ((!active || !wants_lock) && grabbed) {
                XUngrabPointer(connection, CurrentTime);
                XFlush(connection);
                grabbed = false;
            }
            if (active && !was_active) {
                state->resetDevices();
                char keys[32];
                XQueryKeymap(connection, keys);
                // Xorg evdev 驱动的 keycode = evdev + 8；非 evdev 布局须实机确认。
                for (int code = 8; code < 256; ++code)
                    if (keys[code / 8] & (1 << (code % 8))) state->seedKey(0, code - 8);
            }
            was_active = active;
            state->capture(active, grabbed, InputState::nowUs());
            for (int count = 0; count < 512 && XPending(connection); ++count) {
                XEvent next;
                XNextEvent(connection, &next);
                if (next.type != GenericEvent || next.xcookie.extension != opcode ||
                    !XGetEventData(connection, &next.xcookie))
                    continue;
                if (next.xcookie.evtype == XI_HierarchyChanged) {
                    auto* hierarchy_event = static_cast<XIHierarchyEvent*>(next.xcookie.data);
                    for (int i = 0; i < hierarchy_event->num_info; ++i)
                        if (hierarchy_event->info[i].flags &
                            (XISlaveRemoved | XIMasterRemoved | XIDeviceDisabled))
                            state->removeDevice(hierarchy_event->info[i].deviceid);
                } else if (active) {
                    auto* raw = static_cast<XIRawEvent*>(next.xcookie.data);
                    const auto time = InputState::nowUs();
                    const auto kind = next.xcookie.evtype;
                    if (kind == XI_RawKeyPress || kind == XI_RawKeyRelease)
                        state->key(raw->sourceid, raw->detail - 8, kind == XI_RawKeyPress, time);
                    else if (kind == XI_RawMotion) {
                        double x = 0, y = 0;
                        int index = 0;
                        for (int axis = 0; axis < raw->valuators.mask_len * 8; ++axis)
                            if (XIMaskIsSet(raw->valuators.mask, axis)) {
                                if (axis == 0) x = raw->raw_values[index];
                                if (axis == 1) y = raw->raw_values[index];
                                ++index;
                            }
                        state->mouseReport(time);
                        state->motion(x, y, 0, time);
                    } else if (kind == XI_RawButtonPress || kind == XI_RawButtonRelease) {
                        state->mouseReport(time);
                        const bool down = kind == XI_RawButtonPress;
                        if (raw->detail >= 1 && raw->detail <= 3) {
                            const int codes[] = {272, 274, 273};
                            state->key(raw->sourceid, codes[raw->detail - 1], down, time);
                        } else if (raw->detail == 8 || raw->detail == 9) {
                            state->key(raw->sourceid, 275 + raw->detail - 8, down, time);
                        } else if (down && (raw->detail == 4 || raw->detail == 5))
                            state->motion(0, 0, raw->detail == 4 ? 120 : -120, time);
                    }
                }
                XFreeEventData(connection, &next.xcookie);
            }
            pollfd fd{ConnectionNumber(connection), POLLIN, 0};
            poll(&fd, 1, 2);
        }
        if (grabbed) XUngrabPointer(connection, CurrentTime);
        XFreeCursor(connection, cursor);
        XCloseDisplay(connection);
        state->capture(false, false, InputState::nowUs());
    }

    static void global(void* data, wl_registry* registry, uint32_t name, const char* interface,
                       uint32_t version) {
        auto& self = *static_cast<Impl*>(data);
        if (!strcmp(interface, "wl_seat") && !self.seat) {
            self.seat = static_cast<wl_seat*>(
                wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 5u)));
            static const wl_seat_listener listener{capabilities, seatName};
            wl_seat_add_listener(self.seat, &listener, &self);
        } else if (!strcmp(interface, "zwp_relative_pointer_manager_v1"))
            self.relative_manager = static_cast<zwp_relative_pointer_manager_v1*>(
                wl_registry_bind(registry, name, &zwp_relative_pointer_manager_v1_interface, 1));
        else if (!strcmp(interface, "zwp_pointer_constraints_v1"))
            self.constraints = static_cast<zwp_pointer_constraints_v1*>(
                wl_registry_bind(registry, name, &zwp_pointer_constraints_v1_interface, 1));
    }
    static void globalRemoved(void*, wl_registry*, uint32_t) {}
    static void seatName(void*, wl_seat*, const char*) {}
    static void capabilities(void* data, wl_seat* seat, uint32_t capabilities) {
        auto& self = *static_cast<Impl*>(data);
        if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !self.pointer) {
            self.pointer = wl_seat_get_pointer(seat);
            static const wl_pointer_listener listener{pointerEnter,  pointerLeave, pointerMotion,
                                                      pointerButton, pointerAxis,  pointerFrame,
                                                      axisSource,    axisStop,     axisDiscrete};
            wl_pointer_add_listener(self.pointer, &listener, &self);
        }
        if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !self.keyboard) {
            self.keyboard = wl_seat_get_keyboard(seat);
            static const wl_keyboard_listener listener{keymap,      keyboardEnter, keyboardLeave,
                                                       keyboardKey, modifiers,     repeatInfo};
            wl_keyboard_add_listener(self.keyboard, &listener, &self);
        }
        if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD)) {
            self.keyboard_focus = false;
            self.state->removeDevice(1);
        }
        if (!(capabilities & WL_SEAT_CAPABILITY_POINTER)) self.pointer_focus = false;
    }
    static void keymap(void*, wl_keyboard*, uint32_t, int32_t fd, uint32_t) { close(fd); }
    static void keyboardEnter(void* data, wl_keyboard*, uint32_t, wl_surface* surface,
                              wl_array* keys) {
        auto& self = *static_cast<Impl*>(data);
        self.keyboard_focus = surface == self.surface;
        self.state->resetDevices();
        if (self.keyboard_focus)
            for (auto* key = static_cast<uint32_t*>(keys->data);
                 reinterpret_cast<char*>(key) < static_cast<char*>(keys->data) + keys->size; ++key)
                self.state->seedKey(1, *key);
    }
    static void keyboardLeave(void* data, wl_keyboard*, uint32_t, wl_surface*) {
        auto& self = *static_cast<Impl*>(data);
        self.keyboard_focus = false;
        self.state->capture(false, false, InputState::nowUs());
    }
    static void keyboardKey(void* data, wl_keyboard*, uint32_t, uint32_t, uint32_t key,
                            uint32_t down) {
        auto& self = *static_cast<Impl*>(data);
        if (self.keyboard_focus)
            self.state->key(1, key, down == WL_KEYBOARD_KEY_STATE_PRESSED, InputState::nowUs());
    }
    static void modifiers(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
    static void repeatInfo(void*, wl_keyboard*, int32_t, int32_t) {}
    static void pointerEnter(void* data, wl_pointer*, uint32_t serial, wl_surface* surface,
                             wl_fixed_t, wl_fixed_t) {
        auto& self = *static_cast<Impl*>(data);
        self.pointer_focus = surface == self.surface;
        self.pointer_serial = serial;
    }
    static void pointerLeave(void* data, wl_pointer*, uint32_t, wl_surface*) {
        static_cast<Impl*>(data)->pointer_focus = false;
    }
    static void pointerMotion(void*, wl_pointer*, uint32_t, wl_fixed_t, wl_fixed_t) {}
    static void pointerButton(void* data, wl_pointer*, uint32_t, uint32_t, uint32_t button,
                              uint32_t down) {
        auto& self = *static_cast<Impl*>(data);
        if (self.keyboard_focus && button >= 272 && button <= 276) {
            self.pending_mouse_report = true;
            self.state->key(1, button, down == WL_POINTER_BUTTON_STATE_PRESSED,
                            InputState::nowUs());
        }
    }
    static void pointerAxis(void* data, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value) {
        auto& self = *static_cast<Impl*>(data);
        if (self.keyboard_focus && axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
            self.wheel_axis += -wl_fixed_to_double(value) * 12;
            self.pending_mouse_report = true;
        }
    }
    static void pointerFrame(void* data, wl_pointer*) {
        auto& self = *static_cast<Impl*>(data);
        if (self.keyboard_focus && self.pending_mouse_report) {
            const auto time = InputState::nowUs();
            self.state->mouseReport(time);
            self.state->motion(
                0, 0, self.has_discrete_wheel ? self.wheel_discrete : self.wheel_axis, time);
        }
        self.pending_mouse_report = self.has_discrete_wheel = false;
        self.wheel_axis = self.wheel_discrete = 0;
    }
    static void axisSource(void*, wl_pointer*, uint32_t) {}
    static void axisStop(void*, wl_pointer*, uint32_t, uint32_t) {}
    static void axisDiscrete(void* data, wl_pointer*, uint32_t axis, int32_t steps) {
        auto& self = *static_cast<Impl*>(data);
        if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
            self.has_discrete_wheel = true;
            self.wheel_discrete += -steps * 120.0;
        }
    }
    static void relativeMotion(void* data, zwp_relative_pointer_v1*, uint32_t, uint32_t, wl_fixed_t,
                               wl_fixed_t, wl_fixed_t x, wl_fixed_t y) {
        auto& self = *static_cast<Impl*>(data);
        if (self.keyboard_focus && self.pointer_focus) {
            const auto time = InputState::nowUs();
            self.state->mouseReport(time);
            self.state->motion(wl_fixed_to_double(x), wl_fixed_to_double(y), 0, time);
        }
    }
    static void lockConfirmed(void* data, zwp_locked_pointer_v1*) {
        static_cast<Impl*>(data)->locked = true;
    }
    static void lockRevoked(void* data, zwp_locked_pointer_v1*) {
        auto& self = *static_cast<Impl*>(data);
        self.locked = false;
        self.state->capture(false, false, InputState::nowUs());
    }
    void runWayland(std::stop_token stop) {
        // 只包装 Qt 的 display，所有新建 proxy 都路由到本队列；不改变 Qt 自有 proxy 队列。
        queue = wl_display_create_queue(display);
        auto* wrapper = static_cast<wl_display*>(wl_proxy_create_wrapper(display));
        wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(wrapper), queue);
        registry = wl_display_get_registry(wrapper);
        wl_proxy_wrapper_destroy(wrapper);
        static const wl_registry_listener listener{global, globalRemoved};
        wl_registry_add_listener(registry, &listener, this);
        bool reported = false;
        const auto started = InputState::nowUs();
        while (!stop.stop_requested() && wl_display_get_error(display) == 0) {
            if (wl_display_dispatch_queue_pending(display, queue) < 0) break;
            if (pointer && relative_manager && !relative) {
                relative =
                    zwp_relative_pointer_manager_v1_get_relative_pointer(relative_manager, pointer);
                static const zwp_relative_pointer_v1_listener motion{relativeMotion};
                zwp_relative_pointer_v1_add_listener(relative, &motion, this);
            }
            if (relative && constraints && !reported) {
                state->backend("Wayland 未加速相对输入 / 独立事件队列");
                reported = true;
            }
            if (!reported && InputState::nowUs() - started > 2000000)
                state->backend("Wayland", "等待相对指针/指针约束协议；当前无法控制捕获");
            const auto [mode, wants_lock] = state->requested();
            const bool active = mode != InputState::Mode::Off && keyboard_focus;
            if (active && wants_lock && pointer_focus && constraints && relative && !lock) {
                lock = zwp_pointer_constraints_v1_lock_pointer(
                    constraints, surface, pointer, nullptr,
                    ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
                static const zwp_locked_pointer_v1_listener listener{lockConfirmed, lockRevoked};
                zwp_locked_pointer_v1_add_listener(lock, &listener, this);
                wl_pointer_set_cursor(pointer, pointer_serial, nullptr, 0, 0);
            } else if ((!active || !wants_lock) && lock) {
                zwp_locked_pointer_v1_destroy(lock);
                lock = nullptr;
                locked = false;
            }
            state->capture(active, locked, InputState::nowUs());
            // prepare_read_queue 与 Qt 的读取线程协作；有待派发事件时先派发，绝不抢读 Qt 队列。
            if (wl_display_prepare_read_queue(display, queue) != 0) continue;
            const int flush = wl_display_flush(display);
            if (flush < 0 && errno != EAGAIN) {
                wl_display_cancel_read(display);
                break;
            }
            pollfd fd{wl_display_get_fd(display), short(POLLIN | (flush < 0 ? POLLOUT : 0)), 0};
            const int result = poll(&fd, 1, 2);
            if (result > 0 && (fd.revents & POLLIN)) {
                if (wl_display_read_events(display) < 0) break;
            } else
                wl_display_cancel_read(display);
        }
        state->capture(false, false, InputState::nowUs());
        if (!reported) state->backend("Wayland", "相对指针/指针约束不可用，无法进入控制捕获");
        if (lock) zwp_locked_pointer_v1_destroy(lock);
        if (relative) zwp_relative_pointer_v1_destroy(relative);
        if (pointer) wl_pointer_destroy(pointer);
        if (keyboard) wl_keyboard_destroy(keyboard);
        if (seat) wl_seat_destroy(seat);
        if (constraints) zwp_pointer_constraints_v1_destroy(constraints);
        if (relative_manager) zwp_relative_pointer_manager_v1_destroy(relative_manager);
        wl_registry_destroy(registry);
        wl_display_flush(display);
        wl_event_queue_destroy(queue);
    }
};

NativeInput::NativeInput(QWindow& window, std::shared_ptr<InputState> state) {
    const auto platform = QGuiApplication::platformName();
    if (platform == "xcb") {
        impl_ = std::make_unique<Impl>(state);
        impl_->target = window.winId();
        impl_->thread =
            std::jthread([impl = impl_.get()](std::stop_token stop) { impl->runX11(stop); });
    } else if (platform.startsWith("wayland")) {
        auto* native = QGuiApplication::platformNativeInterface();
        auto* display =
            static_cast<wl_display*>(native->nativeResourceForIntegration("wl_display"));
        auto* surface =
            static_cast<wl_surface*>(native->nativeResourceForWindow("surface", &window));
        if (!display || !surface) {
            state->backend("Wayland", "Qt 原生 display/surface 尚未就绪");
            return;
        }
        impl_ = std::make_unique<Impl>(state);
        impl_->display = display;
        impl_->surface = surface;
        impl_->thread =
            std::jthread([impl = impl_.get()](std::stop_token stop) { impl->runWayland(stop); });
    } else
        state->backend("无原生输入", "当前显示插件不能捕获键鼠");
}
NativeInput::~NativeInput() = default;
