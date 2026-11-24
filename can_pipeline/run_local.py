"""Start the local ECU source in the background and run the Windows logger."""
import argparse
import json
import queue
import subprocess
import sys
import threading
from pathlib import Path

import pipeline
from local_simulator import HOST, PORT


HERE = Path(__file__).resolve().parent
RUN_DIR = HERE / "local_run"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wsl", action="store_true", help="run the producer in the default WSL distribution")
    args = parser.parse_args()
    command = (["wsl.exe", "--cd", str(HERE), "--exec", "python3"] if args.wsl else [sys.executable])
    command += ["-u", "local_simulator.py", "--managed"]
    try:
        producer = subprocess.Popen(command, cwd=HERE, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                    creationflags=getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0))
    except OSError as e:
        parser.exit(1, f"Cannot start the simulator: {e}\n")
    try:
        # Wait for this child to bind, so a previously running source cannot be mistaken for ours.
        ready = queue.Queue()
        threading.Thread(target=lambda: ready.put(producer.stdout.readline()), daemon=True).start()
        try:
            banner = ready.get(timeout=30).decode(errors="replace").replace("\x00", "").strip()
        except queue.Empty:
            parser.exit(1, "Source startup timed out. Check the WSL distribution or use native mode.\n")
        if banner != f"Listening on {HOST}:{PORT}":
            parser.exit(1, f"Source failed to start. Check port {PORT} and the selected runtime. {banner}\n")
        print(banner, flush=True)
        RUN_DIR.mkdir(exist_ok=True)
        profile = RUN_DIR / "config.json"
        if not profile.exists():
            cfg = json.loads((HERE / "config.json").read_text(encoding="utf-8"))
            # Older hardware config files predate the explicit simulator ID.
            simulator_device_id = cfg.pop("simulator_device_id", "EC000001")
            cfg.update(serial_port=f"socket://{HOST}:{PORT}", web_host=HOST, web_port=8081,
                       upload_enabled=False, session_from_bucket=True, device_id=simulator_device_id,
                       after_upload="archive", archive_dir="archive", first_session=1, simulator_controls=True)
            tmp = profile.with_suffix(".tmp")
            tmp.write_text(json.dumps(cfg, indent=2), encoding="utf-8")
            tmp.replace(profile)
        return pipeline.main(["--config", str(profile), "--data-dir", str(RUN_DIR)])
    finally:
        producer.stdin.close()
        try:
            producer.wait(timeout=5)
        except subprocess.TimeoutExpired:
            producer.kill()
            producer.wait()
        producer.stdout.close()


if __name__ == "__main__":
    sys.exit(main())
