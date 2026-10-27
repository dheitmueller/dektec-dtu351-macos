#include "dtu351/dtu351.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    assert(DTU351_VENDOR_ID == 0x1297u);
    assert(DTU351_PRODUCT_ID == 0x0333u);
    assert(DTU351_BULK_IN_ENDPOINT == 0x83u);
    assert(DTU351_TRANSFER_SIZE == 512u * 1024u);
    assert(strcmp(dtu351_error_name(DTU351_ERROR_NOT_FOUND), "DTU-351 not found") == 0);
    return 0;
}
