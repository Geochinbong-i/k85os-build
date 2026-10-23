## k85OS 8.0

New tools, a rewritten IR remote, more storage, safer file handling and a round of bug fixes.

### Added
- **Other Firmware** (Tools): install a third-party firmware (Nemo and WLED are in the list; any GitHub repository via its latest release; or a direct https link) into the free OTA slot. It prefers ESP32-S3 builds, shows short file names and a size marker, unpacks full-flash images automatically, checks the size against the slot and shows the SHA-256. Nothing is activated automatically: use BIOS -> Boot options -> Alt Firmware. These images are not signed, install only what you trust.
- **IR Remote rewritten**: IR TX (TV remote by brand and button, Blast power, Blast all), IR RX (noise filtering, NEC / Samsung / Sony decoding, raw capture of other signals, test before saving) and Saved codes (send, rename, delete). The screen turns to portrait while the tool is open and returns to normal on exit. Codes are saved to LittleFS by appending one short line per new code.
- **Larger storage**: LittleFS grown from 2.1 MB to 2.9 MB (two app slots of 2.5 MB each).

### Changed
- **Buttons**: holding A+B only exits and can no longer trigger another action (no more accidental selections), and holding A scrolls menus backwards.

### Fixed
- **Internet SDR** (KiwiSDR): connects and plays audio now (authentication, audio setup after the server announces its rate, keep-alive, reassembly of split frames, jitter buffer, lost-connection detection).
- **WiFi / Router**: the uplink reconnects with back-off instead of a tight loop; stopping the router no longer disturbs the normal WiFi connection; AP settings are validated; open networks and a missing password no longer fail or crash.
- **Web Radio**: fixed a buffer overflow when building long replies.
- **Downloads and file copies**: theme and app downloads go through a temporary file and every write is checked, so a failed download can no longer destroy an installed theme; copying from SD and applying BIOS / GRUB themes report errors (for example a full disk) instead of "Applied". Signature files no longer show up as packages, and a JSON game larger than 16 KB gives a clear message.
- **GitHub downloads** with very long redirect links work now (larger request buffer).
- **Hotspot web file manager**: write errors (storage full) abort the upload at once and remove the partial file.
- **Web Terminal**: the login no longer fails when the POST body arrives in a separate packet.
- **Web Terminal**: control characters in command output are escaped correctly.
- **FTP**: the "login configured" check no longer depends on a too small buffer.

### Notes
- An over-the-air update installs the application only; the partition table is not changed.
- The larger storage applies to devices flashed over USB (`idf.py flash`); LittleFS is reformatted then, so back up your files first.
- Firmware installed through Other Firmware is not signed. Some of them confirm themselves as valid, and then the device keeps booting them. To get k85OS back, erase the boot-selection data over USB: `python -m esptool --chip esp32s3 -p COMx erase_region 0x10000 0x2000`, then reset.

- Firmware is signed (Ed25519): `k85OS_8.0.bin` + `k85OS_8.0.bin.sig`.
