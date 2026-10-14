"""Inspect and exercise NOCTRA's native window only. Run while its menu is open."""
import ctypes as C
from ctypes import wintypes as W
import json

U = C.WinDLL("user32", use_last_error=True)
K = C.WinDLL("kernel32", use_last_error=True)
CALLBACK = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
U.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
U.GetWindowLongPtrW.argtypes = [W.HWND, C.c_int]
U.GetWindowLongPtrW.restype = C.c_ssize_t
U.GetWindowRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
U.GetClientRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
U.ClientToScreen.argtypes = [W.HWND, C.POINTER(W.POINT)]
U.SendMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
U.SendMessageW.restype = W.LPARAM
U.SetWindowPos.argtypes = [W.HWND, W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.UINT]
U.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
U.GetDpiForWindow.argtypes = [W.HWND]
K.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
K.OpenProcess.restype = W.HANDLE
K.QueryFullProcessImageNameW.argtypes = [W.HANDLE, W.DWORD, W.LPWSTR, C.POINTER(W.DWORD)]
K.CloseHandle.argtypes = [W.HANDLE]

windows = []
@CALLBACK
def collect(window, _):
    text = C.create_unicode_buffer(256)
    U.GetWindowTextW(window, text, len(text))
    if text.value == "NOCTRA - External":
        windows.append(window)
    return True

U.EnumWindows(collect, 0)
if len(windows) != 1:
    raise RuntimeError("Expected exactly one NOCTRA menu window")
window = windows[0]
pid = W.DWORD()
U.GetWindowThreadProcessId(window, C.byref(pid))
handle = K.OpenProcess(0x1000, False, pid.value)
if not handle:
    raise C.WinError(C.get_last_error())
try:
    path = C.create_unicode_buffer(32768)
    size = W.DWORD(len(path))
    if not K.QueryFullProcessImageNameW(handle, 0, path, C.byref(size)) or not path.value.lower().endswith("\\noctraexternal\\dist\\noctra.exe"):
        raise RuntimeError("Window is not owned by this workspace build")
finally:
    K.CloseHandle(handle)

rect, client, origin = W.RECT(), W.RECT(), W.POINT()
if not U.GetWindowRect(window, C.byref(rect)) or not U.GetClientRect(window, C.byref(client)) or not U.ClientToScreen(window, C.byref(origin)):
    raise C.WinError(C.get_last_error())
style = U.GetWindowLongPtrW(window, -16)
extended = U.GetWindowLongPtrW(window, -20)
assert extended & 8, "menu is not always on top"
assert style & 0x00C00000 == 0, "native caption remains"
assert style & 0x00040000, "native sizing frame missing"
assert origin.x == rect.left and origin.y == rect.top, "non-client top bar remains"
assert client.right == rect.right-rect.left and client.bottom == rect.bottom-rect.top
scale = U.GetDpiForWindow(window)/96

def hit(x, y):
    screen_x, screen_y = rect.left+x, rect.top+y
    return U.SendMessageW(window, 0x84, 0, ((screen_y & 0xffff) << 16) | (screen_x & 0xffff))

assert hit(1, 1) == 13, "top-left resize hit test failed"
assert hit(int(100*scale), int(25*scale)) == 2, "header drag hit test failed"
assert hit(client.right-int(40*scale), int(30*scale)) == 1, "title controls not client-hit-testable"
assert hit(int(300*scale), int(200*scale)) == 1, "content unexpectedly draggable"
old_width, old_height = rect.right-rect.left, rect.bottom-rect.top
try:
    if not U.SetWindowPos(window, None, 0, 0, int(1000*scale), int(760*scale), 0x16):
        raise C.WinError(C.get_last_error())
    C.windll.kernel32.Sleep(250)
    resized = W.RECT()
    U.GetClientRect(window, C.byref(resized))
    assert resized.right == int(1000*scale) and resized.bottom == int(760*scale), "native resize did not apply"
finally:
    if not U.SetWindowPos(window, None, 0, 0, old_width, old_height, 0x16):
        raise C.WinError(C.get_last_error())
print(json.dumps({"pid": pid.value, "caption_removed": True, "always_on_top": True, "full_client_area": True,
                  "drag_hit_test": True, "resize_hit_test": True, "control_hit_test": True,
                  "resize_roundtrip": True, "dpi": int(scale*96)}, indent=2))
