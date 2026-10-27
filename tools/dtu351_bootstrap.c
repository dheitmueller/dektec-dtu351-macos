#include "dtu351/dtu351.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static void pause_ms(long milliseconds)
{
    struct timespec delay = {milliseconds / 1000, (milliseconds % 1000) * 1000000};
    (void)nanosleep(&delay, NULL);
}

static int open_wait(dtu351_device **device, dtu351_device_info *info, int runtime)
{
    for (int attempt = 0; attempt < 100; ++attempt) {
        int result = dtu351_open_first(device, info);
        if (result == DTU351_OK) {
            int is_runtime = strcmp(info->manufacturer, "DekTec") == 0;
            if (is_runtime == runtime)
                return DTU351_OK;
            dtu351_close(*device);
            *device = NULL;
        }
        pause_ms(100);
    }
    return DTU351_ERROR_NOT_FOUND;
}

int main(int argc, char **argv)
{
    dtu351_device *device = NULL;
    dtu351_device_info info;
    dtu351_video_standard standard;
    uint16_t speed = 0;
    uint16_t ds1 = 0;
    uint16_t raster = 0;
    int result;
    if (argc != 3) {
        fprintf(stderr, "usage: %s DTU351_FX3.bin DTU351_FPGA.bin\n", argv[0]);
        return 64;
    }
    result = dtu351_open_first(&device, &info);
    if (result != DTU351_OK) {
        fprintf(stderr, "open failed: %s (%d)\n", dtu351_error_name(result), result);
        return 1;
    }
    printf("found %s %s (%04x:%04x)\n", info.manufacturer, info.product,
           info.vendor_id, info.product_id);
    if (strcmp(info.manufacturer, "Cypress") == 0) {
        puts("uploading FX3 firmware and waiting for re-enumeration...");
        result = dtu351_load_fx3_firmware(device, argv[1]);
        dtu351_close(device);
        device = NULL;
        if (result != DTU351_OK) {
            fprintf(stderr, "FX3 upload failed: %s (%d)\n",
                    dtu351_error_name(result), result);
            return 1;
        }
        result = open_wait(&device, &info, 1);
        if (result != DTU351_OK) {
            fprintf(stderr, "runtime device did not reattach: %s (%d)\n",
                    dtu351_error_name(result), result);
            return 1;
        }
        printf("reattached as %s %s\n", info.manufacturer, info.product);
        /* The USB service can become openable slightly before the FX3 runtime
           has finished bringing up its internal control path. */
        pause_ms(250);
    }
    result = dtu351_initialize_runtime(device, argv[2]);
    if (result != DTU351_OK) {
        fprintf(stderr, "runtime initialization failed: %s (%d)\n",
                dtu351_error_name(result), result);
        dtu351_close(device);
        return 1;
    }
    result = dtu351_get_reported_usb_speed(device, &speed);
    if (result == DTU351_OK)
        printf("FX3 USB speed code: %u (2 is SuperSpeed)\n", speed);
    pause_ms(250);
    result = dtu351_detect_video(device, &standard, &ds1, &raster);
    if (result != DTU351_OK) {
        fprintf(stderr, "video detection failed: %s (%d)\n",
                dtu351_error_name(result), result);
        dtu351_close(device);
        return 1;
    }
    printf("input: %s (DS1=0x%04x raster=0x%04x)\n",
           dtu351_video_standard_name(standard), ds1, raster);
    dtu351_close(device);
    return standard == DTU351_VIDEO_UNKNOWN ? 2 : 0;
}
