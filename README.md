# DTU-351 user-space driver for macOS

This project ports the DekTec DTU-351 USB transport from DekTec's BSD-licensed Linux
driver to a macOS user-space library built on `libusb`. It does not install a kernel
extension and does not require DriverKit entitlements.

## Current milestone

- Discovers normal (`1297:0333`) and factory/FX3 (`1297:00f3`) device states.
- Claims the USB interface and discovers its bulk-IN endpoint.
- Implements the DTU-351 `D2`/`D3` 16-bit register protocol.
- Implements device reset and power-on commands.
- Extracts and uploads the original FX3 and FPGA firmware payloads.
- Configures the documented SD or HD full-frame 10-bit SDI capture registers.
- Implements the driver's endpoint-3 bulk ring, hardware-validated on macOS with
  packed 10-bit SDI from 525i59.94/625i50 through 1080i60, plus an experimental
  isochronous ring.
- Includes probe and raw-capture command-line tools.

The code deliberately does not reuse DTAPI: the Linux DTAPI library is a prebuilt ELF
object and cannot be linked on macOS. The protocol implementation is derived from the
redistributable driver source retained under `vendor/`.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Homebrew's `libusb` is sufficient on Apple silicon. Probe for a connected device with:

```sh
./build/dtu351-probe
```

The probe is read-only. Raw capture assumes the device is already running DekTec FX3
and FPGA firmware:

```sh
./build/dtu351-capture hd capture.raw
```

For a bounded hardware smoke test, append the number of completed transfers:

```sh
./build/dtu351-capture hd capture.raw 8
```

Set `DTU351_TRANSPORT=iso` to exercise the experimental isochronous path; bulk is the
default because it matches DekTec's Linux driver. A successful official-driver Linux
trace identified the hardware receive value as `2` (DTAPI translates its public
`0x5100` flag set before calling the driver). The corrected macOS bulk path has been
validated with consecutive 512 KiB completions and stable full-frame cadence; see
`docs/hardware-validation-2026-10-26.md`.

## Firmware and initialization

A retail unit enumerates first with Cypress firmware and then re-enumerates after FX3
upload. `dtu351-bootstrap` performs that transition, initializes the FPGA, checks
SuperSpeed, and reports the detected SDI standard. This path has been hardware-validated.
Endpoint-3 payload DMA and frame cadence have been hardware-validated across 13
SD and HD modes.

## Analyzer integration boundary

BasebandExpert Receiver Core consumes this library through its DTU-351 capture adapter.
The adapter assembles the full 10-bit SDI stream, converts active video to v210, decodes
ST 272/ST 299 embedded audio, and passes non-audio HANC/VANC through libklvanc. The
Analyzer integration has been live-validated with preview, native scopes, 16-channel
audio, and ancillary packet inventory.

## Licensing

New portability code is BSD-2-Clause; see `LICENSE`. DekTec source and extracted firmware
retain the vendor's BSD-style license at `vendor/dektec-linux-sdk/LICENSE`.
