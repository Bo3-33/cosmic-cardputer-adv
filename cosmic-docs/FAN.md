# Honeywell HYF290B fan remote — v0.7.1

Open Apps → Honeywell Fan. No Wi-Fi, pairing, or SD-card app package is needed. Point the Cardputer ADV's built-in IR emitter toward the fan's receiver.

Use the arrow/navigation keys to select Power, Oscillate or Fan Speed, then press ENTER once to transmit. Backtick/ESC returns to the desktop. The app uses the same COSMIC colors and buffered rendering as the launcher.

The current file is the [Flipper-IRDB HYF290B capture](https://raw.githubusercontent.com/Lucaslhm/Flipper-IRDB/main/Fans/Honeywell/Honeywell_Fan_HYF290B.ir), retained under `assets/ir/`. Its three signal records are identical to the earlier supplied file; only text formatting differs. Older firmware was reported not to operate the user's fan. Replacing the file alone is therefore not a confirmed fix. All captured mark/space durations and repeated frames are preserved. The ESP32 RMT transmitter drives GPIO44 with a 38 kHz carrier and 33% duty cycle. v0.7.1 uses a 256-symbol DMA buffer to hold the complete recording, avoiding small-memory interrupt refills during transmission. GPIO44 uses the chip's maximum drive capability while the app is open, then restores its previous setting on exit. The official ADV schematic shows an active-high IR LED from GPIO44 through 22 ohms to ground, so idle and end-of-transmission levels stay LOW. The entire selected capture is transmitted once per press; no additional repeat loop is added. RMT requires a final inactive duration after an odd-length capture, so a 1 microsecond inactive tail is added after its final mark. The emitter is left inactive when the app closes, and RMT resources are released.

Power contains 455 durations (506.7 ms), Oscillate 479 (533.4 ms), and Fan Speed 407 (451.7 ms). The UI says Sent only after the transmitter completes. This means the IR waveform was emitted, not that the fan acknowledged it. The fan does not report its power, speed or oscillation state to this remote. There are no timer or other controls in the supplied file, so none are invented here.

Press **T** for a 200 ms 38 kHz carrier test. This is not a fan command and does not toggle Power. Use an IR viewer, or a phone camera that can also see an ordinary remote's IR LED, to check whether the ADV emitter lights. Some cameras filter IR. Then point directly at the fan from 20–30 cm and try Power once. The UI only reports transmitter completion; physical emission, range and fan response remain unverified.

The capture was labeled Honeywell QuietSet Whole Room Tower Fan HYF290B. Compatibility with another model depends on whether it accepts the same signals. Physical range, emitter alignment and fan response have not been tested in this session.

Source checks verify all durations against the original IR file, carrier/duty settings, RMT duration limits and symbol-packing bounds. To regenerate the compiled timing arrays after intentionally changing the capture:

```sh
python3 support_files/generate_honeywell_ir.py
python3 test/cosmic/test_fan.py
pio run -e m5stack-cardputer
```

v0.7.1 keeps v0.7's 2.25 MB partition layout. Upgrading from v0.6 or earlier requires the full matching image/table. No SD-card changes are needed for the fan remote.

Schematic reference: [M5Stack Cardputer-ADV v1.0](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1178/Sch_M5CardputerAdv_v1.0_2025_06_20_17_19_58.pdf), sheet 1. The timing tests validate data/packing, not physical fan compatibility.
