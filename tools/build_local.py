#!/usr/bin/env python3
"""Build ESP32 dan simpan firmware/log. Tidak mengunggah ke perangkat.

Pemakaian dari folder mana pun:
  python3 tools/build_local.py --check
  python3 tools/build_local.py
"""
import argparse
import datetime as dt
import json
from pathlib import Path
import shutil
import subprocess
import sys
import venv

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Periksa berkas lokal saja; tanpa unduhan/build")
    args = parser.parse_args()
    required = ["platformio.ini", "wokwi.toml", "diagram.json", "wokwi/sketch.ino",
                "wokwi/game_logic.h", "wokwi/model_data.h", "wokwi/libraries.txt",
                "wokwi/diagram.json"]
    missing = [name for name in required if not (ROOT / name).is_file()]
    if missing:
        print("Berkas tidak lengkap: " + ", ".join(missing), file=sys.stderr)
        return 2
    root_diagram = json.loads((ROOT / "diagram.json").read_text(encoding="utf-8"))
    browser_diagram = json.loads(
        (ROOT / "wokwi/diagram.json").read_text(encoding="utf-8"))
    if root_diagram != browser_diagram:
        print("diagram.json root dan wokwi/diagram.json tidak identik.", file=sys.stderr)
        return 2
    print("Pemeriksaan berkas lokal: LULUS", flush=True)
    if args.check:
        print("Mode --check tidak mengompilasi firmware dan tidak membuktikan kompatibilitas library.")
        return 0

    reports = ROOT / "reports"
    reports.mkdir(exist_ok=True)
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    log_path = reports / f"build_local_{stamp}.log"
    result_path = reports / "build_local_status.json"
    env_dir = ROOT / ".build-env"
    env_python = env_dir / ("Scripts/python.exe" if sys.platform == "win32" else "bin/python")
    status = {"started_utc": stamp, "build_succeeded": False,
              "simulation_tested": False, "log": log_path.name}

    def run(command, log):
        log.write("\nCOMMAND " + repr(command) + "\n")
        log.flush()
        process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True, bufsize=1)
        assert process.stdout is not None
        for line in process.stdout:
            print(line, end="", flush=True)
            log.write(line)
            log.flush()
        code = process.wait()
        if code:
            raise RuntimeError(f"Perintah gagal dengan exit code {code}")

    try:
        with log_path.open("w", encoding="utf-8") as log:
            log.write(f"Suit TinyML build {stamp}\nPython: {sys.version}\n")
            if not env_python.is_file():
                print("Membuat environment build terpisah...", flush=True)
                venv.EnvBuilder(with_pip=True).create(env_dir)
            run([str(env_python), "-m", "pip", "install", "--disable-pip-version-check",
                 "platformio==6.1.19"], log)
            run([str(env_python), "-m", "platformio", "run", "-e", "esp32dev"], log)
            build = ROOT / ".pio/build/esp32dev"
            output = ROOT / "firmware"
            output.mkdir(exist_ok=True)
            for name in ["firmware.bin", "firmware.elf", "bootloader.bin", "partitions.bin"]:
                source = build / name
                if name in ["firmware.bin", "firmware.elf"] and not source.is_file():
                    raise RuntimeError(f"Build tidak menghasilkan {name}")
                if source.is_file():
                    shutil.copy2(source, output / name)
            status["build_succeeded"] = True
            status["firmware_directory"] = "firmware"
            print("\nBUILD LULUS. Hasil ada di folder firmware/.")
            print("Build lulus belum sama dengan game lulus uji simulator; ikuti bagian pengujian di README.md.")
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        status["error"] = str(exc)
        print(f"\nBUILD BELUM BERHASIL: {exc}\nLog: {log_path}", file=sys.stderr)
        with log_path.open("a", encoding="utf-8") as log:
            log.write("\nERROR: " + str(exc) + "\n")
    finally:
        result_path.write_text(json.dumps(status, indent=2), encoding="utf-8")
    return 0 if status["build_succeeded"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
