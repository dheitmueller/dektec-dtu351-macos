#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

static double monotonic_seconds(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}

static void debug_capture_registers(dtu351_device *device, unsigned sample)
{
    uint16_t pnp = 0xffff, phy = 0xffff, link = 0xffff;
    uint16_t definition = 0xffff, rx_mode = 0xffff, frame = 0xffff;
    uint16_t rate = 0xffff, ds1 = 0xffff, raster = 0xffff;

    (void)dtu351_read_register(device, DTU351_REG_FX3, 0x08, &pnp);
    (void)dtu351_read_register(device, DTU351_REG_FX3, 0x0a, &phy);
    (void)dtu351_read_register(device, DTU351_REG_FX3, 0x0c, &link);
    (void)dtu351_read_register(device, DTU351_REG_FPGA, 0x04, &definition);
    (void)dtu351_read_register(device, DTU351_REG_FPGA, 0x06, &rx_mode);
    (void)dtu351_read_register(device, DTU351_REG_FPGA, 0x0a, &frame);
    (void)dtu351_read_register(device, DTU351_REG_GS1661, 0x24, &rate);
    (void)dtu351_read_register(device, DTU351_REG_GS1661, 0x06, &ds1);
    (void)dtu351_read_register(device, DTU351_REG_GS1661, 0x22, &raster);
    fprintf(stderr,
            "debug[%u]: pnp=0x%04x phy=%u link=%u def=0x%04x "
            "rx=0x%04x frame=%u rate=0x%04x ds1=0x%04x raster=0x%04x\n",
            sample, pnp, phy, link, definition, rx_mode, frame, rate, ds1, raster);
}

typedef struct stream_state stream_state;

typedef struct stream_slot {
    stream_state *state;
    struct libusb_transfer *transfer;
    unsigned char *buffer;
    size_t buffer_size;
    int submitted;
} stream_slot;

struct stream_state {
    dtu351_device *device;
    stream_slot *slots;
    unsigned queue_depth;
    unsigned pending;
    unsigned long completed;
    unsigned long transfer_limit;
    dtu351_data_callback callback;
    dtu351_monitor_callback monitor;
    unsigned monitor_interval_ms;
    void *opaque;
    int stopping;
    int cancel_sent;
    int result;
    double start_time;
    double next_monitor_time;
};

static int transfer_status_error(enum libusb_transfer_status status)
{
    switch (status) {
    case LIBUSB_TRANSFER_TIMED_OUT: return LIBUSB_ERROR_TIMEOUT;
    case LIBUSB_TRANSFER_STALL: return LIBUSB_ERROR_PIPE;
    case LIBUSB_TRANSFER_NO_DEVICE: return LIBUSB_ERROR_NO_DEVICE;
    case LIBUSB_TRANSFER_OVERFLOW: return LIBUSB_ERROR_OVERFLOW;
    default: return LIBUSB_ERROR_IO;
    }
}

static void LIBUSB_CALL transfer_complete(struct libusb_transfer *transfer)
{
    stream_slot *slot = transfer->user_data;
    stream_state *state = slot->state;
    slot->submitted = 0;
    if (state->pending > 0)
        --state->pending;

    if (transfer->status == LIBUSB_TRANSFER_COMPLETED) {
        size_t delivered = 0;
        if (transfer->num_iso_packets == 0 && transfer->actual_length > 0) {
            if (state->callback(transfer->buffer, (size_t)transfer->actual_length,
                                state->opaque) != 0) {
                state->result = LIBUSB_ERROR_IO;
                state->stopping = 1;
            }
            delivered = (size_t)transfer->actual_length;
        }
        for (int packet = 0; packet < transfer->num_iso_packets; ++packet) {
            struct libusb_iso_packet_descriptor *descriptor =
                &transfer->iso_packet_desc[packet];
            if (descriptor->status != LIBUSB_TRANSFER_COMPLETED) {
                state->result = transfer_status_error(descriptor->status);
                state->stopping = 1;
                break;
            }
            if (descriptor->actual_length > 0) {
                unsigned char *packet_data =
                    libusb_get_iso_packet_buffer_simple(transfer, (unsigned)packet);
                if (state->callback(packet_data, descriptor->actual_length,
                                    state->opaque) != 0) {
                    state->result = LIBUSB_ERROR_IO;
                    state->stopping = 1;
                    break;
                }
                delivered += descriptor->actual_length;
            }
        }
        if (delivered > 0) {
            ++state->completed;
            if (state->transfer_limit > 0 && state->completed >= state->transfer_limit)
                state->stopping = 1;
        }
    } else if (transfer->status != LIBUSB_TRANSFER_CANCELLED) {
        state->result = transfer_status_error(transfer->status);
        state->stopping = 1;
    }

    if (!state->stopping) {
        int result = libusb_submit_transfer(transfer);
        if (result == LIBUSB_SUCCESS) {
            slot->submitted = 1;
            ++state->pending;
        } else {
            state->result = result;
            state->stopping = 1;
        }
    }
}

static void cancel_pending(stream_state *state)
{
    if (state->cancel_sent)
        return;
    state->cancel_sent = 1;
    for (unsigned index = 0; index < state->queue_depth; ++index) {
        if (state->slots[index].submitted)
            (void)libusb_cancel_transfer(state->slots[index].transfer);
    }
}

int dtu351_capture_run_monitored(dtu351_device *device, int high_definition,
                       uint16_t receive_mode, dtu351_transport transport,
                       unsigned queue_depth,
                       unsigned long transfer_limit,
                       dtu351_data_callback callback,
                       dtu351_monitor_callback monitor,
                       unsigned monitor_interval_ms, void *opaque)
{
    stream_state state;
    int result;
    if (device == NULL || callback == NULL || queue_depth < 2 || queue_depth > 32 ||
        (transport != DTU351_TRANSPORT_BULK &&
         transport != DTU351_TRANSPORT_ISOCHRONOUS))
        return DTU351_ERROR_INVALID;
    memset(&state, 0, sizeof(state));
    state.device = device;
    state.queue_depth = queue_depth;
    state.transfer_limit = transfer_limit;
    state.callback = callback;
    state.monitor = monitor;
    state.monitor_interval_ms = monitor_interval_ms;
    state.opaque = opaque;
    state.result = DTU351_OK;
    state.slots = calloc(queue_depth, sizeof(*state.slots));
    if (state.slots == NULL)
        return LIBUSB_ERROR_NO_MEM;

    result = dtu351_prepare_capture(device);
    if (result != DTU351_OK)
        goto cleanup;
    if (transport == DTU351_TRANSPORT_ISOCHRONOUS) {
        /* Alt 6 provides 196.6 MB/s for HD; alt 3 provides 41.0 MB/s for SD. */
        int alternate = high_definition ? 6 : 3;
        result = libusb_set_interface_alt_setting(device->handle,
                                                   device->interface_number, alternate);
        if (result != LIBUSB_SUCCESS)
            goto cleanup;
    }
    for (unsigned index = 0; index < queue_depth; ++index) {
        stream_slot *slot = &state.slots[index];
        const int packet_count = 8;
        const unsigned packet_size = high_definition ? 24576u : 5120u;
        slot->state = &state;
        slot->buffer_size = transport == DTU351_TRANSPORT_ISOCHRONOUS
            ? (size_t)packet_count * packet_size : DTU351_TRANSFER_SIZE;
        slot->buffer = malloc(slot->buffer_size);
        slot->transfer = libusb_alloc_transfer(
            transport == DTU351_TRANSPORT_ISOCHRONOUS ? packet_count : 0);
        if (slot->buffer == NULL || slot->transfer == NULL) {
            result = LIBUSB_ERROR_NO_MEM;
            state.stopping = 1;
            break;
        }
        if (transport == DTU351_TRANSPORT_ISOCHRONOUS) {
            libusb_fill_iso_transfer(slot->transfer, device->handle,
                                     device->bulk_in_endpoint, slot->buffer,
                                     (int)slot->buffer_size, packet_count,
                                     transfer_complete, slot, 5000);
            libusb_set_iso_packet_lengths(slot->transfer, packet_size);
        } else {
            libusb_fill_bulk_transfer(slot->transfer, device->handle,
                                      device->bulk_in_endpoint, slot->buffer,
                                      (int)slot->buffer_size, transfer_complete, slot, 5000);
        }
        result = libusb_submit_transfer(slot->transfer);
        if (result != LIBUSB_SUCCESS) {
            state.stopping = 1;
            break;
        }
        slot->submitted = 1;
        ++state.pending;
    }
    if (!state.stopping) {
        /* Give Darwin's IOKit async source one turn before enabling FPGA DMA.
           The Linux driver arms every host request before writing RxMode; on
           macOS submission and run-loop delivery are separate operations. */
        struct timeval arm_time = {0, 100000};
        result = libusb_handle_events_timeout_completed(device->context,
                                                         &arm_time, NULL);
        if (result != LIBUSB_SUCCESS && result != LIBUSB_ERROR_INTERRUPTED)
            state.stopping = 1;
    }
    if (!state.stopping) {
        result = dtu351_start_capture(device, high_definition, receive_mode);
        if (result != DTU351_OK)
            state.stopping = 1;
    }
    state.start_time = monotonic_seconds();
    state.next_monitor_time = state.start_time;
    int debug_registers = getenv("DTU351_DEBUG_REGS") != NULL;
    unsigned debug_sample = 0;
    double next_debug_time = state.start_time;
    while (state.pending > 0) {
        struct timeval poll_time = {0, 100000};
        if (state.stopping)
            cancel_pending(&state);
        result = libusb_handle_events_timeout_completed(device->context, &poll_time, NULL);
        if (result != LIBUSB_SUCCESS && result != LIBUSB_ERROR_INTERRUPTED) {
            state.result = result;
            state.stopping = 1;
        }
        if (!state.stopping && state.monitor &&
            monotonic_seconds() >= state.next_monitor_time) {
            if (state.monitor(state.opaque) != 0) {
                state.result = DTU351_ERROR_RECONFIGURE;
                state.stopping = 1;
            }
            state.next_monitor_time = monotonic_seconds() +
                (double)state.monitor_interval_ms / 1000.0;
        }
        if (!state.stopping && state.completed == 0 &&
            monotonic_seconds() - state.start_time > 5.0) {
            state.result = LIBUSB_ERROR_TIMEOUT;
            state.stopping = 1;
        }
        if (debug_registers && !state.stopping &&
            monotonic_seconds() >= next_debug_time) {
            debug_capture_registers(device, debug_sample++);
            next_debug_time += 0.5;
        }
    }
    if (result == DTU351_OK || result == LIBUSB_SUCCESS)
        result = state.result;
    (void)dtu351_stop_capture(device);
    (void)libusb_set_interface_alt_setting(device->handle, device->interface_number, 0);

cleanup:
    for (unsigned index = 0; index < queue_depth; ++index) {
        if (state.slots[index].transfer != NULL)
            libusb_free_transfer(state.slots[index].transfer);
        free(state.slots[index].buffer);
    }
    free(state.slots);
    return result;
}

int dtu351_capture_run(dtu351_device *device, int high_definition,
                       uint16_t receive_mode, dtu351_transport transport,
                       unsigned queue_depth,
                       unsigned long transfer_limit,
                       dtu351_data_callback callback, void *opaque)
{
    return dtu351_capture_run_monitored(
        device, high_definition, receive_mode, transport, queue_depth,
        transfer_limit, callback, NULL, 0, opaque);
}
