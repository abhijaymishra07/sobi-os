#!/usr/bin/env python3
"""Boot the debug ISO in QEMU, type shell commands, and record docs/img/demo.gif."""
import os, socket, subprocess, sys, tempfile, time
from PIL import Image

ISO = sys.argv[1] if len(sys.argv) > 1 else "build/debug/sobi.iso"
OUT = sys.argv[2] if len(sys.argv) > 2 else "docs/img/demo.gif"
SCRIPT = ["clear", "help", "ls", "cat", "cat | upper", "uptime", "hello"]
CROP = (0, 0, 760, 480)
KEYS = {" ": "spc", "\n": "ret", "|": "shift-backslash", "/": "slash", ".": "dot", "-": "minus"}

tmp = tempfile.mkdtemp()
sock_path = os.path.join(tmp, "mon.sock")
qemu = subprocess.Popen(["qemu-system-x86_64", "-M", "q35", "-m", "256M", "-no-reboot",
                         "-cdrom", ISO, "-display", "none", "-serial", "null",
                         "-monitor", f"unix:{sock_path},server,nowait"])
for _ in range(100):
    if os.path.exists(sock_path):
        break
    time.sleep(0.05)
mon = socket.socket(socket.AF_UNIX)
mon.connect(sock_path)
mon.settimeout(5)
banner = b""
while not banner.endswith(b"(qemu) "):
    banner += mon.recv(65536)

def cmd(c):
    mon.send((c + "\n").encode())
    data = b""
    while not data.endswith(b"(qemu) "):
        chunk = mon.recv(65536)
        if not chunk:
            break
        data += chunk

frames = []

def snap(ms):
    path = os.path.join(tmp, f"f{len(frames)}.ppm")
    cmd(f"screendump {path}")
    for _ in range(50):
        if os.path.exists(path) and os.path.getsize(path) > 0:
            break
        time.sleep(0.02)
    time.sleep(0.05)
    frames.append((Image.open(path).convert("RGB").crop(CROP), ms))

time.sleep(5)
snap(1800)
for line in SCRIPT:
    for i, ch in enumerate(line + "\n"):
        cmd("sendkey " + KEYS.get(ch, ch))
        time.sleep(0.05)
        if ch != "\n" and i % 2 == 1:
            snap(160)
    time.sleep(0.8)
    snap(1400)
    print("typed:", line, "frames:", len(frames), flush=True)
mon.send(b"quit\n")
qemu.wait(timeout=10)

pal = [f[0].convert("P", palette=Image.ADAPTIVE, colors=4) for f in frames]
pal[0].save(OUT, save_all=True, append_images=pal[1:], duration=[f[1] for f in frames],
            loop=0, optimize=True, disposal=1)
frames[-1][0].save(OUT.replace("demo.gif", "shell.png"))
print(OUT, len(frames), "frames", os.path.getsize(OUT) // 1024, "KiB")
