# DTU-351 macOS hardware validation — 2026-10-26

Hardware: retail DTU-351 connected directly at USB SuperSpeed with a BasebandExpert
Generator 1080i59.94 SDI signal routed to its input.

## Verified

- Cold device enumerated as `1297:0333`, manufacturer `Cypress`, product `WestBridge`.
- The extracted 119,476-byte FX3 image uploaded and the device re-enumerated as
  manufacturer `DekTec`, product `DTU-351`.
- The extracted 135,634-byte FPGA image uploaded in 512-byte control transfers.
- FPGA type register returned decimal `351`.
- FX3 USB speed register returned `2`, DekTec's SuperSpeed value; macOS reported a
  5 Gb/s link.
- GS1661 input detection locked to `1080i59.94` with `DS1=0x0a66` and
  `RasterStructure=0x3a1c`.
- FX3 PHY and link error registers both returned zero.

## Original macOS DMA failure

Endpoint 3 payload transfer has not started on macOS. Eight queued 512 KiB bulk
transfers on alternate setting 0 time out with no data. The device also exposes
SuperSpeed isochronous alternate settings; HD alternate setting 6 provides 24,576
bytes per 125 microseconds, but queued isochronous transfers likewise receive no data.

The evidence narrows the remaining problem to the transition from the FPGA `RxMode`
write to active USB DMA, or to how macOS/libusb submits endpoint-3 traffic. Firmware,
FPGA configuration, input lock, format detection, and USB link speed are working.

## Clean-boot and macOS-only DMA diagnostics

The unit was physically unplugged for ten seconds and reconnected. It returned as the
Cypress/WestBridge boot device, accepted the FX3 and FPGA images, enumerated at 5 Gbit/s,
and relocked to 1080i59.94. A fresh capture still returned zero bytes, ruling out a
previously wedged FIFO or DMA channel.

Additional tests made the receive path match the Linux worker more closely:

- Removed the extra `CLEAR_FEATURE(ENDPOINT_HALT)` operation before capture.
- Queued all eight 512 KiB bulk reads and serviced the Darwin event source before writing
  the FPGA receive mode.
- Confirmed with libusb tracing that macOS/IOKit accepted all eight reads on endpoint
  `0x83`; none completed before the diagnostic timeout.
- Tested `0x1100` (full SDI), `0x5100` (full 10-bit), `0x9100` (full 16-bit), and `0x1400`
  (raw DMA), all with zero payload.
- Tested isochronous alternate setting 6 with the locked HD signal, also with zero payload.
- Leaving GS1661 automatic-rate detection enabled preserves the 1080i59.94 lock during
  the attempt, but does not start payload delivery. The Linux driver's manual-HD value
  causes this unit to lose lock, which is a separate receiver-state clue.

During every locked automatic-rate test, FPGA register `0x04` reports HD, register `0x0a`
remains at frame ID 253, and reads of the write/action register `0x06` return zero. This
places the remaining fault before USB payload production rather than in file writing or
receive throughput.

## Successful Linux reference trace

The official DekTec Linux SDK v2026.09.0 driver was built and installed on Ubuntu
25.10, kernel 6.17.0-41-generic. The DTU-351 enumerated at 5 Gbit/s and DTAPI detected
video standard 68 (`1080i59.94`). A full-frame packed 10-bit read succeeded:

- 6,187,504 bytes returned by `DtInpChannel_ReadFrame`.
- First 16 bytes: `ffff0f00000000802db60412082080f7`.
- No current or latched receive flags.
- Eight 512 KiB bulk-IN requests were queued on endpoint `0x83` before receive started.
- The usbmon capture contains 250 transactions with zero capture drops.

Artifacts are stored under `traces/linux-2026-10-26/`; the PCAP SHA-256 is
`6a57822a1b4b35066e5ff5f08a99bb96d9529c9887b782c0a6d8df2cc18cac2d`.

## Root cause found in the trace

DTAPI's public receive flags and the DTU-351 FPGA register use different encodings.
`DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B` is `0x5100`, but DTAPI translates that
to hardware value `0x0002` before issuing `DTU_IOCTL_SET_RX_MODE`. The Linux USB trace
shows this exact start sequence:

1. Write GS1661 register `0x24 = 0` (manual HD rate).
2. Write FPGA register `0x04 = 1` (HD definition).
3. Submit eight 512 KiB bulk reads to endpoint `0x83`.
4. Write FPGA register `0x06 = 2` (start packed 10-bit receive).
5. Bulk completions begin about 10.7 ms later.

The macOS implementation had incorrectly written the untranslated public value
`0x5100` directly to FPGA register `0x06`, so the FPGA never entered its valid receive
state. The default constant is now `0x0002`, invalid raw values are rejected, and the
stop path now matches the official worker's device-state reset followed by automatic
rate selection.

## Corrected macOS capture

After moving the same DTU-351 and signal back to the Mac, a cold bootstrap again
reported SuperSpeed code 2 and locked to 1080i59.94 (`DS1=0x0a66`,
`RasterStructure=0x3a1c`). With FPGA register `0x06 = 2`:

- All eight initially queued 512 KiB bulk reads completed, producing 4 MiB with no
  timeout.
- A 24-transfer run produced 12,582,912 consecutive bytes.
- The official Linux frame-start signature
  `ffff0f00000000802db60412082080f7` occurs at byte offsets 12, 6,187,524, and
  12,375,036 in that sample: a stable 6,187,512-byte device-frame cadence.
- The cadence consists of a 12-byte device framing prefix and the expected 6,187,500
  bytes of packed 10-bit 2200x1125 SDI data.

The macOS DMA problem is resolved. The retained validation sample is
`traces/mac-2026-10-26/dtu351-1080i59.94-twoframes-packed10.raw`, SHA-256
`325078bf6f76db83ddded1955cbf49750c722b0730ea85bfee983a326ff3fd64`.

## Receiver and Analyzer integration

The Receiver Core frame assembler removes the 12-byte device prefix, validates frame
boundaries, converts active video to v210, and delivers embedded audio and ancillary
data to the shared media pipeline. Live Generator sweeps subsequently validated all 13
supported non-3G modes: 525i59.94, 625i50, 720p50/59.94/60, 1080p23.98/24/25/29.97/30,
and 1080i50/59.94/60. Every run completed with zero malformed frames and zero discarded
bytes. ST 272 and ST 299 audio both produced 16 channels, while audio data packets were
kept out of the Analyzer's general ANC inventory.
