#ifndef DTU351_INTERNAL_H
#define DTU351_INTERNAL_H

#include <libusb.h>
#include <stdint.h>

#include "dtu351/dtu351.h"

struct dtu351_device {
    libusb_context *context;
    libusb_device_handle *handle;
    int interface_number;
    uint8_t bulk_in_endpoint;
    int capture_active;
};

enum {
    DTU_USB3_FPGA_UPLOAD = 0xd0,
    DTU_USB3_PNP_CMD = 0xd1,
    DTU_USB3_READ_VALUE = 0xd2,
    DTU_USB3_WRITE_VALUE = 0xd3,
    DTU_FX3_RAM_WRITE = 0xa0,
    DTU_PNP_CMD_RESET = 0x01,
    DTU_PNP_CMD_DVC_POWER = 0x02,
    DTU_PNP_CMD_FPGA_UPLOAD = 0x03,
    DTU_RESET_DVC_STATE = 0x02,
    DTU_RESET_DMA_IN = 0x04,
    DTU_DVC_POWER_ON = 0x01,
    DTU_FPGA_UPLOAD_START = 0x01,
    DTU_FPGA_UPLOAD_FINISH = 0x02,
};

int dtu351_control(dtu351_device *device, uint8_t direction, uint8_t request,
                   uint16_t value, uint16_t index, void *data, uint16_t length,
                   unsigned timeout_ms);

#endif
