#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t read_u32_le(const unsigned char bytes[4])
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

int dtu351_load_fx3_firmware(dtu351_device *device, const char *bundle_path)
{
    static const unsigned char magic[8] = {'D','T','U','3','5','1','F','1'};
    unsigned char header[12];
    unsigned char record_header[8];
    unsigned char *data = NULL;
    FILE *file;
    uint32_t record_count;
    int result = DTU351_ERROR_FIRMWARE;

    if (device == NULL || bundle_path == NULL)
        return DTU351_ERROR_INVALID;
    file = fopen(bundle_path, "rb");
    if (file == NULL)
        return DTU351_ERROR_FIRMWARE;
    if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
        memcmp(header, magic, sizeof(magic)) != 0)
        goto done;
    record_count = read_u32_le(header + 8);
    if (record_count == 0 || record_count > 64)
        goto done;
    for (uint32_t record = 0; record < record_count; ++record) {
        uint32_t address;
        uint32_t length;
        if (fread(record_header, 1, sizeof(record_header), file) != sizeof(record_header))
            goto done;
        address = read_u32_le(record_header);
        length = read_u32_le(record_header + 4);
        if (length > 16u * 1024u * 1024u)
            goto done;
        free(data);
        data = length > 0 ? malloc(length) : NULL;
        if (length > 0 && (data == NULL || fread(data, 1, length, file) != length))
            goto done;
        for (uint32_t offset = 0; offset < length || (length == 0 && offset == 0);) {
            uint32_t remaining = length - offset;
            uint16_t chunk = remaining < 4096u ? (uint16_t)remaining : 4096u;
            result = dtu351_control(device, LIBUSB_ENDPOINT_OUT, DTU_FX3_RAM_WRITE,
                                    (uint16_t)((address + offset) & 0xffffu),
                                    (uint16_t)((address + offset) >> 16),
                                    data == NULL ? NULL : data + offset, chunk, 5000);
            if (result != DTU351_OK)
                goto done;
            if (length == 0)
                break;
            offset += chunk;
        }
    }
    result = DTU351_OK;
done:
    free(data);
    fclose(file);
    return result;
}

int dtu351_load_fpga_firmware(dtu351_device *device, const char *image_path)
{
    /* The Linux driver derives 512 bytes from bMaxPacketSize0 for SuperSpeed
       control transfers. Larger libusb requests complete but the DTU-351 FPGA
       loader silently fails to consume them correctly. */
    unsigned char buffer[512];
    FILE *file;
    int result;
    size_t length;
    if (device == NULL || image_path == NULL)
        return DTU351_ERROR_INVALID;
    file = fopen(image_path, "rb");
    if (file == NULL)
        return DTU351_ERROR_FIRMWARE;
    result = dtu351_control(device, LIBUSB_ENDPOINT_OUT, DTU_USB3_PNP_CMD,
                            DTU_PNP_CMD_FPGA_UPLOAD, DTU_FPGA_UPLOAD_START,
                            NULL, 0, 1000);
    while (result == DTU351_OK && (length = fread(buffer, 1, sizeof(buffer), file)) > 0)
        result = dtu351_control(device, LIBUSB_ENDPOINT_OUT, DTU_USB3_FPGA_UPLOAD,
                                0, 0, buffer, (uint16_t)length, 5000);
    if (ferror(file) && result == DTU351_OK)
        result = DTU351_ERROR_FIRMWARE;
    fclose(file);
    if (result != DTU351_OK)
        return result;
    return dtu351_control(device, LIBUSB_ENDPOINT_OUT, DTU_USB3_PNP_CMD,
                          DTU_PNP_CMD_FPGA_UPLOAD, DTU_FPGA_UPLOAD_FINISH,
                          NULL, 0, 1000);
}
