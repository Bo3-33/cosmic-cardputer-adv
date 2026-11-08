# Standalone Authenticator — v0.6 development build

Open **Apps > Authenticator**. This is an unofficial native app running inside
the COSMIC launcher, using your existing theme. After one-time computer pairing,
it generates codes offline and pulls encrypted accounts directly from Proton over
Wi-Fi. No computer service or bridge is needed during normal use.

**Account login is not implemented on the Cardputer.** The computer handles Proton
SRP, bcrypt and OpenPGP key unlocking; the device receives a passphrase-encrypted
vault containing its own session and the unwrapped Authenticator keys. If Proton
revokes that session or introduces a new encryption key, pair again. Refreshing
an existing session is implemented on-device. Cloud sync is read-only: local
creation, editing and deletion of accounts are not provided.

## Install and pair

1. Flash the v0.6 firmware with the included flashing script. It backs up the
   existing 8 MB flash first. No partition-layout change from v0.5 is needed.
2. Enable sync in the official Proton Authenticator app and let it upload.
3. Install **git** and **Bun >=1.1** on your computer if needed. Bun installation:
   <https://bun.com/docs/installation>.
4. Mount the SD card, then run the pairing helper beside its matching `.ts` file:

   ```sh
   python3 pair-authenticator.py --sd /media/boyates/BOCARDPUTER
   ```

   The helper downloads reference CLI commit
   `4e796c4f1e8ba19e3098e591fff5833e4d9c3ef5` into your user cache, installs its
   locked dependencies, then asks for your Proton login and login 2FA code.
   Password entry is masked. It does not write your Proton password, account
   session or unencrypted vault to a file. JavaScript strings cannot be reliably
   scrubbed from RAM; the pairing process exits afterward.

   Choose a **separate strong vault passphrase, 12–64 printable ASCII characters**.
   Pairing creates `/authenticator/vault.cav` directly on the SD card. Existing
   vaults are never overwritten by this helper. Alternatively use
   `--output /path/to/new-vault.cav` and copy it to the SD card yourself.
5. Safely eject the SD card and insert it into the Cardputer.
6. Open the app and enter the vault passphrase. Choose **ENTER > Set clock over
   Wi-Fi** after each cold boot, or enter Unix UTC time manually. Codes are hidden
   until a plausible clock is available. The device has no battery-backed RTC.

The reference CLI supports single-password Proton accounts and TOTP login 2FA.
Security-key-only login and organization-managed keys are unsupported. Its
interactive CAPTCHA helper is macOS-only: pairing on Linux may fail if Proton
requires a CAPTCHA. The error is deliberately generic to avoid printing server
data or secrets. Pairing never creates or deletes cloud entries or remote keys.

## Controls

- Arrow keys: previous/next account.
- ENTER: sync, Wi-Fi clock, manual clock, or lock.
- S: pull from Proton over Wi-Fi.
- ESC/back: leave and lock immediately.
- 60 seconds without input: leave and lock automatically.

There is no unlocked background instance in the app switcher. Reopening requires
the passphrase. The code view uses the buffered display and updates once per
second. Clock sync uses ordinary unauthenticated NTP; manual time is available
when the network clock cannot be trusted. Incorrect time produces incorrect codes.

## Limits, protection and recovery

- SHA1/SHA256/SHA512 TOTP, 6–8 digits, periods 1–3600 seconds; Steam codes.
- Up to 64 entries, eight Authenticator keys, 48 KiB encrypted-vault plaintext;
  each network response is limited to 48 KiB. Large accounts may exceed the
  Cardputer's available RAM before those limits. Close other apps if sync asks.
- Local vault: PBKDF2-HMAC-SHA256, 120,000 rounds, random 16-byte salt;
  AES-256-GCM with a new random 12-byte nonce per save and authenticated tag.
  Keys, tokens and metadata are all inside the encrypted file. No plaintext SD
  import file or WebUI password field is used.
- Direct HTTPS verifies the hostname and certificate chain against bundled ISRG
  Root X1/X2 trust anchors; it does not use insecure TLS. Certificate or API
  changes may require updated firmware. The paired client's compatibility header
  is retained; User-Agent identifies this unofficial Cardputer client.
- Incoming pages and decrypted entries are validated before replacing the cache.
  Missing keys, unsupported formats, size limits and network errors keep the
  previous cache. A refreshed token is saved immediately, even if a later pull
  fails. A revoked session does not prevent viewing cached codes offline.
- Saves use encrypted `vault.tmp`, then rotate the previous vault to `vault.bak`.
  If the main file is missing at startup, the backup is restored. A corrupt main
  file is not silently replaced: restore the backup manually or pair again.
  SD/FAT cannot guarantee survival of power loss; keep a separate encrypted backup.
- WebUI/SD backups can copy encrypted vault files, so a strong passphrase matters.
  Locking wipes explicit key/plaintext buffers and JSON arenas on a best-effort
  basis. This custom firmware has no security audit or hardware tamper protection.
  Keep your existing authenticator until real-account/device validation succeeds.

## Validation and source

The firmware build and portable tests pass. Tests include all RFC 6238 vectors,
64-bit timestamps, malformed URI/protobuf rejection, wrong-password/tag/AAD
rejection, and compatibility with synthetic entries encrypted by Proton's official
WASM core 1.7.2 (TOTP and Steam). The pairing helper passes TypeScript checking.
**Real Proton login, session refresh and sync, Cardputer memory usage, clock
controls and physical UI have not been tested with a real account/device.** No
credentials were requested and nothing was flashed during development.

References: [reference CLI](https://github.com/brandonkramer/proton-authenticator-cli)
(GPL-3.0-or-later), [official crypto/protobuf format](https://github.com/protonpass/proton-pass-common)
(reviewed commit b1a9edc622a5a8e0b284326a5586e6c5f08153b5).
The new Authenticator integration and pairing helper are GPL-3.0-or-later; see
`AUTHENTICATOR-LICENSE`. Other project components retain their respective licenses.
Host tests require a C++ compiler and OpenSSL development headers (`libssl-dev`).
