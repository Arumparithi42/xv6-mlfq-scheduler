"""Boot xv6 in QEMU, type shell commands and capture their output.

Used by run_experiments.py and run_tests.py. Must be run from the
repository root.
"""

import os
import subprocess
import time

# QEMU options for deterministic runs: the virtual clock advances with
# the number of executed instructions (1 ns per instruction) instead of
# host time, and idle periods are skipped. Repeated runs then produce
# the same schedule, independent of host speed and load.
ICOUNT = "-icount shift=0,sleep=off"


class XV6:
    def __init__(self, makevars=None, icount=False, boot_timeout=60):
        self.makevars = [f"{k}={v}" for k, v in (makevars or {}).items()]
        env = dict(os.environ)
        env["QEMUEXTRA"] = ICOUNT if icount else ""
        self.build(env)
        self.proc = subprocess.Popen(
            ["make", "-s"] + self.makevars + ["qemu"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, env=env)
        os.set_blocking(self.proc.stdout.fileno(), False)
        self.out = b""
        if not self._wait_for(b"init: starting sh", boot_timeout):
            self.close()
            raise RuntimeError("xv6 did not boot:\n" + self.text())
        self._wait_for(b"$ ", 5)

    def build(self, env):
        subprocess.run(["make", "-s"] + self.makevars +
                       ["kernel/kernel", "fs.img"], check=True, env=env,
                       stdout=subprocess.DEVNULL)

    def _pump(self):
        try:
            data = os.read(self.proc.stdout.fileno(), 1 << 16)
        except BlockingIOError:
            data = None
        if data:
            self.out += data
            return True
        time.sleep(0.02)
        return False

    def _wait_for(self, pattern, timeout, start=0):
        end = time.time() + timeout
        while time.time() < end:
            if pattern in self.out[start:]:
                return True
            self._pump()
        return pattern in self.out[start:]

    def run(self, cmd, timeout=600):
        """Type cmd and return its output (up to the next prompt)."""
        start = len(self.out)
        self.proc.stdin.write(cmd.encode() + b"\n")
        self.proc.stdin.flush()
        # The shell echoes the command, then prints "$ " when done.
        if not self._wait_for(b"\n$ ", timeout, start):
            raise RuntimeError(f"timeout running {cmd!r}:\n" +
                               self.out[start:].decode(errors="replace"))
        out = self.out[start:].decode(errors="replace")
        return out[:out.rfind("\n$ ")] + "\n"

    def send(self, data, wait=1.0):
        """Send raw input (e.g. b"\\x10" for Ctrl-P); return new output."""
        start = len(self.out)
        self.proc.stdin.write(data)
        self.proc.stdin.flush()
        end = time.time() + wait
        while time.time() < end:
            self._pump()
        return self.out[start:].decode(errors="replace")

    def text(self):
        return self.out.decode(errors="replace")

    def close(self):
        self.proc.terminate()
        try:
            self.proc.wait(5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
        # "make qemu" does not always pass the signal on to QEMU.
        subprocess.run(["pkill", "-f", "qemu-system-riscv64.*kernel/kernel"],
                       stderr=subprocess.DEVNULL)
        time.sleep(0.5)
