# WeActDoom

[README на русском](README.md)

**Classic DOOM on a WeAct MiniSTM32H723.** Breadboard buttons, the stock 160×80 TFT, and a LOMO microscope for a monitor. I built it to see if it would run. It turned out to be quite playable through the eyepiece.

<p>
  <img src="media/bench-setup.png" width="285" alt="WeAct MiniSTM32H723 on a LOMO microscope stage with buttons wired on a breadboard">
  <img src="media/doom-through-the-microscope.png" width="285" alt="DOOM through the eyepiece, with individual RGB subpixels visible on the display">
</p>

A [GBADoom](https://github.com/doomhack/GBADoom) port for the STM32H723VGT6. Play a classic campaign from your own IWAD or use Freedoom. The microSD card holds the campaign and saves. The active group of maps goes into the board's **8 MiB OctoSPI flash**. This build runs without audio.

## Quick start

You need a **WeAct MiniSTM32H723** with its stock display, a **FAT32 or exFAT** microSD card, and pushbuttons. Build with PlatformIO and prepare game data on Windows with Python 3.

1. Run `pio run` from the project root.
2. Flash with ST-Link using `pio run -t upload`. For USB DFU, hold **BOOT0**, tap **RESET**, then release **BOOT0**. Use STM32CubeProgrammer to write `.pio/build/weact_h723vg/firmware.bin` at `0x08000000`.
3. Prepare the card using one of the methods below, insert it, and press **RESET**. Wait for the initial import to OctoSPI. At the title screen, press **K1** or **START**, then select **New Game** with **A**.

### Where to get an IWAD

| Campaign | File | Source |
| --- | --- | --- |
| Doom Shareware v1.9 | `DOOM1.WAD`, nine maps from the first episode | [Original shareware archive on /idgames](https://legacy.doomworld.com/idgames/idstuff/doom/doom19s) · [ZIP mirror](https://mirrors.lug.mtu.edu/idgames/idstuff/doom/doom19s.zip) |
| Doom Registered, The Ultimate Doom, or Doom II | `DOOM.WAD` / `DOOMU.WAD` or `DOOM2.WAD` from your copy of the game | [DOOM + DOOM II on Steam](https://store.steampowered.com/app/2280/) · [GOG](https://www.gog.com/en/game/doom_doom_ii) |
| Freedoom Phase 1 | `freedoom1.wad`, a free campaign with its own maps | [Official Freedoom 0.13.0 release](https://github.com/freedoom/freedoom/releases/tag/v0.13.0) |

Game data and the packs made from it are prepared separately. This repository has the firmware source and the SD card preparation tools.

### Full campaign

```powershell
python tools/prepare_campaign.py "C:\Games\DOOM.WAD"
```

The script downloads the pinned GbaWadUtil version and creates `campaign_sd`. Copy **`CAMPAIGN.IDX` and every `Pnnn.GBA`** from that folder to the root of the microSD card. Keep each campaign's index and packs together. Leave the card in the board while playing.

The script groups three maps per pack by default, reducing the group size as needed to stay within the **8,384,512-byte** limit. Set a different group size with `--maps-per-pack`, or pass the path to an existing `GbaWadUtil.exe` with `--converter`. When crossing into another pack, the firmware records a checkpoint, reboots, and imports the next pack. The display shows import progress.

### Quick Freedoom demo

```powershell
python tools/prepare_freedoom.py
```

The script checks the archives' SHA-256 hashes and bundles the first three Freedoom maps as `DOOM.GBA`. Copy it to the root of the card. If the card also has `CAMPAIGN.IDX`, the indexed campaign takes priority. To replace a cached single pack, hold **K1** at power-up and release it when the progress bar appears.

## Controller wiring

Use normally open pushbuttons. With the board powered off, connect one terminal of every button to a shared **GND**, such as **P2.15**. Connect the other terminals to the GPIOs below. The firmware enables the internal 3.3 V pull-ups.

| Button | GPIO | WeAct V1.2 header | Action |
| --- | --- | --- | --- |
| ↑ | PA0 | P2.17 | Move forward |
| ↓ | PA1 | P2.18 | Move backward |
| ← | PA2 | P2.19 | Turn left |
| → | PA3 | P2.20 | Turn right |
| **A** | PA4 | P2.21 | Fire or confirm |
| **B** | PA5 | P2.22 | Use doors and switches or go back in menus |
| **STRAFE** | PA6 | P2.23 | Hold while pressing ← or → to sidestep |
| **SELECT** | PA7 | P2.24 | Cycle weapons. Hold STRAFE to toggle the map |
| **START** | PA8 | P1.28 | Menu and pause. The onboard **K1** also works |

To test the buttons, hold **K1**, tap **RESET**, and keep holding K1 until **PAD TEST** appears. Yellow shows the raw input, green shows the debounced press, and the cyan mark remembers any press. PA8 and K1 have separate indicators. To exit, release K1 and press RESET. [WeAct schematic and documentation](https://github.com/WeActStudio/WeActStudio.MiniSTM32H723)

## Saves

Slots **1–7** are manual and slot **8 AUTO** updates on level entry. Loading a save selects the right pack automatically. A save returns you **to the start of the map** with the health, armor, weapons, and ammo you had on entry. Position, open doors, and enemy state reset.

Saves and settings belong to the original IWAD. You can repack the same IWAD with a different group size and keep your saves. Back up both `S….DAT` files from the card to preserve the save journal.

## Troubleshooting

| Blue LED flashes or red bars | What to check |
| --- | --- |
| 1 | TFT connection and display initialization |
| 2 | OctoSPI flash access |
| 3 | microSD, the filesystem, and the expected pack |
| 4 | Pack size, format, and CRC. The converter must produce `GBAVER=15` |
| 5 | Unrecognized data is present in OctoSPI. After an interrupted import, leave the same card in and reboot |
| 6 | Programming or verification failed. Reboot with the same file |
| 7 | Engine error. Inspect `engine_error_message` over SWD |
| 8 | Check `CAMPAIGN.IDX` and copy the packs from the same build |

Classic DOOM was played on the actual board with breadboard buttons. Cross-pack transitions, inventory carryover, and loading a save from a different pack were also tested in an ARM emulator. Import speed depends on the board and microSD card.

## Authors and license

**sedovnik** started the project and built and tested the hardware. **Codex (OpenAI)** co-authored the port, firmware, and SD card tools. WeActDoom builds on GBADoom, PrBoom, and id Software's DOOM engine. See [NOTICE.md](NOTICE.md) for upstream credits and license notices.

The firmware is distributed under the [GNU GPL v2](LICENSE). Commercial use and distribution are permitted under its terms. If you ship a commercial product based on this port, please credit **sedovnik** as the author of WeActDoom.
