Import("env")

import os
import subprocess


def _resolve_partitions_csv():
    partitions = env.GetProjectOption("board_build.partitions", "")
    if not partitions:
        raise RuntimeError("Missing `board_build.partitions` in platformio.ini")

    candidates = [
        os.path.join(env.subst("$PROJECT_DIR"), partitions),
    ]

    framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
    if framework_dir:
        candidates.append(os.path.join(framework_dir, "tools", "partitions", partitions))

    for candidate in candidates:
        if os.path.isfile(candidate):
            return candidate

    raise RuntimeError("Cannot find partitions CSV: %s" % partitions)


def _find_nvs_region(partitions_csv):
    with open(partitions_csv, "r", encoding="utf-8") as fp:
        for raw_line in fp:
            line = raw_line.split("#", 1)[0].strip()
            if not line:
                continue

            columns = [part.strip() for part in line.split(",")]
            if len(columns) < 5:
                continue

            if columns[0] == "nvs":
                return columns[3], columns[4]

    raise RuntimeError("Cannot find `nvs` partition in %s" % partitions_csv)


def _erase_nvs_before_upload(source, target, env):
    upload_protocol = env.subst("$UPLOAD_PROTOCOL")
    if upload_protocol not in ("", "esptool"):
        print("Skipping NVS erase for upload protocol: %s" % upload_protocol)
        return

    env.AutodetectUploadPort()
    upload_port = env.subst("$UPLOAD_PORT")
    if not upload_port:
        raise RuntimeError("Upload port not found for NVS erase")

    partitions_csv = _resolve_partitions_csv()
    nvs_offset, nvs_size = _find_nvs_region(partitions_csv)

    esptool = os.path.join(env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py")
    command = [
        env.subst("$PYTHONEXE"),
        esptool,
        "--chip",
        env.subst("$BOARD_MCU"),
        "--port",
        upload_port,
        "--baud",
        env.subst("$UPLOAD_SPEED"),
        "--before",
        "default_reset",
        "--after",
        "hard_reset",
        "erase_region",
        nvs_offset,
        nvs_size,
    ]

    print("Erasing NVS region %s (%s bytes) on %s" % (nvs_offset, nvs_size, upload_port))
    subprocess.check_call(command)


env.AddPreAction("upload", _erase_nvs_before_upload)
