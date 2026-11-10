/* Wide window side channel: HBlank DMA publishes recorded full edges. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "port_hdma.h"
uint8_t gIoMem[0x400];
static uint8_t table[2][0xa00];
static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
static uint16_t io16(int off) { return (uint16_t)(gIoMem[off] | (gIoMem[off + 1] << 8)); }
static void put(uint8_t* t, int line, int left, int right) {
    t[line * 2] = (uint8_t)(right > 0xef ? 0xf0 : right);
    t[line * 2 + 1] = (uint8_t)left;
}
int main(void) {
    /* Circle centred at x=300: lines 10..12 recorded, line 11 later rewritten
     * by another effect, line 13 drawn without a record. */
    port_ws_window_table_begin(table[0]);
    for (int line = 10; line <= 12; ++line) {
        put(table[0], line, 280, 320);
        port_ws_window_table_set(table[0], line, 280, 320);
    }
    put(table[0], 11, 0, 0x50);
    put(table[0], 13, 0, 0x50);
    port_hdma_register(0, table[0], &gIoMem[0x40], 0x0060, 1); /* 16-bit, dest reload */
    port_ws_window_frame_begin();
    for (int line = 0; line < 160; ++line) {
        port_hdma_step_line(line);
        /* Line N renders table entry N-1 (line 0 renders entry 0). */
        int entry = line == 0 ? 0 : line - 1;
        CHECK(io16(0x40) == (uint16_t)(table[0][entry * 2] | (table[0][entry * 2 + 1] << 8)));
        int valid = entry == 10 || entry == 12;
        CHECK(((io16(0x5e) & 1) != 0) == valid);
        if (valid) {
            CHECK(io16(0x56) == 280);
            CHECK(io16(0x58) == 320);
        }
    }
    /* The other double buffer keeps its own records; a fresh begin clears. */
    port_ws_window_table_begin(table[1]);
    put(table[1], 0, 100, 400);
    port_ws_window_table_set(table[1], 0, 100, 400);
    port_hdma_register(0, table[1], &gIoMem[0x42], 0x0060, 1);
    port_ws_window_frame_begin();
    CHECK(io16(0x5e) == 0);
    port_hdma_step_line(0);
    CHECK(io16(0x5e) == 2 && io16(0x5a) == 100 && io16(0x5c) == 400);
    port_ws_window_table_begin(table[0]);
    port_hdma_register(0, table[0], &gIoMem[0x40], 0x0060, 1);
    port_hdma_step_line(11);
    CHECK((io16(0x5e) & 1) == 0);
    /* Static window: the record survives while its packed value stays live
     * across vblanks, and is dropped once the game stores another value. */
    port_hdma_unregister(0);
    port_ws_window_static(0, 0x36f0, 310, 400);
    port_ws_window_latch(0x36f0, 0);
    port_ws_window_frame_begin();
    CHECK(io16(0x5e) == 1 && io16(0x56) == 310 && io16(0x58) == 400);
    port_ws_window_latch(0x36f0, 0);
    port_ws_window_frame_begin();
    CHECK(io16(0x5e) == 1);
    port_ws_window_latch(0x00f0, 0);
    port_ws_window_frame_begin();
    CHECK(io16(0x5e) == 0);
    port_ws_window_latch(0x36f0, 0);
    port_ws_window_frame_begin();
    CHECK(io16(0x5e) == 0);
    if (!failures) puts("widescreen HDMA window: PASS");
    return failures != 0;
}
