# DTU-351 firmware

The two generated files in this directory are extracted without modification from
DekTec's BSD-licensed Linux driver source. They remain covered by DekTec's license in
`DEKTEC-LICENSE`.

Regenerate them with:

```sh
python3 scripts/extract_dtu351_firmware.py \
  vendor/dektec-linux-sdk/Drivers/Dtu/Source/DtuFwStore.c firmware
```

`dtu351-fx3.bin` is a small container holding the four address/data records and final
entry-point record used by `EzUsbLoadFirmwareFx3`. `dtu351-fpga.bin` is the original
`PLD_FIRMWARE_DTU351` byte array.
