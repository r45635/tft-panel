#!/usr/bin/env python3
"""Gives a panel its Wi-Fi credentials over the serial console.

    python3 tools/set-wifi.py [/dev/cu.usbserial-XXXX]

For a board whose screen the firmware cannot drive yet, where the setup portal is unusable
because its access-point password is only ever shown on that screen. The password is typed with
the echo off, sent straight to the panel's `wifi` console command, and stored in its NVS; it is
never written to a file, a build flag or this script's output. The firmware's console does not
echo, so nothing comes back either.

A network name containing a space needs the setup portal instead: the console command splits its
arguments at the first space.
"""
import getpass
import os
import sys
import time

try:
    import serial
except ModuleNotFoundError:
    _pio = os.path.expanduser("~/.platformio/penv/bin/python")
    if os.path.exists(_pio) and os.environ.get("_TFT_RELAUNCHED") != "1":
        os.environ["_TFT_RELAUNCHED"] = "1"
        os.execv(_pio, [_pio, os.path.abspath(__file__)] + sys.argv[1:])
    sys.exit("This needs pyserial: pip3 install pyserial, or run it with PlatformIO's Python.")

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbserial-8310"


def main():
    ssid = input("Name of the Wi-Fi network: ").strip()
    if not ssid:
        return 1
    if " " in ssid:
        print("That name has a space in it, which this console command cannot carry.")
        print("Use the setup portal on the panel instead.")
        return 1
    password = getpass.getpass(f'Password for "{ssid}" (hidden): ')

    try:
        port = serial.Serial(PORT, 115200, timeout=0.2, dsrdtr=False, rtscts=False)
    except serial.SerialException as e:
        print(f"Cannot open {PORT}: {e}")
        print("A serial monitor holding that port is the usual reason.")
        return 1
    # Both lines low: on a CH340 board, asserting either one resets the panel.
    port.dtr = False
    port.rts = False
    time.sleep(0.2)
    port.reset_input_buffer()

    port.write(f"wifi {ssid} {password}\n".encode())
    port.flush()
    secret = password
    del password

    print("\nSent. The panel saves it and joins; what it says:\n")
    end = time.time() + 45
    text = ""
    joined = False
    while time.time() < end:
        chunk = port.read(512).decode("utf-8", "replace")
        if not chunk:
            continue
        text += chunk
        while "\n" in text:
            line, text = text.split("\n", 1)
            # The firmware does not echo, but never print a line back without checking.
            if secret and secret in line:
                line = line.replace(secret, "********")
            print("   " + line.rstrip())
            if "Wi-Fi up" in line:
                joined = True
            if "ip " in line and joined:
                end = time.time() + 3
    port.close()
    if joined:
        print("\nJoined. The panel will connect by itself from now on.")
        return 0
    print("\nIt did not report joining. A wrong password is the usual reason; run this again.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
