# Ollee Watch Clock — experimental v0.8

Open **Apps > Ollee Watch Clock**. This native app gets a fresh network time over
Wi-Fi, connects to a selected watch over Bluetooth LE, checks its hardware/firmware,
and sends the time reference. It requires **hardware 01.05.00 / firmware 00.01.11**;
other versions are refused before a clock write. It needs no phone bridge or
SD-card app package. Keep the phone's Ollee app closed/disconnected during use. If it still holds the
watch connection, temporarily turn Bluetooth off on the phone.

## Set up and sync

1. Choose your time zone. The default is **Denver (auto DST)**. Alternatively set
   a fixed UTC offset in minutes (for example UTC = 0, UTC+5:30 = 330). A fixed
   offset does not adjust for daylight saving automatically. Resync the watch
   after an offset/DST change; this app does not run in the background.
2. Set your watch's home latitude and longitude, and its tide/lunitidal interval
   in minutes if used. The protocol's time-reference write also replaces those
   fields; there is no separate clock-only command in the reviewed reference.
   The app cannot read the existing values, so copy the desired values from your
   phone. It will not sync until you set both coordinates or explicitly choose
   **No location: use 0,0**. That choice replaces the location with zero coordinates
   and turns the tide interval off. It can affect location/tide-based watch faces.
3. Choose **Sync watch clock** and connect the Cardputer to Wi-Fi when asked.
4. From the watch clock face hold **ALARM** until LAP blinks to enable Bluetooth.
   Choose your watch from the scanned list. The iPhone's displayed "MAC" is a
   CoreBluetooth identifier, not an address this app can use; no need to enter it.
5. Review the proposed local time, UTC offset, location and tide interval. Select
   **Send clock + these location fields** to perform the write. Cancel leaves the
   watch unchanged. Bluetooth is stopped and its resources released afterward.

Success means a valid clock-command acknowledgement arrived. There is no RTC
readback command in the reviewed interface, so check the watch display yourself.
If acknowledgement times out, the watch may already have accepted the write;
inspect it before retrying. Getting network time uses ordinary unauthenticated
NTP. Network time, connection latency and firmware behavior limit precision.

The app does not send reset, re-pair, bootloader, alarm, activity-erasure or
firmware-update commands. No changes are made merely by opening it or scanning.
The home settings are saved in NVS and are retained by the configuration-preserving
updater. Your watch address/serial and the supplied screenshot are not bundled.

## Compatibility and validation

The protocol reference was validated by its author against **00.01.10**. The
00.01.11 layout is an experimental compatibility assumption, not verified by a
physical test here. The live version check limits writes to the specific version
requested for this app; it does not by itself prove the command layout unchanged.

Portable tests cover CRC-16, frame bounds, fragmentation, signed/time fields,
input validation and version rejection. Firmware compilation is checked. Actual
watch discovery, version replies, clock acknowledgement and resulting time/location
need testing with the watch. The app has not connected to or changed any watch
during development. If the clock command is rejected, do not reset your watch;
report the error so compatibility can be investigated.

References: [Ollee FAQ](https://www.olleewatch.com/faq),
[Gadgetbridge framing notes](https://gadgetbridge.org/internals/specifics/ollee-protocol/),
[OAP command reference](https://gist.github.com/X3NOOO/445b7adc9a271dc0179b335754815f96).
The app uses its own implementation of the documented wire format and explicitly
restricts commands to get-version (0x2a) and set-time-reference (0x23).
