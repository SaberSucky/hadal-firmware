/* Host driver for applications/system/battery_history/battery_stats.c.
 *
 * stdin: one record a line, "<timestamp> <charge> <flags>", oldest first
 * argv: estimate                       prints "<seconds left> <percent per day x10>" or "none"
 *       columns <now> <span> <width>   prints the columns: charge (or -), "c" when charging
 *       duration <seconds>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "battery_stats.h"

int main(int argc, char** argv) {
    if(argc == 3 && strcmp(argv[1], "duration") == 0) {
        char out[32];
        battery_stats_format_duration((uint32_t)strtoul(argv[2], NULL, 10), out, sizeof(out));
        printf("%s\n", out);
        return 0;
    }
    static BatteryLogRecord records[8192];
    size_t count = 0;
    unsigned long t;
    unsigned charge, flags;
    while(count < 8192 && scanf("%lu %u %u", &t, &charge, &flags) == 3) {
        records[count++] = (BatteryLogRecord){.timestamp = (uint32_t)t, .charge = (uint8_t)charge, .flags = (uint8_t)flags};
    }
    if(argc == 2 && strcmp(argv[1], "estimate") == 0) {
        BatteryEstimate estimate;
        if(battery_stats_estimate(records, count, &estimate)) {
            printf("%lu %lu\n", (unsigned long)estimate.seconds_left, (unsigned long)estimate.percent_per_day_x10);
        } else {
            printf("none\n");
        }
    } else if(argc == 5 && strcmp(argv[1], "columns") == 0) {
        const size_t width = (size_t)atoi(argv[4]);
        int8_t* columns = malloc(width);
        bool* charging = malloc(width * sizeof(bool));
        battery_stats_columns(
            records, count, (uint32_t)strtoul(argv[2], NULL, 10), (uint32_t)strtoul(argv[3], NULL, 10),
            columns, charging, width);
        for(size_t x = 0; x < width; x++) {
            if(columns[x] == BATTERY_STATS_NO_DATA) {
                printf("-%s\n", charging[x] ? "c" : "");
            } else {
                printf("%d%s\n", columns[x], charging[x] ? "c" : "");
            }
        }
        free(columns);
        free(charging);
    } else {
        return 2;
    }
    return 0;
}
