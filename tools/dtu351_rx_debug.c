#include "dtu351/dtu351.h"

#include <stdio.h>
#include <time.h>

int main(void)
{
    dtu351_device *device = NULL;
    struct timespec delay = {0, 100000000};
    int result = dtu351_open_first(&device, NULL);
    if (result != DTU351_OK)
        return 1;
    result = dtu351_start_capture(device, 1, DTU351_RXMODE_SDI_FULL_10B);
    for (int n = 0; result == DTU351_OK && n < 10; ++n) {
        uint16_t rx = 0, frame = 0, definition = 0, rate = 0;
        (void)nanosleep(&delay, NULL);
        (void)dtu351_read_register(device, DTU351_REG_FPGA, 0x04, &definition);
        (void)dtu351_read_register(device, DTU351_REG_FPGA, 0x06, &rx);
        (void)dtu351_read_register(device, DTU351_REG_FPGA, 0x0a, &frame);
        (void)dtu351_read_register(device, DTU351_REG_GS1661, 0x24, &rate);
        printf("%d definition=0x%04x rx=0x%04x frame=%u rate=0x%04x\n",
               n, definition, rx, frame, rate);
    }
    (void)dtu351_stop_capture(device);
    dtu351_close(device);
    return result == DTU351_OK ? 0 : 1;
}
