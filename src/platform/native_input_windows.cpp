#include <QGuiApplication>
#include <QWindow>
#include <array>
#include <thread>
#include <vector>

#include "platform/native_input.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct NativeInput::Impl {
    std::shared_ptr<InputState> state;
    HWND target;
    std::jthread thread;

    Impl(HWND window, std::shared_ptr<InputState> input) : state(std::move(input)), target(window) {
        thread = std::jthread([this](std::stop_token stop) { run(stop); });
    }
    ~Impl() {
        thread.request_stop();
        thread.join();
    }

    void input(HRAWINPUT handle) {
        UINT size = 0;
        if (GetRawInputData(handle, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 ||
            size > 65536 || size < sizeof(RAWINPUTHEADER))
            return;
        // 固定上限避免异常驱动报文导致无限分配；每个 WM_INPUT 均完整取出。
        std::vector<BYTE> bytes(size);
        if (GetRawInputData(handle, RID_INPUT, bytes.data(), &size, sizeof(RAWINPUTHEADER)) != size)
            return;
        const auto& raw = *reinterpret_cast<const RAWINPUT*>(bytes.data());
        const auto time = InputState::nowUs();
        const auto device = quint64(reinterpret_cast<uintptr_t>(raw.header.hDevice));
        if (raw.header.dwType == RIM_TYPEKEYBOARD &&
            size >= offsetof(RAWINPUT, data) + sizeof(RAWKEYBOARD)) {
            const auto& key = raw.data.keyboard;
            int code = key.MakeCode;
            if (key.VKey == VK_PAUSE) code = 119;
            if (key.VKey == VK_SNAPSHOT) code = 99;
            if (!code || code == 255) return;
            if (key.Flags & RI_KEY_E0) {
                switch (code) {
                    case 53:
                        code = 98;
                        break;
                    case 93:
                        code = 127;
                        break;
                    case 29:
                        code = 97;
                        break;
                    case 56:
                        code = 100;
                        break;
                    case 28:
                        code = 96;
                        break;
                    case 71:
                        code = 102;
                        break;
                    case 72:
                        code = 103;
                        break;
                    case 73:
                        code = 104;
                        break;
                    case 75:
                        code = 105;
                        break;
                    case 77:
                        code = 106;
                        break;
                    case 79:
                        code = 107;
                        break;
                    case 80:
                        code = 108;
                        break;
                    case 81:
                        code = 109;
                        break;
                    case 82:
                        code = 110;
                        break;
                    case 83:
                        code = 111;
                        break;
                    case 91:
                        code = 125;
                        break;
                    case 92:
                        code = 126;
                        break;
                    default:
                        break;
                }
            }
            state->key(device, code, !(key.Flags & RI_KEY_BREAK), time);
        } else if (raw.header.dwType == RIM_TYPEMOUSE &&
                   size >= offsetof(RAWINPUT, data) + sizeof(RAWMOUSE)) {
            const auto& mouse = raw.data.mouse;
            state->mouseReport(time);
            constexpr USHORT down[] = {RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_DOWN,
                                       RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_BUTTON_4_DOWN,
                                       RI_MOUSE_BUTTON_5_DOWN};
            constexpr USHORT up[] = {RI_MOUSE_LEFT_BUTTON_UP, RI_MOUSE_RIGHT_BUTTON_UP,
                                     RI_MOUSE_MIDDLE_BUTTON_UP, RI_MOUSE_BUTTON_4_UP,
                                     RI_MOUSE_BUTTON_5_UP};
            for (int i = 0; i < 5; ++i) {
                if (mouse.usButtonFlags & down[i]) state->key(device, 272 + i, true, time);
                if (mouse.usButtonFlags & up[i]) state->key(device, 272 + i, false, time);
            }
            // 绝对坐标设备不能伪装成原始相对鼠标；按钮/滚轮仍可测试。
            const bool relative = !(mouse.usFlags & MOUSE_MOVE_ABSOLUTE);
            state->motion(relative ? mouse.lLastX : 0, relative ? mouse.lLastY : 0,
                          mouse.usButtonFlags & RI_MOUSE_WHEEL ? SHORT(mouse.usButtonData) : 0,
                          time);
        }
    }
    void run(std::stop_token stop) {
        // 每进程同 usage 只有一个 RawInput 注册目标；不使用 NOLEGACY，保留 Qt 菜单输入。
        const auto instance = GetModuleHandleW(nullptr);
        const wchar_t* name = L"PIP.NativeInput";
        WNDCLASSW type{};
        type.lpfnWndProc = DefWindowProcW;
        type.hInstance = instance;
        type.lpszClassName = name;
        RegisterClassW(&type);
        const auto window =
            CreateWindowExW(0, name, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
        RAWINPUTDEVICE devices[] = {{1, 6, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, window},
                                    {1, 2, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, window}};
        if (!window || !RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE))) {
            state->backend("Windows Raw Input", QString("注册失败，Win32=%1").arg(GetLastError()));
            if (window) DestroyWindow(window);
            return;
        }
        state->backend("Windows Raw Input / 独立线程");
        bool was_active = false, clipped = false;
        while (!stop.stop_requested()) {
            const auto [mode, wants_lock] = state->requested();
            const bool active = mode != InputState::Mode::Off && GetForegroundWindow() == target &&
                                IsWindowVisible(target) && !IsIconic(target);
            bool locked = false;
            if (active && wants_lock) {
                RECT rect{};
                POINT origin{};
                if (GetClientRect(target, &rect) && ClientToScreen(target, &origin)) {
                    OffsetRect(&rect, origin.x, origin.y);
                    locked = rect.right > rect.left && rect.bottom > rect.top && ClipCursor(&rect);
                    if (!locked && clipped) ClipCursor(nullptr);
                    clipped = locked;
                }
            } else if (clipped) {
                ClipCursor(nullptr);
                clipped = false;
            }
            if (active && !was_active) {
                state->resetDevices();
                // OS 快照只用于恢复时抑制已按住键，正常状态仍逐个消费全部 RawInput。
                for (int scan = 1; scan < 128; ++scan) {
                    const auto vk = MapVirtualKeyW(scan, MAPVK_VSC_TO_VK_EX);
                    if (vk && (GetAsyncKeyState(vk) & 0x8000)) state->seedKey(0, scan);
                }
                if (GetAsyncKeyState(VK_RCONTROL) & 0x8000) state->seedKey(0, 97);
                for (int i = 0; i < 3; ++i) {
                    const int vk[] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON};
                    if (GetAsyncKeyState(vk[i]) & 0x8000) state->seedKey(0, 272 + i);
                }
            }
            was_active = active;
            state->capture(active, locked, InputState::nowUs());
            MSG message;
            // 有界处理批次保证持续输入下仍检查退出/焦点；剩余事件下一批继续处理。
            for (int count = 0; count < 512 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE);
                 ++count) {
                if (message.message == WM_INPUT && active && GetForegroundWindow() == target)
                    input(reinterpret_cast<HRAWINPUT>(message.lParam));
                if (message.message == WM_INPUT_DEVICE_CHANGE && message.wParam == GIDC_REMOVAL)
                    state->removeDevice(quint64(message.lParam));
                DispatchMessageW(&message);
            }
            MsgWaitForMultipleObjectsEx(0, nullptr, 2, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
        if (clipped) ClipCursor(nullptr);
        state->capture(false, false, InputState::nowUs());
        for (auto& device : devices) {
            device.dwFlags = RIDEV_REMOVE;
            device.hwndTarget = nullptr;
        }
        RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE));
        DestroyWindow(window);
    }
};

NativeInput::NativeInput(QWindow& window, std::shared_ptr<InputState> state) {
    if (QGuiApplication::platformName() != "windows") {
        state->backend("无原生输入", "当前显示插件不能捕获键鼠");
        return;
    }
    impl_ = std::make_unique<Impl>(reinterpret_cast<HWND>(window.winId()), std::move(state));
}
NativeInput::~NativeInput() = default;
