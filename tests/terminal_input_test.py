#!/usr/bin/env python3
import os, pty, select, subprocess, sys, termios, time
for value, expected in ((b'fixture-password', 0), (b'excessive', 1)):
    master, slave = pty.openpty()
    original = termios.tcgetattr(slave)
    child = subprocess.Popen([sys.argv[1]] + (["bounded"] if expected else []), stdin=slave, stdout=slave, stderr=slave)
    output = b''
    try:
        deadline = time.monotonic() + 5
        while b'Password: ' not in output and time.monotonic() < deadline:
            if select.select([master], [], [], .1)[0]: output += os.read(master, 4096)
        assert b'Password: ' in output
        assert not (termios.tcgetattr(slave)[3] & termios.ECHO)
        os.write(master, value + b'\n')
        deadline = time.monotonic() + 5
        while child.poll() is None and time.monotonic() < deadline:
            if select.select([master], [], [], .05)[0]: output += os.read(master, 4096)
        assert child.wait(timeout=1) == expected
        while select.select([master], [], [], .05)[0]: output += os.read(master, 4096)
        assert value not in output
        assert termios.tcgetattr(slave)[3] & termios.ECHO == original[3] & termios.ECHO
    finally:
        if child.poll() is None: child.kill(); child.wait()
        os.close(master); os.close(slave)
print('Terminal echo stayed off during input and was restored on success and rejection')
