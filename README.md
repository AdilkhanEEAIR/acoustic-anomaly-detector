# Edge AI Acoustic Anomaly Detector (Project 5)

Programming Interface and Microcontrollers - Adilkhan Dzhanteliev, EEAIR24

An Arduino Uno (ATmega328P) listens to a small fan with a MAX9814 microphone.
The chip computes time-domain sound features and sends them to the PC.
A decision tree learns to tell a normal fan from a faulty one.

## Folders

| Path | What it is |
|---|---|
| `firmware/acoustic_detector/acoustic_detector.ino` | Firmware, register level only (ADC free-running, TIMER2, TIMER0 PWM, UART) |
| `wokwi/` | Simulation: `sketch.ino` + `diagram.json` (potentiometer instead of the microphone) |
| `python/logger.py` | Saves features from the board into `data/*.csv` with a class label |
| `python/train.py` | Trains the decision tree, saves pictures and `model.h` into `results/` |
| `docs/schematic.png` | Wiring diagram |

## How it works

1. ADC runs in free-running mode (prescaler 128, about 9.6 kHz).
2. TIMER2 in CTC mode gives an interrupt every 250 µs (4 kHz). The interrupt takes the last ADC value.
3. Features are updated in the interrupt, samples are not stored (only 2 KB RAM).
4. Every 2048 samples (0.51 s) one CSV line is sent by UART (115200 baud).

Features: `rms`, `p2p` (peak-to-peak), `crest_x100` (peak / RMS × 100), `zcr` (zero crossings),
`mad` (mean absolute deviation), `diff_rms` (how fast the signal changes).
All math is integer, because the chip has no floating-point unit.

Memory use (avr-gcc -Os): Flash about 2.3 KB of 32 KB, RAM about 160 B of 2 KB.

## Commands to the board (Serial Monitor, 115200)

`f` fan on/off, `1`-`9` fan speed 10-90 %, `0` 100 %, `r` red LED on/off.

## Steps

```powershell
pip install pyserial scikit-learn matplotlib pandas micromlgen
cd python
python logger.py --port COM3 --label off --seconds 60
python logger.py --port COM3 --label normal --seconds 120 --fan-on
python logger.py --port COM3 --label imbalance --seconds 120 --fan-on
python train.py
```

Close the Arduino Serial Monitor before running `logger.py` - only one program can use the port.
