#ifndef PORT_TYPES_H
#define PORT_TYPES_H
#include <stddef.h>
#include <stdint.h>

// Type definitions for GBA-style fixed-width types
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef volatile u8 vu8;
typedef volatile u16 vu16;
typedef volatile u32 vu32;
typedef volatile u64 vu64;

// Type assertions
_Static_assert(sizeof(u8) == 1, "u8 must be 1 byte");
_Static_assert(sizeof(u16) == 2, "u16 must be 2 bytes");
_Static_assert(sizeof(u32) == 4, "u32 must be 4 bytes");
_Static_assert(sizeof(u64) == 8, "u64 must be 8 bytes");
_Static_assert(sizeof(s8) == 1, "s8 must be 1 byte");
_Static_assert(sizeof(s16) == 2, "s16 must be 2 bytes");
_Static_assert(sizeof(s32) == 4, "s32 must be 4 bytes");
_Static_assert(sizeof(s64) == 8, "s64 must be 8 bytes");
// Pointer assertions
//_Static_assert(sizeof(void*) == 4, "Pointer size must be 4 bytes");

#endif // PORT_TYPES_H