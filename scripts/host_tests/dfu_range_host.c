/* Host driver for lib/update_util/dfu_file.c: dfu_range_host <file.dfu>
 * prints "<n targets> <crc ok> <start hex> <end hex>" as the updater would see the image. */
#include <stdio.h>

#include <update_util/dfu_file.h>

extern const char* host_storage_root;

static void progress(const uint8_t value, void* context) {
    (void)value;
    (void)context;
}

int main(int argc, char** argv) {
    if(argc != 2) return 2;
    host_storage_root = "";
    File* file = storage_file_alloc(NULL);
    if(!storage_file_open(file, argv[1], FSAM_READ, FSOM_OPEN_EXISTING)) return 3;

    const DfuValidationParams params = {.device = 0xFFFF, .product = 0xDF11, .vendor = 0x0483};
    const bool crc_ok = dfu_file_validate_crc(file, progress, NULL);
    const uint8_t targets = dfu_file_validate_headers(file, &params);
    uint32_t start = 0, end = 0;
    const bool range = targets && dfu_file_get_address_range(file, targets, &start, &end);
    printf("%u %d %d %08X %08X\n", targets, crc_ok, range, start, end);
    storage_file_free(file);
    return 0;
}
