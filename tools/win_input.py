"""Sends real Windows input to a window, for exercising the SDL2 build.

Used by tools/test_sdl2_runtime.sh. Nothing here is part of the engine; it exists
because the SDL2 fixes that matter most (special keys, resize) cannot be reached
by running the engine headlessly and diffing mugen.log.
"""
import ctypes
import ctypes.wintypes as wt
import sys
import time

u32 = ctypes.windll.user32

INPUT_KEYBOARD = 1
KEYEVENTF_KEYUP = 0x0002
VK = {
    "up": 0x26, "down": 0x28, "left": 0x25, "right": 0x27,
    "esc": 0x1B, "escape": 0x1B, "enter": 0x0D, "space": 0x20, "f1": 0x70,
    "home": 0x24, "end": 0x23, "insert": 0x2D, "delete": 0x2E,
    "a": 0x41, "z": 0x5A, "lctrl": 0xA2, "pause": 0x13,
}


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wt.WORD), ("wScan", wt.WORD), ("dwFlags", wt.DWORD),
                ("time", wt.DWORD), ("dwExtraInfo", ctypes.POINTER(wt.ULONG))]


class INPUT(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [("ki", KEYBDINPUT)]
    _anonymous_ = ("u",)
    _fields_ = [("type", wt.DWORD), ("u", _U)]


def send(vk, up=False):
    inp = INPUT(type=INPUT_KEYBOARD)
    inp.ki = KEYBDINPUT(wVk=vk, wScan=0,
                        dwFlags=KEYEVENTF_KEYUP if up else 0,
                        time=0, dwExtraInfo=None)
    u32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT))


def find_window(substr):
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(hwnd, _):
        n = u32.GetWindowTextLengthW(hwnd)
        if n:
            buf = ctypes.create_unicode_buffer(n + 1)
            u32.GetWindowTextW(hwnd, buf, n + 1)
            if substr.lower() in buf.value.lower():
                found.append(hwnd)
        return True

    u32.EnumWindows(cb, 0)
    return found[0] if found else None


def main():
    action = sys.argv[1]
    hwnd = find_window(sys.argv[2] if len(sys.argv) > 2 else "M.U.G.E.N")
    if not hwnd:
        print("window not found")
        return 2
    u32.SetForegroundWindow(hwnd)
    time.sleep(0.3)
    if action == "keys":
        # Every key here is one SDL 1.2 numbered above 255, i.e. exactly the
        # class that corrupted memory before the translation fix.
        for name in sys.argv[3:]:
            vk = VK[name]
            send(vk)
            time.sleep(0.15)
            send(vk, up=True)
            time.sleep(0.15)
        print("sent", " ".join(sys.argv[3:]))
    elif action == "resize":
        rect = wt.RECT()
        u32.GetClientRect(hwnd, ctypes.byref(rect))
        w, h = (int(sys.argv[3]), int(sys.argv[4]))
        u32.MoveWindow(hwnd, 50, 50, w, h, True)
        print("resized to", w, h)
    else:
        print("unknown action")
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
