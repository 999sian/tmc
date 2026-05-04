#ifndef CONFIGS_RUNTIME_H
#define CONFIGS_RUNTIME_H

#include "global.h"

typedef struct {
    bool32 skipCutscenesWithSelect;
} RuntimeConfig;

/*
 * Build-time PC port settings.
 * Toggle these before building to change default runtime behavior.
 */

extern const RuntimeConfig gRuntimeConfig;

#endif // CONFIGS_RUNTIME_H
