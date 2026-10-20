# vip_vips (Plugify)

[VIP] VIPS: !vips (and mm_vips / sm_vips / vips) lists every VIP that is on the server, with or without the VIP group in parentheses depending on vip_vips.ini. Ported from the Pisex VIP core module VIP_Vips (vip_vips). Requires the vip core.

Plugify port (fork) of **VIP_Vips ([VIP] VIPS)** by Pisex (Metamod (C++)).
Original: https://github.com/Pisex/cs2-vip-modules
The code was rewritten for Plugify / s2sdk; behaviour follows the original.

## Requirements
- [Plugify](https://github.com/untrustedmodders/plugify) with [s2sdk](https://github.com/untrustedmodders/plugify-plugin-s2sdk)
- Plugify dependencies: s2sdk, vip, hudmenu (optional)

## Installation
Download `vip_vips-1.0.1.zip` from Releases and extract it into `game/csgo/`.

## Building
Requires Docker. `./build.sh` builds in the Steam Runtime 3 (sniper) SDK image; the package
is written to `build/package/vip_vips`.

`plugins/vip` carry only the API headers of the plugins this one uses.

## License
GPL-3.0, see LICENSE.

