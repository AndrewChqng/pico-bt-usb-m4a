# pico-bt-usb-m4a

Raspberry Pi Pico W / WH firmware that receives Bluetooth Classic A2DP audio (`AAC-LC`) and exposes a virtual **FAT32 USB Mass Storage** flash drive containing a single ~17.4-hour (`~1.93 GiB`) `.m4a` file (`BT_AUDIO.M4A`) streamed on-the-fly when played by automotive head units that only support USB media files (e.g., Suzuki `84MS0-14E`).

## Target Head Unit Compatibility (`84MS0-14E`)

| Parameter | Head Unit Requirement | Firmware Implementation |
| :--- | :--- | :--- |
| **USB Class** | USB Mass Storage Class (USB 1.1/2.0 Full Speed) | TinyUSB MSC Bulk-Only Transport (`12 Mbps` Full Speed) |
| **Filesystem** | `FAT12 / FAT16 / FAT32 / VFAT` | Procedural `FAT32` (`512 B` sectors, `4 KiB` clusters, dual mirrored FATs, MBR LBA `64`, zero non-audio files) |
| **Codec & Container** | `AAC (.m4a)`, `16 – 320 kbps`, `32 / 44.1 / 48 kHz` | Zero-transcode `44.1 kHz` Stereo `AAC-LC` in faststart `.m4a` (`264.6 kbps` container rate, A2DP capped at `224 kbps`) |
| **File Offset Limit** | `< 2 GiB` (`int32_t` parser safety) | `2,073,604,096` bytes (`2,700,000` frames = `17h 24m 53.9s`) |

## Architecture

1. **Zero-Transcode Bluetooth A2DP `AAC-LC` Passthrough (Core 0)**
   - Advertises a locked `44.1 kHz` Stereo `MPEG-2/4 AAC-LC` A2DP Sink endpoint (up to `224 kbps`) via `BTstack` on the Pico WH's CYW43439 radio.
   - Extracts bit-shifted `raw_data_block()` AAC frames from incoming RFC 3016 RTP/LATM (`AudioMuxElement`) packets in [`src/aac_latm.c`](src/aac_latm.c) and pads each frame to a constant `768` bytes (`264.6 kbps`).
   - Eliminates real-time audio transcoding on the dual Cortex-M0+ (no FPU required).

2. **Zero-RAM Procedural `.m4a` & `FAT32` Synthesis**
   - [`src/m4a_container.c`](src/m4a_container.c) places `[ftyp][moov][free][8B mdat header]` in the first `4096` bytes (`1` cluster / `8` sectors) of `BT_AUDIO.M4A`.
   - Because `stsz` uses a constant `sample_size = 768` and `stsc`/`stco` use a single contiguous chunk starting at byte offset `4096`, the entire 17.4-hour sample table takes **20 bytes** instead of megabytes of RAM.
   - Every `2` AAC frames (`2 x 768 = 1536` bytes) align to exactly `3` USB `512`-byte sectors. [`src/stream_ringbuf.c`](src/stream_ringbuf.c) maintains a 4-slot 3-sector (`1536 B`) phase-coherent window cache so single-sector or multi-sector host reads never tear an AAC frame.

3. **Paced SCSI `READ(10)` & Underrun Fallback (Core 1)**
   - Car stereos aggressively read ahead into internal RAM buffers. [`src/usb_msc_driver.c`](src/usb_msc_driver.c) paces live `mdat` sector reads for up to `28 ms` while waiting for incoming Bluetooth frames, then emits a valid `44.1 kHz` Stereo `AAC-LC` silent frame if no audio is active—preventing multi-second silence buffer bloat while avoiding USB SCSI timeouts.

4. **Bluetooth Pairing & Auto-Reconnect**
   - Always discoverable/connectable when idle (`Pico-USB-Audio`).
   - Automatically reconnects to the last bonded device on ignition power-up.
   - Onboard LED blinks while waiting/idle and turns solid during active A2DP streaming.

## Building

### Firmware (`.uf2` for Raspberry Pi Pico W / WH)

```bash
mkdir build && cd build
cmake -DPICO_BOARD=pico_w -DPICO_SDK_FETCH_FROM_GIT=ON ..
make -j$(nproc)
```

Hold `BOOTSEL` while plugging the Pico WH into a computer and copy `build/pico_bt_usb_m4a.uf2` to the `RPI-RP2` drive.

### Host Verification Suite (`LATM` + `M4A` + `FAT32` + `FFmpeg`)

```bash
mkdir build-tests && cd build-tests
cmake -DBUILD_HOST_TESTS=ON ..
make -j$(nproc)
ctest --output-on-failure -V
```

## Credits & Disclaimer

- **AI-Assisted Engineering**: Designed and implemented with assistance from **Antigravity (Google DeepMind)** by combining an RFC 3016 LATM bit-level unwrapper, a constant-sample-size (`stsz = 768`) ISO BMFF `.m4a` procedural generator, a zero-RAM virtual FAT32 block layer, and dual-core RP2040 TinyUSB/BTstack pacing.
- **Experimental Disclaimer**: This software is provided as an experimental open-source project "as is" without warranty of any kind. Automotive USB host implementations vary in internal buffer sizes, parser strictness, and SCSI timeout behavior; test at low volume before regular vehicle use.
