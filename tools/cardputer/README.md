# Cardputer updater and backups (Linux)

Installs the newest published COSMIC Cardputer ADV firmware from
[Bo3-33/cosmic-cardputer-adv](https://github.com/Bo3-33/cosmic-cardputer-adv/releases).
It includes published prereleases. Firmware downloads are checked against GitHub's
SHA-256 digest, or the matching release kit's checksum manifest.

## Install

Requires Python 3.10+, Python's venv/pip support, and standard Linux disk tools
(`lsblk`, `findmnt`, `tar`, and optionally `udisksctl`, `sudo`, `setfacl`).
On Debian/Ubuntu, Python's venv support is supplied by `python3-venv` and
`setfacl` by `acl`.

From a checkout or downloaded source ZIP, run:

```sh
sh tools/cardputer/install.sh
export PATH="$HOME/.local/bin:$PATH"
```

The installer creates its own Python environment in
`~/.local/lib/cosmic-cardputer` and installs `cardputer` and `cardputer-connect`
in `~/.local/bin`. It does not access hardware. Run it again from an updated
checkout to update the command. The command fetches the newest firmware at each
installation, independently of the command's own version.

## Back up and install

Connect the Cardputer by USB. For an SD backup, also insert the SD card into
your computer's card reader; the updater cannot read it through the Cardputer.
Close serial monitors and other flashers first.

```sh
cardputer --all --fix-permissions
```

This backs up SD files (including hidden files), backs up all 8 MB of firmware,
then flashes the downloaded full image at address zero. An incomplete requested
backup stops installation. `--fix-permissions` uses sudo only to grant your user
temporary access to the selected USB port; the updater itself runs as your user.

Backups go into a new dated directory each time, for example:
`~/.local/share/cosmic-cardputer/backups/20261108-093500-123456/`.
It contains `sd-card.tar`, `firmware-original.bin`, `manifest.json`, and
`SHA256SUMS`. SD backup is a file archive, not a raw partition image. This command
backs up SD files; it does not install or replace SD app packages.

```sh
cardputer --flash --fix-permissions      # firmware backup + newest firmware
cardputer --backup-firmware             # firmware backup only
cardputer --backup-sd                   # SD files only
cardputer --all --dry-run               # check release/plan; no device access
cardputer --flash --release 0.7.1        # select a published release
cardputer --all --sd /media/USER/CARD --port /dev/ttyACM0
cardputer --all --backup-dir "$HOME/Cardputer-backups"
cardputer --repo Bo3-33/cosmic-cardputer-adv # validate and remember a source
```

Automatic SD detection uses the label `BOCARDPUTER`. For a different label,
supply its mounted FAT/exFAT root with `--sd`. Set `CARDPUTER_HOME` to relocate
configuration, downloads, logs, and default backups. For offline/local firmware,
put the matching `cosmic-cardputer-adv-vVERSION-full.bin` and `-kit.zip` in that
directory and use `--flash --local`.

After flashing, disconnect USB and power-cycle the Cardputer. The command leaves
it in download mode. If USB does not connect, try:

```sh
cardputer-connect --fix-permissions
```

This helper waits for USB and tests reset/ROM connection methods with a read-only
transfer. It never writes flash; its temporary read is discarded. A missing USB
device may still require a data cable, another port, or physically entering G0
download mode. No script can connect to a device the computer cannot detect.

## Tests

```sh
python3 -m unittest discover -s tools/cardputer/tests -v
```

Tests mock hardware/network access; they never flash your device.
