import argparse
import csv
import time
from datetime import datetime
from pathlib import Path

import serial

EXPECTED_HEADER = [
    "window",
    "mean",
    "rms",
    "p2p",
    "crest_x100",
    "zcr",
    "mad",
    "diff_rms",
    "fan",
]


def main() -> None:
    parser = argparse.ArgumentParser(description="Record features from the Arduino")
    parser.add_argument("--port", required=True, help="COM port, for example COM3")
    parser.add_argument(
        "--label",
        required=True,
        help="class name: normal, imbalance, rubbing, blocked, off",
    )
    parser.add_argument("--seconds", type=int, default=120, help="how long to record")
    parser.add_argument(
        "--fan-on",
        action="store_true",
        help="turn the fan on at the start (sends 'f' to the board)",
    )
    parser.add_argument("--out", default="data", help="folder for CSV files")
    args = parser.parse_args()

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_path = out_dir / f"{args.label}_{stamp}.csv"

    with serial.Serial(args.port, 115200, timeout=2) as ser:
        time.sleep(2.0)
        ser.reset_input_buffer()

        if args.fan_on:
            ser.write(b"f")
            time.sleep(2.0)
            ser.reset_input_buffer()

        rows = 0
        t_end = time.time() + args.seconds
        with out_path.open("w", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(EXPECTED_HEADER + ["label"])
            while time.time() < t_end:
                line = ser.readline().decode("ascii", errors="ignore").strip()
                if not line or line.startswith(("#", "window")):
                    continue
                parts = line.split(",")
                if len(parts) != len(EXPECTED_HEADER) or not all(
                    p.isdigit() for p in parts
                ):
                    continue
                writer.writerow(parts + [args.label])
                rows += 1
                if rows % 10 == 0:
                    left = int(t_end - time.time())
                    print(f"{rows} windows saved, {left} s left | last: {line}")

        if args.fan_on:
            ser.write(b"f")

    print(f"Done: {rows} windows -> {out_path}")


if __name__ == "__main__":
    main()
