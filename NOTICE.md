# WeActDoom notices

Copyright © 2026 sedovnik for the WeActDoom port. The original upstream authors retain copyright in their respective contributions.

- **sedovnik** created the project, assembled the hardware, tested it on the board, and took the bench and microscope photos.
- **Codex (OpenAI)** co-authored the port, firmware, and SD card tools.

The combined firmware is distributed under the GNU General Public License, version 2. See [LICENSE](LICENSE). Commercial distribution is permitted under the GPL's terms, including its notice and corresponding-source requirements. If you ship a commercial product based on this port, please credit **sedovnik** as the author of WeActDoom.

## Third-party components

- The engine comes from [GBADoom at commit 7763f5f](https://github.com/doomhack/GBADoom/tree/7763f5f9adf18f83a59093bc1ab786fe2c36591f), based on [PrBoom](https://prboom.sourceforge.net/) and [id Software's DOOM source release](https://github.com/id-Software/DOOM). The original source notices are retained. The modified upstream files identify the WeActDoom changes.
- FatFs R0.15 with patch 2 is copyright © 2022, ChaN. Its terms are reproduced in `lib/FatFs/src/ff.c` and `ffunicode.c`.
- The ST7735 component is copyright © 2018 STMicroelectronics and licensed under BSD 3-Clause. The notice and conditions follow.
- STM32Cube CMSIS and HAL are resolved by PlatformIO. Their license notices are supplied with the installed package.
- DOOM and its commercial IWADs belong to their respective rights holders. Game data is acquired separately. [id Software's source release](https://github.com/id-Software/DOOM) and the original game data have separate terms.

## ST7735 component

Copyright (c) 2018 STMicroelectronics. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
