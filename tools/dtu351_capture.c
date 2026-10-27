#include "dtu351/dtu351.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct capture_sink {
    FILE *output;
    size_t total;
} capture_sink;

static int write_capture(const void *data, size_t length, void *opaque)
{
    capture_sink *sink = opaque;
    if (fwrite(data, 1, length, sink->output) != length)
        return 1;
    sink->total += length;
    fprintf(stderr, "\r%zu bytes", sink->total);
    return 0;
}

int main(int argc, char **argv)
{
    dtu351_device *device = NULL;
    dtu351_device_info info;
    FILE *output = NULL;
    unsigned long transfer_limit = 0;
    unsigned long receive_mode = DTU351_RXMODE_SDI_FULL_10B;
    dtu351_transport transport = DTU351_TRANSPORT_BULK;
    capture_sink sink = {0};
    int result;

    if ((argc != 3 && argc != 4) ||
        (strcmp(argv[1], "hd") != 0 && strcmp(argv[1], "sd") != 0)) {
        fprintf(stderr, "usage: %s hd|sd OUTPUT.raw [TRANSFER_COUNT]\n", argv[0]);
        return 64;
    }
    if (argc == 4) {
        char *end = NULL;
        transfer_limit = strtoul(argv[3], &end, 10);
        if (transfer_limit == 0 || end == argv[3] || *end != '\0') {
            fprintf(stderr, "TRANSFER_COUNT must be a positive integer\n");
            return 64;
        }
    }
    {
        const char *mode_text = getenv("DTU351_RX_MODE");
        if (mode_text != NULL && *mode_text != '\0') {
            char *end = NULL;
            receive_mode = strtoul(mode_text, &end, 0);
            if (receive_mode != DTU351_RXMODE_SDI_FULL_10B ||
                end == mode_text || *end != '\0') {
                fprintf(stderr, "DTU351_RX_MODE must be 2 (full-frame packed 10-bit)\n");
                return 64;
            }
        }
    }
    {
        const char *transport_text = getenv("DTU351_TRANSPORT");
        if (transport_text != NULL && strcmp(transport_text, "iso") == 0)
            transport = DTU351_TRANSPORT_ISOCHRONOUS;
        else if (transport_text != NULL && strcmp(transport_text, "bulk") != 0) {
            fprintf(stderr, "DTU351_TRANSPORT must be bulk or iso\n");
            return 64;
        }
    }
    result = dtu351_open_first(&device, &info);
    if (result != DTU351_OK) {
        fprintf(stderr, "open failed: %s (%d)\n", dtu351_error_name(result), result);
        return 1;
    }
    output = fopen(argv[2], "wb");
    if (output == NULL) {
        fprintf(stderr, "unable to allocate capture output\n");
        result = 1;
        goto done;
    }
    sink.output = output;
    fprintf(stderr, "capturing with 8 queued %s transfers, rx_mode=0x%04lx; "
                    "press Control-C to stop\n",
                    transport == DTU351_TRANSPORT_BULK ? "bulk" : "isochronous",
                    receive_mode);
    result = dtu351_capture_run(device, strcmp(argv[1], "hd") == 0,
                                (uint16_t)receive_mode, transport, 8, transfer_limit,
                                write_capture, &sink);
    fputc('\n', stderr);
    if (result != DTU351_OK)
        fprintf(stderr, "read failed: %s (%d)\n", dtu351_error_name(result), result);
done:
    if (device != NULL)
        dtu351_close(device);
    if (output != NULL)
        fclose(output);
    return result == DTU351_OK ? 0 : 1;
}
