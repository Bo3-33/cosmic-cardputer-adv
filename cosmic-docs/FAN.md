# Honeywell HYF290B fan remote — v0.4

Open Apps → Honeywell Fan. No Wi-Fi, pairing, or SD-card app package is needed. Point the Cardputer ADV's built-in IR emitter toward the fan's receiver.

Use the arrow/navigation keys to select Power, Oscillate or Fan Speed, then press ENTER once to transmit. Backtick/ESC returns to the desktop. The app uses the same COSMIC colors and buffered rendering as the launcher.

The signals come directly from the supplied `Honeywell_Fan_HYF290B.ir`, retained in the source bundle under `assets/ir/`. All captured mark/space durations and repeated frames are preserved. The ESP32 RMT transmitter drives GPIO44 with a 38 kHz carrier and 33% duty cycle. The entire selected capture is transmitted once per press; no additional repeat loop is added. RMT requires a final inactive duration after an odd-length capture, so a 1 microsecond inactive tail is added after its final mark. The emitter is left inactive when the app closes, and RMT resources are released.

Power contains 455 durations (506.7 ms), Oscillate 479 (533.4 ms), and Fan Speed 407 (451.7 ms). The UI says Sent only after the transmitter completes. This means the IR waveform was emitted, not that the fan acknowledged it. The fan does not report its power, speed or oscillation state to this remote. There are no timer or other controls in the supplied file, so none are invented here.

The capture was labeled Honeywell QuietSet Whole Room Tower Fan HYF290B. Compatibility with another model depends on whether it accepts the same signals. Physical range, emitter alignment and fan response have not been tested in this session.

Source checks verify all durations against the original IR file, carrier/duty settings, RMT duration limits and symbol-packing bounds. To regenerate the compiled timing arrays after intentionally changing the capture:

```sh
python3 support_files/generate_honeywell_ir.py
python3 test/cosmic/test_fan.py
pio run -e m5stack-cardputer
```

This build keeps the existing 2 MB launcher partition layout. Only the firmware needs updating; no SD-card changes are needed for the fan remote.
