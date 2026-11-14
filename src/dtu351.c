#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void sleep_ms(long milliseconds)
{
    struct timespec delay = {milliseconds / 1000, (milliseconds % 1000) * 1000000};
    (void)nanosleep(&delay, NULL);
}

static int supported_product(uint16_t product_id)
{
    return product_id == DTU351_PRODUCT_ID || product_id == DTU351_FACTORY_PRODUCT_ID;
}

static void read_string(libusb_device_handle *handle, uint8_t index,
                        char *destination, size_t capacity)
{
    int result;
    if (capacity == 0)
        return;
    destination[0] = '\0';
    if (index == 0)
        return;
    result = libusb_get_string_descriptor_ascii(handle, index,
                                                 (unsigned char *)destination,
                                                 (int)capacity - 1);
    if (result > 0)
        destination[result] = '\0';
}

static void fill_info(libusb_device *usb_device, libusb_device_handle *handle,
                      const struct libusb_device_descriptor *descriptor,
                      dtu351_device_info *info)
{
    memset(info, 0, sizeof(*info));
    info->vendor_id = descriptor->idVendor;
    info->product_id = descriptor->idProduct;
    info->bus_number = libusb_get_bus_number(usb_device);
    info->device_address = libusb_get_device_address(usb_device);
    info->usb_speed = libusb_get_device_speed(usb_device);
    if (handle != NULL) {
        read_string(handle, descriptor->iManufacturer, info->manufacturer,
                    sizeof(info->manufacturer));
        read_string(handle, descriptor->iProduct, info->product, sizeof(info->product));
        read_string(handle, descriptor->iSerialNumber, info->serial, sizeof(info->serial));
    }
}

int dtu351_enumerate(dtu351_device_info *devices, size_t capacity, size_t *count)
{
    libusb_context *context = NULL;
    libusb_device **list = NULL;
    ssize_t list_count;
    size_t found = 0;
    int result;

    if (count == NULL || (capacity > 0 && devices == NULL))
        return DTU351_ERROR_INVALID;
    *count = 0;
    result = libusb_init(&context);
    if (result != LIBUSB_SUCCESS)
        return result;
    list_count = libusb_get_device_list(context, &list);
    if (list_count < 0) {
        libusb_exit(context);
        return (int)list_count;
    }
    for (ssize_t index = 0; index < list_count; ++index) {
        struct libusb_device_descriptor descriptor;
        libusb_device_handle *handle = NULL;
        if (libusb_get_device_descriptor(list[index], &descriptor) != LIBUSB_SUCCESS)
            continue;
        if (descriptor.idVendor != DTU351_VENDOR_ID ||
            !supported_product(descriptor.idProduct))
            continue;
        if (found < capacity) {
            (void)libusb_open(list[index], &handle);
            fill_info(list[index], handle, &descriptor, &devices[found]);
            if (handle != NULL)
                libusb_close(handle);
        }
        ++found;
    }
    libusb_free_device_list(list, 1);
    libusb_exit(context);
    *count = found;
    return DTU351_OK;
}

static int find_bulk_input(libusb_device *usb_device, int *interface_number,
                           uint8_t *endpoint)
{
    struct libusb_config_descriptor *configuration = NULL;
    int result = libusb_get_active_config_descriptor(usb_device, &configuration);
    if (result != LIBUSB_SUCCESS)
        result = libusb_get_config_descriptor(usb_device, 0, &configuration);
    if (result != LIBUSB_SUCCESS)
        return result;
    result = DTU351_ERROR_PROTOCOL;
    for (uint8_t i = 0; i < configuration->bNumInterfaces; ++i) {
        const struct libusb_interface *interface = &configuration->interface[i];
        for (int a = 0; a < interface->num_altsetting; ++a) {
            const struct libusb_interface_descriptor *alternate = &interface->altsetting[a];
            for (uint8_t e = 0; e < alternate->bNumEndpoints; ++e) {
                const struct libusb_endpoint_descriptor *candidate = &alternate->endpoint[e];
                if ((candidate->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) ==
                        LIBUSB_TRANSFER_TYPE_BULK &&
                    (candidate->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) ==
                        LIBUSB_ENDPOINT_IN) {
                    *interface_number = alternate->bInterfaceNumber;
                    *endpoint = candidate->bEndpointAddress;
                    result = DTU351_OK;
                    goto done;
                }
            }
        }
    }
done:
    libusb_free_config_descriptor(configuration);
    return result;
}

int dtu351_open_first(dtu351_device **out, dtu351_device_info *info)
{
    dtu351_device *device;
    libusb_device **list = NULL;
    ssize_t list_count;
    int result;

    if (out == NULL)
        return DTU351_ERROR_INVALID;
    *out = NULL;
    device = calloc(1, sizeof(*device));
    if (device == NULL)
        return LIBUSB_ERROR_NO_MEM;
    device->interface_number = -1;
    device->bulk_in_endpoint = DTU351_BULK_IN_ENDPOINT;
    result = libusb_init(&device->context);
    if (result != LIBUSB_SUCCESS)
        goto fail;
    list_count = libusb_get_device_list(device->context, &list);
    if (list_count < 0) {
        result = (int)list_count;
        goto fail;
    }
    result = DTU351_ERROR_NOT_FOUND;
    for (ssize_t index = 0; index < list_count; ++index) {
        struct libusb_device_descriptor descriptor;
        if (libusb_get_device_descriptor(list[index], &descriptor) != LIBUSB_SUCCESS)
            continue;
        if (descriptor.idVendor != DTU351_VENDOR_ID ||
            !supported_product(descriptor.idProduct))
            continue;
        result = libusb_open(list[index], &device->handle);
        if (result != LIBUSB_SUCCESS)
            break;
        if (info != NULL)
            fill_info(list[index], device->handle, &descriptor, info);
        result = find_bulk_input(list[index], &device->interface_number,
                                 &device->bulk_in_endpoint);
        /* A cold DTU-351 uses the same PID but identifies as Cypress/WestBridge and
           has no streaming endpoint until FX3 firmware has been uploaded. */
        if (result != DTU351_OK && info != NULL &&
            strcmp(info->manufacturer, "Cypress") != 0)
            break;
        if (device->interface_number < 0)
            device->interface_number = 0;
        result = libusb_claim_interface(device->handle, device->interface_number);
        if (result == LIBUSB_SUCCESS) {
            result = libusb_set_interface_alt_setting(device->handle,
                                                       device->interface_number, 0);
            if (result == LIBUSB_SUCCESS) {
                *out = device;
                break;
            }
        }
        break;
    }
    libusb_free_device_list(list, 1);
    list = NULL;
    if (*out != NULL)
        return DTU351_OK;
fail:
    if (list != NULL)
        libusb_free_device_list(list, 1);
    dtu351_close(device);
    return result;
}

void dtu351_close(dtu351_device *device)
{
    if (device == NULL)
        return;
    if (device->handle != NULL) {
        if (device->capture_active)
            (void)dtu351_stop_capture(device);
        if (device->interface_number >= 0)
            (void)libusb_release_interface(device->handle, device->interface_number);
        libusb_close(device->handle);
    }
    if (device->context != NULL)
        libusb_exit(device->context);
    free(device);
}

int dtu351_control(dtu351_device *device, uint8_t direction, uint8_t request,
                   uint16_t value, uint16_t index, void *data, uint16_t length,
                   unsigned timeout_ms)
{
    int transferred;
    uint8_t request_type;
    if (device == NULL || device->handle == NULL)
        return DTU351_ERROR_INVALID;
    request_type = direction | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE;
    transferred = libusb_control_transfer(device->handle, request_type, request, value,
                                          index, data, length, timeout_ms);
    if (transferred < 0)
        return transferred;
    return transferred == length ? DTU351_OK : DTU351_ERROR_PROTOCOL;
}

int dtu351_read_register(dtu351_device *device, dtu351_register_device target,
                         uint16_t address, uint16_t *value)
{
    unsigned char bytes[2] = {0, 0};
    int result;
    if (value == NULL)
        return DTU351_ERROR_INVALID;
    result = dtu351_control(device, LIBUSB_ENDPOINT_IN, DTU_USB3_READ_VALUE, address,
                            (uint16_t)target, bytes, sizeof(bytes), 1000);
    if (result == DTU351_OK)
        *value = (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
    return result;
}

int dtu351_write_register(dtu351_device *device, dtu351_register_device target,
                          uint16_t address, uint16_t value)
{
    unsigned char bytes[2] = {(unsigned char)(value & 0xffu),
                              (unsigned char)(value >> 8)};
    return dtu351_control(device, LIBUSB_ENDPOINT_OUT, DTU_USB3_WRITE_VALUE, address,
                          (uint16_t)target, bytes, sizeof(bytes), 1000);
}

int dtu351_get_reported_usb_speed(dtu351_device *device, uint16_t *speed)
{
    return dtu351_read_register(device, DTU351_REG_FX3, 0x00, speed);
}

int dtu351_reset_device_state(dtu351_device *device)
{
    return dtu351_control(device, LIBUSB_ENDPOINT_OUT, DTU_USB3_PNP_CMD,
                          DTU_PNP_CMD_RESET, DTU_RESET_DVC_STATE, NULL, 0, 1000);
}

int dtu351_power_on(dtu351_device *device)
{
    return dtu351_control(device, LIBUSB_ENDPOINT_OUT, DTU_USB3_PNP_CMD,
                          DTU_PNP_CMD_DVC_POWER, DTU_DVC_POWER_ON, NULL, 0, 1000);
}

int dtu351_initialize_runtime(dtu351_device *device, const char *fpga_image_path)
{
    uint16_t usb_speed;
    uint16_t value;
    int result;
    if (device == NULL || fpga_image_path == NULL)
        return DTU351_ERROR_INVALID;
    result = dtu351_reset_device_state(device);
    if (result != DTU351_OK)
        return result;
    result = dtu351_get_reported_usb_speed(device, &usb_speed);
    if (result != DTU351_OK)
        return result;
    /* DekTec's FX3 firmware reports 2 for SuperSpeed. */
    if (usb_speed != 2)
        return DTU351_ERROR_USB_SPEED;
    result = dtu351_power_on(device);
    if (result != DTU351_OK)
        return result;
    sleep_ms(100);
    result = dtu351_load_fpga_firmware(device, fpga_image_path);
    if (result != DTU351_OK)
        return result;
    sleep_ms(100);
    result = dtu351_read_register(device, DTU351_REG_FPGA, 0x00, &value);
    if (result != DTU351_OK)
        return result;
    if (value != 351u)
        return DTU351_ERROR_FIRMWARE;
    result = dtu351_write_register(device, DTU351_REG_FPGA, 0x06, 0);
    if (result != DTU351_OK)
        return result;
    result = dtu351_read_register(device, DTU351_REG_GS1661, 0x24, &value);
    if (result != DTU351_OK)
        return result;
    result = dtu351_write_register(device, DTU351_REG_GS1661, 0x24,
                                   (uint16_t)(value | 0x04u));
    if (result != DTU351_OK)
        return result;
    result = dtu351_read_register(device, DTU351_REG_GS1661, 0x00, &value);
    if (result != DTU351_OK)
        return result;
    return dtu351_write_register(device, DTU351_REG_GS1661, 0x00,
                                 (uint16_t)(value | 0x081fu));
}

int dtu351_detect_video(dtu351_device *device, dtu351_video_standard *standard,
                        uint16_t *ds1_out, uint16_t *raster_out)
{
    uint16_t ds1;
    uint16_t raster;
    uint16_t rate;
    int result;
    int fractional;
    unsigned code;
    if (device == NULL || standard == NULL)
        return DTU351_ERROR_INVALID;
    *standard = DTU351_VIDEO_UNKNOWN;
    result = dtu351_read_register(device, DTU351_REG_GS1661, 0x24, &rate);
    if (result != DTU351_OK)
        return result;
    if ((rate & 0x04u) == 0) {
        result = dtu351_write_register(device, DTU351_REG_GS1661, 0x24,
                                       (uint16_t)(rate | 0x04u));
        if (result != DTU351_OK)
            return result;
    }
    result = dtu351_read_register(device, DTU351_REG_GS1661, 0x06, &ds1);
    if (result != DTU351_OK)
        return result;
    result = dtu351_read_register(device, DTU351_REG_GS1661, 0x22, &raster);
    if (result != DTU351_OK)
        return result;
    if (ds1_out != NULL)
        *ds1_out = ds1;
    if (raster_out != NULL)
        *raster_out = raster;
    if ((raster & 0x1000u) == 0)
        return DTU351_OK;
    fractional = (raster & 0x2000u) != 0;
    code = (ds1 >> 8) & 0x3fu;
    if (code == 0x00u)
        *standard = fractional ? DTU351_VIDEO_720P59_94 : DTU351_VIDEO_720P60;
    else if (code == 0x04u)
        *standard = DTU351_VIDEO_720P50;
    else if (code == 0x0au)
        *standard = fractional ? DTU351_VIDEO_1080I59_94 : DTU351_VIDEO_1080I60;
    else if (code == 0x0bu)
        *standard = fractional ? DTU351_VIDEO_1080P29_97 : DTU351_VIDEO_1080P30;
    else if (code == 0x0cu)
        *standard = DTU351_VIDEO_1080I50;
    else if (code == 0x0du)
        *standard = DTU351_VIDEO_1080P25;
    else if (code == 0x10u)
        *standard = fractional ? DTU351_VIDEO_1080P23_98 : DTU351_VIDEO_1080P24;
    else if (code == 0x16u)
        *standard = DTU351_VIDEO_525I59_94;
    else if (code == 0x18u)
        *standard = DTU351_VIDEO_625I50;
    else
        *standard = DTU351_VIDEO_OTHER_LOCKED;
    return DTU351_OK;
}

const char *dtu351_video_standard_name(dtu351_video_standard standard)
{
    switch (standard) {
    case DTU351_VIDEO_525I59_94: return "525i59.94";
    case DTU351_VIDEO_625I50: return "625i50";
    case DTU351_VIDEO_720P50: return "720p50";
    case DTU351_VIDEO_720P59_94: return "720p59.94";
    case DTU351_VIDEO_720P60: return "720p60";
    case DTU351_VIDEO_1080P23_98: return "1080p23.98";
    case DTU351_VIDEO_1080P24: return "1080p24";
    case DTU351_VIDEO_1080P25: return "1080p25";
    case DTU351_VIDEO_1080P29_97: return "1080p29.97";
    case DTU351_VIDEO_1080P30: return "1080p30";
    case DTU351_VIDEO_1080I59_94: return "1080i59.94";
    case DTU351_VIDEO_1080I60: return "1080i60";
    case DTU351_VIDEO_1080I50: return "1080i50";
    case DTU351_VIDEO_OTHER_LOCKED: return "locked (other supported standard)";
    default: return "unlocked/unknown";
    }
}

int dtu351_start_capture(dtu351_device *device, int high_definition,
                         uint16_t receive_mode)
{
    int result;
    if (device == NULL || receive_mode != DTU351_RXMODE_SDI_FULL_10B)
        return DTU351_ERROR_INVALID;
    if (getenv("DTU351_KEEP_AUTO_RATE") == NULL) {
        result = dtu351_write_register(device, DTU351_REG_GS1661, 0x24,
                                       high_definition ? 0u : 1u);
        if (result != DTU351_OK)
            return result;
    }
    result = dtu351_write_register(device, DTU351_REG_FPGA, 0x04,
                                   high_definition ? 1u : 0u);
    if (result != DTU351_OK)
        return result;
    result = dtu351_write_register(device, DTU351_REG_FPGA, 0x06, receive_mode);
    if (result == DTU351_OK)
        device->capture_active = 1;
    return result;
}

int dtu351_prepare_capture(dtu351_device *device)
{
    if (device == NULL)
        return DTU351_ERROR_INVALID;
    /* Match DekTec's DTU-351 worker exactly: queue reads on the already-open
       endpoint without issuing CLEAR_FEATURE(ENDPOINT_HALT).  On FX3 firmware,
       clearing a USB endpoint can reset endpoint/DMA state independently of
       the FPGA receive-mode state.  A device-state reset is already performed
       when capture stops. */
    return DTU351_OK;
}

int dtu351_read(dtu351_device *device, void *buffer, size_t capacity,
                size_t *bytes_read, unsigned timeout_ms)
{
    int transferred = 0;
    int result;
    if (device == NULL || buffer == NULL || bytes_read == NULL ||
        capacity == 0 || capacity > (size_t)INT32_MAX)
        return DTU351_ERROR_INVALID;
    *bytes_read = 0;
    result = libusb_bulk_transfer(device->handle, device->bulk_in_endpoint, buffer,
                                  (int)capacity, &transferred, timeout_ms);
    if (result == LIBUSB_SUCCESS || result == LIBUSB_ERROR_TIMEOUT)
        *bytes_read = transferred > 0 ? (size_t)transferred : 0;
    return result;
}

int dtu351_stop_capture(dtu351_device *device)
{
    int result;
    if (device == NULL)
        return DTU351_ERROR_INVALID;
    /* The official worker resets device state to stop/flush DMA, then restores
       the GS1661 rate selector to automatic detection. */
    result = dtu351_reset_device_state(device);
    if (result == DTU351_OK)
        result = dtu351_write_register(device, DTU351_REG_GS1661, 0x24, 0x04);
    device->capture_active = 0;
    return result;
}

const char *dtu351_error_name(int status)
{
    switch (status) {
    case DTU351_OK: return "success";
    case DTU351_ERROR_INVALID: return "invalid argument";
    case DTU351_ERROR_NOT_FOUND: return "DTU-351 not found";
    case DTU351_ERROR_PROTOCOL: return "unexpected USB transfer length or descriptors";
    case DTU351_ERROR_FIRMWARE: return "invalid firmware image";
    case DTU351_ERROR_USB_SPEED: return "device is not operating at USB SuperSpeed";
    case DTU351_ERROR_RECONFIGURE: return "capture reconfiguration requested";
    default:
        return status < 0 && status >= LIBUSB_ERROR_OTHER
            ? libusb_error_name(status) : "unknown error";
    }
}
