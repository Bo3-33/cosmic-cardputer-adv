# Music — development build v0.2

Open Apps → Music. Copy MP3 or PCM WAV files into `/music` on the SD card (up to two album subdirectories, 128 tracks). A quiet one-second Demo.wav is included as an optional playback test. Music never starts automatically when entering the player.

Left/right browses tracks; ENTER plays a selected track or pauses/resumes the current one; up/down changes volume in 5% steps. Tracks advance automatically at the end. Backtick/ESC returns to the desktop and pauses playback. The switcher provides Resume Music and Close Music; close releases decoder and I2S memory. Music is foreground-only in this version; the controller bridge can still forward in the background.

Cover Flow uses a large center cover, perspective-compressed neighboring covers and a short reflection. It has no automatic sliding animation. Track titles come from filenames. Embedded ID3/JPEG artwork is not extracted on-device.

Use optional 64×64, little-endian RGB565 album artwork. Put `cover.rgb565` beside an album's tracks, or use `<track-name>.rgb565` for a single track. The included generic cover works out of the box. Convert a PNG/JPEG on your computer:

```sh
python3 app-sdk/prepare_cover.py album.jpg cover.rgb565
```

The converter requires Pillow. One cover is exactly 8192 bytes. The source image is center-cropped to a square. Copy the converted file to the SD card alongside the tracks.

The output is mono on the ADV's built-in ES8311 audio path. MP3 and PCM WAV are supported; DRM files, AAC, FLAC, network streams and embedded-art decoding are not implemented. Large decoder allocations may require closing a Lua app or closing Music before opening another app. No on-device audio or display timing tests were available during this revision.

The codec initialization register sequence follows M5Stack's MIT-licensed M5Unified `_speaker_enabled_cb_cardputer_adv` implementation. Playback uses ESP8266Audio (GPL-3.0), whose complete library source and license are included in the source bundle. See `lib/ESP8266Audio/LICENSE` and its source notices. The register sequence is attributed in `src/cosmic_music.cpp`; the complete M5Stack MIT notice follows:

Copyright (c) M5Stack

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
