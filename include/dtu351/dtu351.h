#ifndef DTU351_DTU351_H
#define DTU351_DTU351_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DTU351_VENDOR_ID 0x1297u
#define DTU351_PRODUCT_ID 0x0333u
#define DTU351_FACTORY_PRODUCT_ID 0x00f3u
#define DTU351_BULK_IN_ENDPOINT 0x83u
#define DTU351_TRANSFER_SIZE (512u * 1024u)

/* DTU-351 FPGA register 0x06 values.  These are hardware encodings, not the
   public DTAPI_RXMODE_* flags accepted by DekTec's user-space library. */
#define DTU351_RXMODE_SDI_FULL_10B 0x0002u

typedef struct dtu351_device dtu351_device;

typedef struct dtu351_device_info {
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t bus_number;
    uint8_t device_address;
    int usb_speed;
    char manufacturer[64];
    char product[64];
    char serial[64];
} dtu351_device_info;

typedef enum dtu351_register_device {
    DTU351_REG_FX3 = 0x01,
    DTU351_REG_FPGA = 0x02,
    DTU351_REG_GS1661 = 0x03,
} dtu351_register_device;

typedef enum dtu351_video_standard {
    DTU351_VIDEO_UNKNOWN = 0,
    DTU351_VIDEO_525I59_94,
    DTU351_VIDEO_625I50,
    DTU351_VIDEO_720P50,
    DTU351_VIDEO_720P59_94,
    DTU351_VIDEO_720P60,
    DTU351_VIDEO_1080P23_98,
    DTU351_VIDEO_1080P24,
    DTU351_VIDEO_1080P25,
    DTU351_VIDEO_1080P29_97,
    DTU351_VIDEO_1080P30,
    DTU351_VIDEO_1080I59_94,
    DTU351_VIDEO_1080I60,
    DTU351_VIDEO_1080I50,
    DTU351_VIDEO_OTHER_LOCKED,
} dtu351_video_standard;

typedef int (*dtu351_data_callback)(const void *data, size_t length, void *opaque);
typedef int (*dtu351_monitor_callback)(void *opaque);

typedef enum dtu351_transport {
    DTU351_TRANSPORT_BULK = 0,
    DTU351_TRANSPORT_ISOCHRONOUS = 1,
} dtu351_transport;

enum {
    DTU351_OK = 0,
    DTU351_ERROR_INVALID = -1000,
    DTU351_ERROR_NOT_FOUND = -1001,
    DTU351_ERROR_PROTOCOL = -1002,
    DTU351_ERROR_FIRMWARE = -1003,
    DTU351_ERROR_USB_SPEED = -1004,
    DTU351_ERROR_RECONFIGURE = -1005,
};

int dtu351_enumerate(dtu351_device_info *devices, size_t capacity, size_t *count);
int dtu351_open_first(dtu351_device **out, dtu351_device_info *info);
void dtu351_close(dtu351_device *device);

int dtu351_read_register(dtu351_device *device, dtu351_register_device target,
                         uint16_t address, uint16_t *value);
int dtu351_write_register(dtu351_device *device, dtu351_register_device target,
                          uint16_t address, uint16_t value);
int dtu351_get_reported_usb_speed(dtu351_device *device, uint16_t *speed);
int dtu351_reset_device_state(dtu351_device *device);
int dtu351_power_on(dtu351_device *device);

int dtu351_load_fx3_firmware(dtu351_device *device, const char *bundle_path);
int dtu351_load_fpga_firmware(dtu351_device *device, const char *image_path);
int dtu351_initialize_runtime(dtu351_device *device, const char *fpga_image_path);
int dtu351_detect_video(dtu351_device *device, dtu351_video_standard *standard,
                        uint16_t *ds1, uint16_t *raster_structure);
const char *dtu351_video_standard_name(dtu351_video_standard standard);

int dtu351_start_capture(dtu351_device *device, int high_definition,
                         uint16_t receive_mode);
int dtu351_prepare_capture(dtu351_device *device);
int dtu351_read(dtu351_device *device, void *buffer, size_t capacity,
                size_t *bytes_read, unsigned timeout_ms);
int dtu351_stop_capture(dtu351_device *device);
int dtu351_capture_run(dtu351_device *device, int high_definition,
                       uint16_t receive_mode, dtu351_transport transport,
                       unsigned queue_depth,
                       unsigned long transfer_limit,
                       dtu351_data_callback callback, void *opaque);
int dtu351_capture_run_monitored(
                       dtu351_device *device, int high_definition,
                       uint16_t receive_mode, dtu351_transport transport,
                       unsigned queue_depth,
                       unsigned long transfer_limit,
                       dtu351_data_callback callback,
                       dtu351_monitor_callback monitor,
                       unsigned monitor_interval_ms, void *opaque);

const char *dtu351_error_name(int status);

#ifdef __cplusplus
}
#endif
#endif
