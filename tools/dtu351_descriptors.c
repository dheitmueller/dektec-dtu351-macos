#include <libusb.h>
#include <stdio.h>

int main(void)
{
    libusb_context *context = NULL;
    libusb_device **list = NULL;
    ssize_t count;
    if (libusb_init(&context) != 0)
        return 1;
    count = libusb_get_device_list(context, &list);
    for (ssize_t n = 0; n < count; ++n) {
        struct libusb_device_descriptor d;
        struct libusb_config_descriptor *c = NULL;
        if (libusb_get_device_descriptor(list[n], &d) != 0 ||
            d.idVendor != 0x1297 || d.idProduct != 0x0333)
            continue;
        printf("device %04x:%04x speed=%d configurations=%u\n",
               d.idVendor, d.idProduct, libusb_get_device_speed(list[n]),
               d.bNumConfigurations);
        if (libusb_get_active_config_descriptor(list[n], &c) != 0)
            continue;
        printf("configuration=%u interfaces=%u attributes=0x%02x power=%umA\n",
               c->bConfigurationValue, c->bNumInterfaces, c->bmAttributes,
               (unsigned)c->MaxPower * 8u);
        for (uint8_t i = 0; i < c->bNumInterfaces; ++i) {
            for (int a = 0; a < c->interface[i].num_altsetting; ++a) {
                const struct libusb_interface_descriptor *alt = &c->interface[i].altsetting[a];
                printf(" interface=%u alt=%u endpoints=%u class=0x%02x\n",
                       alt->bInterfaceNumber, alt->bAlternateSetting,
                       alt->bNumEndpoints, alt->bInterfaceClass);
                for (uint8_t e = 0; e < alt->bNumEndpoints; ++e) {
                    const struct libusb_endpoint_descriptor *ep = &alt->endpoint[e];
                    struct libusb_ss_endpoint_companion_descriptor *ss = NULL;
                    printf("  endpoint=0x%02x attributes=0x%02x max_packet=%u interval=%u\n",
                           ep->bEndpointAddress, ep->bmAttributes, ep->wMaxPacketSize,
                           ep->bInterval);
                    if (libusb_get_ss_endpoint_companion_descriptor(NULL, ep, &ss) == 0) {
                        printf("   ss max_burst=%u attributes=0x%02x bytes_per_interval=%u\n",
                               ss->bMaxBurst, ss->bmAttributes, ss->wBytesPerInterval);
                        libusb_free_ss_endpoint_companion_descriptor(ss);
                    }
                }
            }
        }
        libusb_free_config_descriptor(c);
    }
    libusb_free_device_list(list, 1);
    libusb_exit(context);
    return 0;
}
