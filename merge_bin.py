# Post-build: merge bootloader + partitions + boot_app0 + app into cardcore.bin
# for web flashers (ESPConnect, esptool-js): flash it at offset 0x0.
# Also stamps `git describe` into CARDCORE_VERSION (shown on the boot screen).
import os
import subprocess
Import("env", "projenv")

try:
    version = subprocess.check_output(["git", "describe", "--tags", "--always", "--dirty"],
                                      cwd=env.subst("$PROJECT_DIR"), text=True,
                                      stderr=subprocess.DEVNULL).strip()
except Exception:
    version = "dev"
projenv.Append(CPPDEFINES=[("CARDCORE_VERSION", env.StringifyMacro(version))])


def merge(source, target, env):
    out = os.path.join(env.subst("$PROJECT_DIR"), "cardcore.bin")
    parts = [f"{off} \"{path}\"" for off, path in env["FLASH_EXTRA_IMAGES"]]
    parts.append(f"{env.subst('$ESP32_APP_OFFSET')} \"$BUILD_DIR/${{PROGNAME}}.bin\"")
    env.Execute(env.subst(
        f'"$PYTHONEXE" "$OBJCOPY" --chip esp32s3 merge_bin -o "{out}" '
        f'--flash_mode keep --flash_freq keep --flash_size keep {" ".join(parts)}'))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge)
