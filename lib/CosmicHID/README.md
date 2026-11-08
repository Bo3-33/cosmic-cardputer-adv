# COSMIC HID adaptation

Core input routing, keymap, tilt mouse, and USB/BLE transports adapted from
[jayden-sudo/Cardputer-Keyboard-Mouse](https://github.com/jayden-sudo/Cardputer-Keyboard-Mouse),
commit bc88fe165e5e6657edbd45abf1bf3c4dd5050d00, MIT (LICENSE).

The launcher supplies physical-key capture, themed UI, NVS settings and a small
BMI270 Wire adapter. Added actual gyro integration/calibration, device-unique BLE
identities, BLE shutdown and transport-start status. Core code can be built with
COSMIC_HID_HOST for portable tests. Host reports are generated only in the app.

BMI270 initialization image and register sequencing reference M5Unified commit
96576a14247ddc935149145aa6b51943464120f1, MIT (M5STACK-LICENSE).
NimBLE-Arduino 2.5.0, commit f66f4fe26306747cd8308e77d755da1863219089,
is vendored separately under lib/NimBLE-Arduino with its Apache/BSD licenses.
