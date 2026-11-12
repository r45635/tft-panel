# PlatformIO pre-script (ESP32-P4 boards): fetches the ESP32-C6 coprocessor firmware that matches
# the esp_hosted version inside the pinned Arduino core, verifies it, and leaves it where
# platformio.ini embeds it (assets/hosted/esp32c6.bin, not in git). The firmware installs it on
# the C6 at boot when the C6 runs an older one (src/net/coprocessor.cpp).
#
# When the platform is updated: Arduino logs the expected version and URL at boot
# ("Update URL: .../esp32c6-vX.Y.Z.bin"); update VERSION and SHA256 here.
Import("env")  # noqa: F821

import hashlib
import os
import urllib.request

VERSION = "2.12.13"
SHA256 = "f8509949826c197a4fa3c7958943b4e8985cffdfdf4204c2847d8953905121c9"
URL = f"https://espressif.github.io/arduino-esp32/hosted/esp32c6-v{VERSION}.bin"

dst = os.path.join(env.subst("$PROJECT_DIR"), "assets", "hosted", "esp32c6.bin")  # noqa: F821


def digest(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


if not os.path.exists(dst) or digest(dst) != SHA256:
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    print(f"hosted_fw: downloading ESP32-C6 coprocessor firmware v{VERSION}")
    tmp = dst + ".part"
    urllib.request.urlretrieve(URL, tmp)
    got = digest(tmp)
    if got != SHA256:
        os.remove(tmp)
        raise SystemExit(f"hosted_fw: {URL} has SHA-256 {got}, expected {SHA256}")
    os.replace(tmp, dst)
