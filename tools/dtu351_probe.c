#include "dtu351/dtu351.h"

#include <stdio.h>

int main(void)
{
    dtu351_device_info devices[16];
    size_t count = 0;
    int result = dtu351_enumerate(devices, 16, &count);
    if (result != DTU351_OK) {
        fprintf(stderr, "enumeration failed: %s (%d)\n", dtu351_error_name(result), result);
        return 1;
    }
    if (count == 0) {
        puts("No DTU-351 detected.");
        return 2;
    }
    for (size_t index = 0; index < count && index < 16; ++index) {
        const dtu351_device_info *device = &devices[index];
        printf("DTU-351 %04x:%04x bus=%u address=%u speed=%d",
               device->vendor_id, device->product_id, device->bus_number,
               device->device_address, device->usb_speed);
        if (device->serial[0] != '\0')
            printf(" serial=%s", device->serial);
        printf(" manufacturer=%s product=%s\n",
               device->manufacturer[0] ? device->manufacturer : "(unavailable)",
               device->product[0] ? device->product : "(unavailable)");
    }
    return 0;
}
