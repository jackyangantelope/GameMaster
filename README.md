# GameMaster for Adafruit Fruit Jam

Game Master / Systema 2000 emulator source candidate for the Adafruit Fruit Jam (RP2350B). The emulator core is based on the accepted 0.2.18 source, including the 62-column LCD path, timer-driven speaker output, CPU fast paths, and quiet USB input logging. The existing host tests and the reported basic hardware test apply to that source baseline. This public Fruit Jam adaptation **builds but has not been tested on Fruit Jam hardware**; boot, speed, controls, video, and audio must be verified there before calling a binary hardware-validated.

No commercial BIOS or cartridge data is included. Supply your own lawful 4,096-byte Game Master BIOS as a `.bin` file in `/BIOS/GMASTER` on the SD card, and your own 8, 16, or 32 KiB cartridge `.bin` files in `/roms/GMASTER`. The firmware asks you to select a BIOS and cartridge. USB host input and HDMI output are enabled; device-specific input behavior remains unverified on Fruit Jam.

The default build uses the public Fruit Jam board profile at 252 MHz with a 1.20 V request. It does not enable the higher-clock test profile. Performance figures from another RP2350B configuration do not predict Fruit Jam FPS or audio quality.

## Build

On Windows, install CMake, Ninja, an ARM Cortex-M33 toolchain, Pico SDK 2.3.0 or newer, and Pico-PIO-USB. Run:

```powershell
./scripts/build-release.ps1 -PicoSdkPath C:\path\to\pico-sdk -PicoPioUsbPath C:\path\to\Pico-PIO-USB -ToolchainPath C:\path\to\toolchain -NinjaPath C:\path\to\ninja.exe
```

If the SDK cannot find its host tools or TinyUSB checkout, also pass `-PioasmDir`, `-PicotoolDir`, or `-TinyUsbPath`. The output is `build/fruit-jam-release/picoGameMaster.uf2`.

The default UF2 links to a resident loader's application partition beginning at flash address `0x10080000`. **Do not install it as a standalone full-flash image.** The `BUILD_FOR_BOOTLOADER=OFF` link option is available for a separate standalone build, but that variant has not been validated.

## Origin and license

The uPD7810/78C11 interpreter comes from [tautologico/common-src-project](https://github.com/tautologico/common-src-project), revision `91ebd909500b7c829d9741f10ac30964cbab71f0`. Its authors offered GPL-2.0-or-later as an alternative to the historical MAME license; this port selects the GPL option. The retained source header and [upstream GPL text](deps/common-src-project/license/COPYING.txt) are included. The copied `upd7810.c` is **modified**, not pristine upstream: guarded Game Master fast paths and single-step behavior were added. See [SOURCE_CHANGES.md](SOURCE_CHANGES.md).

The public Fruit Jam runtime uses [PicoPlus-devel/pico_shared](https://github.com/PicoPlus-devel/pico_shared) revision `df683bf00ff95e1ceda88f132bbf72f8cc7cadb0`, licensed under GPL-3.0. The combined firmware is distributed under [GPL-3.0](LICENSE), with component details in [THIRD_PARTY.md](THIRD_PARTY.md).
