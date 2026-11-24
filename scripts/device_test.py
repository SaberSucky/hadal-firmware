#!/usr/bin/env python3
"""Drive a Flipper over USB to test on the device, and catch crashes.

  python3 scripts/device_test.py STEP [STEP ...]
    c=<cli command>        run a CLI command, print its output
    k=<key>[:long]         press a key (up down left right ok back)
    K=<key,key,...>        several short presses, fast
    s=<name>               screenshot to build/device_shots/<name>.png (skipped when RAM is
                           low: a session and the screen stream need ~13 KB)
    S=<name>               screenshot even when low (the firmware refuses rather than crash)
    p=<secs>               sample free heap for that long (prints start/min/end)
    f=<secs>               redraws per second as a timeline (a frame is sent per redraw: an
                           animation that stops shows as 0s)
    w=<secs>               wait
    r                      reboot and time USB coming back

After the steps (or as soon as USB drops): waits for the Flipper to return, and
prints any crash report written since the start, decoded with addr2line.
Exit code 0 = no crash, 2 = crashed, 3 = not back on USB.

The Flipper must be unlocked: a PIN lock turns its USB off. With Hadal > Lock Screen >
Development on, its own restarts on USB (crashes, reboots, updates) come back unlocked.
Close qFlipper first: it holds the port. FLIPPER_PORT picks a port; else the first
Flipper found is used.
"""
import os, re, sys, time, struct, subprocess, zlib
import serial
from serial.tools import list_ports

PROMPT = b">: "
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHOTS = os.path.join(REPO, "build", "device_shots")
ELF = os.path.join(REPO, "build", "f7-firmware-C", "firmware.elf")
ADDR2LINE = os.path.join(
    REPO,
    "toolchain",
    "current",
    "bin",
    "arm-none-eabi-addr2line" + (".exe" if os.name == "nt" else ""),
)


def find_port():
    if os.environ.get("FLIPPER_PORT"):
        return os.environ["FLIPPER_PORT"]
    for p in list_ports.comports():
        if p.vid == 0x0483 and p.pid == 0x5740:
            return p.device
    raise serial.SerialException("no Flipper on USB (locked, or qFlipper holding it?)")


class Lost(Exception):
    pass


def open_port(wait=0.0):
    end = time.time() + wait
    while True:
        try:
            s = serial.Serial(find_port(), 230400, timeout=0.1)
            s.reset_input_buffer()
            return s
        except serial.SerialException:
            if time.time() >= end:
                raise
            time.sleep(0.25)


class Dev:
    def __init__(self, wait=0.0):
        self.s = open_port(wait)
        self.sync()

    def _io(self, fn, *a):
        try:
            return fn(*a)
        except serial.SerialException as e:
            raise Lost(str(e))

    def write(self, b):
        self._io(self.s.write, b)

    def read(self, n=4096):
        return self._io(self.s.read, n)

    def read_until(self, token, timeout):
        buf = b""
        end = time.time() + timeout
        while time.time() < end:
            buf += self.read()
            if token in buf[-(len(token) + 8):]:
                break
        return buf

    def sync(self):
        self.write(b"\x03")
        time.sleep(0.05)
        self._io(self.s.reset_input_buffer)
        self.write(b"\r")
        self.read_until(PROMPT, 2)

    def cli(self, cmd, timeout=10.0):
        self.write(cmd.encode() + b"\r")
        out = self.read_until(PROMPT, timeout).decode(errors="replace").replace("\r", "")
        lines = out.split("\n")
        if lines and cmd in lines[0]:
            lines = lines[1:]
        return "\n".join(l for l in lines if l.strip() != ">:").strip()

    def key(self, k, kind="short"):
        # one write: press, short|long, release
        cmds = [f"input send {k} press", f"input send {k} {kind}", f"input send {k} release"]
        for c in cmds:
            self.cli(c, 2)
            if kind == "long" and c.endswith("press"):
                time.sleep(0.4)

    def free(self):
        o = self.cli("free", 3)
        f = re.search(r"Free heap size: (\d+)", o)
        b = re.search(r"Maximum heap block: (\d+)", o)
        return (int(f.group(1)) if f else None, int(b.group(1)) if b else None)

    def close(self):
        try:
            self.s.close()
        except Exception:
            pass


# ---- minimal protobuf for the screen stream ---------------------------------
def varint(n):
    out = b""
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out += bytes([b | 0x80])
        else:
            return out + bytes([b])


def read_varint(buf, i):
    n = shift = 0
    while True:
        b = buf[i]
        i += 1
        n |= (b & 0x7F) << shift
        shift += 7
        if not b & 0x80:
            return n, i


def parse(buf):
    i, out = 0, {}
    while i < len(buf):
        tag, i = read_varint(buf, i)
        f, wt = tag >> 3, tag & 7
        if wt == 0:
            v, i = read_varint(buf, i)
        elif wt == 2:
            ln, i = read_varint(buf, i)
            v = buf[i:i + ln]
            i += ln
        elif wt == 5:
            v = buf[i:i + 4]; i += 4
        elif wt == 1:
            v = buf[i:i + 8]; i += 8
        else:
            break
        out[f] = v
    return out


def main_msg(cmd_id, field, payload=b""):
    body = varint(1 << 3) + varint(cmd_id) + varint((field << 3) | 2) + varint(len(payload)) + payload
    return varint(len(body)) + body


def screenshot(dev, path, scale=3):
    dev.write(b"start_rpc_session\r")
    got = dev.read_until(b"start_rpc_session\r\n", 2)
    time.sleep(0.05)
    got += dev.read()
    if b"Session start error" in got:
        print("   refused: not enough RAM for a session")
        dev.sync()
        return
    dev.write(main_msg(1, 20))
    buf, frame, end = b"", None, time.time() + 3
    while time.time() < end and frame is None:
        buf += dev.read(8192)
        while buf:
            try:
                ln, i = read_varint(buf, 0)
            except IndexError:
                break
            if len(buf) < i + ln:
                break
            m = parse(buf[i:i + ln])
            buf = buf[i + ln:]
            if 22 in m:
                frame = parse(m[22]).get(1)
                break
    dev.write(main_msg(2, 21))
    dev.write(main_msg(3, 19))  # stop session
    time.sleep(0.2)
    dev._io(dev.s.reset_input_buffer)
    dev.sync()
    if not frame:
        print("   no frame")
        return
    w, h = 128, 64
    on_c, off_c = b"\x1e\x1e\x1e", b"\xff\x8c\x29"
    raw = b""
    for y in range(h):
        line = b"".join((on_c if frame[(y // 8) * w + x] >> (y % 8) & 1 else off_c) * scale for x in range(w))
        raw += (b"\x00" + line) * scale

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w * scale, h * scale, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "wb").write(png)
    print("   saved", os.path.normpath(path))


def frame_timeline(dev, seconds):
    dev.write(b"start_rpc_session\r")
    dev.read_until(b"start_rpc_session\r\n", 2)
    dev.write(main_msg(1, 20))
    buf, t0, counts, last = b"", time.time(), {}, None
    while time.time() - t0 < seconds:
        buf += dev.read(8192)
        while buf:
            try:
                ln, i = read_varint(buf, 0)
            except IndexError:
                break
            if len(buf) < i + ln:
                break
            m = parse(buf[i:i + ln])
            buf = buf[i + ln:]
            if 22 in m:
                fr = parse(m[22]).get(1)
                if fr != last:
                    sec = int(time.time() - t0)
                    counts[sec] = counts.get(sec, 0) + 1
                    last = fr
    dev.write(main_msg(2, 21))
    dev.write(main_msg(3, 19))
    time.sleep(0.2)
    dev._io(dev.s.reset_input_buffer)
    dev.sync()
    line = "".join(str(min(counts.get(s, 0), 9)) for s in range(int(seconds)))
    print(f"   changed frames per second: {line}")


# ---- crash reports -----------------------------------------------------------
def crash_list(dev):
    o = dev.cli("storage list /ext/crash", 5)
    return sorted(re.findall(r"(crash_\d{8}_\d{6}\.txt)", o))


def decode(report):
    m = re.search(r"Call chain: ([0-9xA-Fa-f ]+)", report)
    addrs = (m.group(1).split() if m else [])
    r = re.search(r"Raised from: (0x[0-9A-Fa-f]+)", report)
    if r:
        addrs = [r.group(1)] + [a for a in addrs if a != r.group(1)]
    if not addrs or not os.path.exists(ELF):
        return ""
    try:
        out = subprocess.run([ADDR2LINE, "-f", "-p", "-e", ELF] + addrs, capture_output=True, text=True).stdout
    except OSError:
        return ""
    return "\n".join("   " + l.replace(REPO + "/", "").replace(REPO + "\\", "") for l in out.splitlines())


def report_crashes(dev, before):
    new = [f for f in crash_list(dev) if f not in before]
    for f in new:
        rep = dev.cli(f"storage read /ext/crash/{f}", 5)
        rep = re.sub(r"^Size: \d+\n", "", rep)
        print(f"\n!!! CRASH REPORT {f}\n{rep}\n--- decoded:\n{decode(rep)}")
    return new


def run(steps):
    dev = Dev()
    before = crash_list(dev)
    t0 = time.time()
    lost = False
    try:
        for a in steps:
            t, _, v = a.partition("=")
            tag = f"[{time.time() - t0:5.1f}s] {a}"
            if t == "c":
                print(f"{tag}\n{dev.cli(v, 30)}")
            elif t == "k":
                k, _, kind = v.partition(":")
                dev.key(k, kind or "short")
                time.sleep(0.15)
            elif t == "K":
                for k in v.split(","):
                    dev.key(k)
                    time.sleep(0.12)
            elif t in ("s", "S"):
                f, blk = dev.free()
                if t == "s" and blk is not None and blk < 16000:
                    print(f"{tag}: skipped, low RAM ({f} free, largest block {blk})")
                else:
                    print(tag)
                    screenshot(dev, os.path.join(SHOTS, v + ".png"))
            elif t == "w":
                time.sleep(float(v))
            elif t == "f":
                # redraws per second, as a timeline (the stream sends a frame per redraw)
                print(tag)
                frame_timeline(dev, float(v))
            elif t == "p":
                end, vals = time.time() + float(v), []
                while time.time() < end:
                    f, _ = dev.free()
                    if f:
                        vals.append(f)
                if vals:
                    print(f"{tag}: free start={vals[0]} min={min(vals)} end={vals[-1]} (n={len(vals)})")
            elif t == "r":
                dev.write(b"power reboot\r")
                dev.close()
                tr = time.time()
                time.sleep(1)
                dev = Dev(wait=60)
                print(f"{tag}: back on USB after {time.time() - tr:.1f}s")
    except Lost as e:
        lost = True
        print(f"\n!!! USB LOST at {time.time() - t0:.1f}s ({e}); waiting for the Flipper...")
        dev.close()
        tr = time.time()
        try:
            dev = Dev(wait=90)
            print(f"    back after {time.time() - tr:.1f}s; {dev.cli('uptime')}")
        except serial.SerialException:
            print("    not back after 90 s: frozen, or a screen waiting for a key")
            return 3
    new = report_crashes(dev, before)
    dev.close()
    return 2 if (new or lost) else 0


if __name__ == "__main__":
    sys.exit(run(sys.argv[1:]))
