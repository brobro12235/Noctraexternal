"""Read-only candidate-offset diagnostic. Never writes to the client process."""
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
from pathlib import Path
import struct
import sys
import math
import time


def instance_class(h, a):
    return string(h, u64(h, u64(h, a + 0x18) + 8))


def instance_name(h, a):
    return string(h, u64(h, a + 0x70) + 8)


def children_of(h, a):
    container = u64(h, a + 0x78)
    start, end = struct.unpack("<QQ", read(h, container, 16))
    if end < start or (end-start) % 16 or end-start > 4096 * 16:
        raise ValueError("invalid children collection")
    return [u64(h, start + i) for i in range(0, end-start, 16)]


def inspect_scene(h, base, dm):
    services = {instance_class(h, a): a for a in children_of(h, dm)}
    players = services["Players"]
    local = u64(h, players + 0x120)
    workspace = services["Workspace"]
    camera = u64(h, workspace + 0x4A8)
    if instance_class(h, local) != "Player" or instance_class(h, camera) != "Camera":
        raise ValueError("local player/camera identity mismatch")
    fov = struct.unpack("<f", read(h, camera + 0x130, 4))[0]
    cam_pos = struct.unpack("<3f", read(h, camera + 0xEC, 12))
    ve = u64(h, base + 0x8656E40)
    matrix = struct.unpack("<16f", read(h, ve + 0x1B0, 64))
    dims = struct.unpack("<2f", read(h, ve + 0xB10, 8))
    entries = [a for a in children_of(h, players) if instance_class(h, a) == "Player"]
    local_models = []
    for offset in range(0x200, 0x350, 8):
        try:
            candidate = u64(h, local + offset)
            if instance_class(h, candidate) == "Model":
                local_models.append({"offset": hex(offset), "child_count": len(children_of(h, candidate))})
        except (OSError, ValueError, UnicodeError):
            pass
    characters = []
    problems = []
    teams = []
    for player in entries:
        team = u64(h, player + 0x2C8)
        neutral = read(h, player + 0x3BC, 1)[0]
        color = struct.unpack("<I", read(h, player + 0x3A0, 4))[0]
        teams.append({"local": player == local, "team": hex(team), "team_class": instance_class(h, team) if team else None, "neutral": neutral, "team_color": color})
    for player in entries[:256]:
        model = u64(h, player + 0x288)
        if not model:
            continue
        try:
            parts = children_of(h, model)
            identities = [(instance_class(h, a), instance_name(h, a)) for a in parts]
            humanoid = next(a for a in parts if instance_class(h, a) == "Humanoid")
            head = next(a for a in parts if instance_name(h, a) == "Head")
            primitive = u64(h, head + 0x178)
            pos = struct.unpack("<3f", read(h, primitive + 0xD4, 12))
            size = struct.unpack("<3f", read(h, primitive + 0x1BC, 12))
            health = struct.unpack("<f", read(h, humanoid + 0x180, 4))[0]
            max_health = struct.unpack("<f", read(h, humanoid + 0x198, 4))[0]
            characters.append({"head_position": pos, "head_size": size, "health": health,
                               "max_health": max_health, "local": player == local})
        except (StopIteration, OSError, ValueError, UnicodeError) as e:
            problems.append({"model_class": instance_class(h, model), "part_identities": identities if 'identities' in locals() else [], "error": str(e)})
            continue
    coherent = (0 < fov < math.pi and all(math.isfinite(v) for v in matrix + cam_pos)
                and any(abs(v) > 0.001 for v in matrix) and 100 <= dims[0] <= 16384 and 100 <= dims[1] <= 16384
                and bool(characters) and all(0 <= c["health"] <= c["max_health"] <= 1e8
                and all(math.isfinite(v) and abs(v) < 1e7 for v in c["head_position"])
                and all(0 < v < 1000 for v in c["head_size"]) for c in characters))
    return {"player_count": len(entries), "character_count": len(characters), "camera_fov_radians": fov,
            "camera_position": cam_pos, "dimensions": dims, "view_matrix": matrix,
            "characters": characters, "teams": teams, "local_model_candidates": local_models, "problems": problems, "coherent": coherent}


K = C.WinDLL("kernel32", use_last_error=True)
P = C.WinDLL("psapi", use_last_error=True)
K.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
K.OpenProcess.restype = W.HANDLE
K.CloseHandle.argtypes = [W.HANDLE]
K.ReadProcessMemory.argtypes = [W.HANDLE, C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
K.ReadProcessMemory.restype = W.BOOL
K.QueryFullProcessImageNameW.argtypes = [W.HANDLE, W.DWORD, W.LPWSTR, C.POINTER(W.DWORD)]
P.EnumProcesses.argtypes = [C.POINTER(W.DWORD), W.DWORD, C.POINTER(W.DWORD)]
P.EnumProcessModulesEx.argtypes = [W.HANDLE, C.POINTER(C.c_void_p), W.DWORD, C.POINTER(W.DWORD), W.DWORD]


def read(handle, address, size):
    if not 0x10000 <= address < 0x0000800000000000 or not 0 < size <= 65536:
        raise ValueError("invalid read bounds")
    buffer = C.create_string_buffer(size)
    count = C.c_size_t()
    if not K.ReadProcessMemory(handle, address, buffer, size, C.byref(count)) or count.value != size:
        raise OSError(C.get_last_error(), f"ReadProcessMemory failed at {address:#x}, bytes={count.value}/{size}")
    return buffer.raw


def u64(h, a):
    return struct.unpack("<Q", read(h, a, 8))[0]


def string(h, a):
    size, capacity = struct.unpack("<QQ", read(h, a + 0x10, 16))
    if size > 128 or capacity < size or capacity > 1048576:
        raise ValueError(f"unverified string layout: size={size}, capacity={capacity}")
    return read(h, u64(h, a) if capacity >= 16 else a, size).decode("utf-8", errors="strict") if size else ""


def run():
    ids = (W.DWORD * 4096)()
    used = W.DWORD()
    if not P.EnumProcesses(ids, C.sizeof(ids), C.byref(used)):
        raise C.WinError(C.get_last_error())
    for pid in ids[:used.value // 4]:
        h = K.OpenProcess(0x410, False, pid)  # QUERY_INFORMATION | VM_READ only
        if not h:
            continue
        try:
            name = C.create_unicode_buffer(32768)
            length = W.DWORD(len(name))
            if not K.QueryFullProcessImageNameW(h, 0, name, C.byref(length)) or Path(name.value).name.lower() != "robloxplayerbeta.exe":
                continue
            modules = (C.c_void_p * 1024)()
            needed = W.DWORD()
            if not P.EnumProcessModulesEx(h, modules, C.sizeof(modules), C.byref(needed), 3):
                raise C.WinError(C.get_last_error())
            base = modules[0]
            result = {"pid": pid, "build": Path(name.value).parent.name,
                      "sha256": hashlib.sha256(Path(name.value).read_bytes()).hexdigest(),
                      "module_base": hex(base), "candidate_build": "version-cec3ad5889b447cf", "globals": {}}
            for key, rva in {"fake_data_model": 0x8BCFD50, "scheduler": 0x8B79128, "visual_engine": 0x8656E40}.items():
                try:
                    ptr = u64(h, base + rva)
                    head = read(h, ptr, 32)
                    result["globals"][key] = {"pointer": hex(ptr), "readable_header": len(head) == 32}
                except (OSError, ValueError) as e:
                    result["globals"][key] = {"error": str(e)}
            try:
                fake = u64(h, base + 0x8BCFD50)
                dm = u64(h, fake + 0x1F8)
                descriptor = u64(h, dm + 0x18)
                class_name = string(h, u64(h, descriptor + 8))
                place_id = u64(h, dm + 0x188)
                children = u64(h, dm + 0x78)
                start, end = struct.unpack("<QQ", read(h, children, 16))
                if end < start or (end - start) % 16 or end - start > 16 * 4096:
                    raise ValueError("invalid candidate children collection")
                result["data_model"] = {"class_name": class_name, "place_id": place_id, "child_count": (end-start)//16}
                result["samples"] = []
                for _ in range(3):
                    result["samples"].append(inspect_scene(h, base, dm))
                    time.sleep(0.3)
            except (OSError, ValueError, UnicodeError) as e:
                result["data_model"] = {"error": str(e)}
            if "--teams" in sys.argv:
                print(json.dumps({"place": result.get("data_model", {}).get("place_id"), "teams": result.get("samples", [{}])[0].get("teams", [])}, indent=2))
            else:
                print(json.dumps(result, indent=2))
            return 0 if result.get("data_model", {}).get("class_name") == "DataModel" and all(s["coherent"] for s in result.get("samples", [])) and len(result.get("samples", [])) == 3 else 2
        finally:
            K.CloseHandle(h)
    print(json.dumps({"error": "No readable RobloxPlayerBeta.exe process found; ordinary access only."}))
    return 3


if __name__ == "__main__":
    sys.exit(run())
