#include <stdio.h>

#include "scroll.h"

static int sFailures;

#define CHECK_EQ(actual, expected, message)                                                                          \
    do {                                                                                                             \
        const int actualValue = (actual);                                                                            \
        const int expectedValue = (expected);                                                                        \
        if (actualValue != expectedValue) {                                                                          \
            fprintf(stderr, "FAIL: %s: got 0x%04x expected 0x%04x\n", message, actualValue & 0xffff,                \
                    expectedValue & 0xffff);                                                                         \
            sFailures++;                                                                                             \
        }                                                                                                            \
    } while (0)

int main(void) {
    CHECK_EQ(DecodePreservedTransitionAxis(0x8a1d, 0x0870), 0x01ad,
             "preserved Minish Woods Y becomes room-local");
    CHECK_EQ((u16)DecodePreservedTransitionAxis(0x0ba8, 0x0000), 0x0ba8, "fixed X is unchanged");
    CHECK_EQ((u16)DecodePreservedTransitionAxis(0x01f8, 0x0000), 0x01f8, "fixed transition endpoint is unchanged");
    CHECK_EQ((u16)DecodePreservedTransitionAxis(0x8008, 0x0010), 0xfff8, "preserved axis can move before origin");

    if (sFailures == 0) {
        puts("port_transition_axis_test: ALL PASS");
        return 0;
    }
    fprintf(stderr, "port_transition_axis_test: %d FAILED\n", sFailures);
    return 1;
}
