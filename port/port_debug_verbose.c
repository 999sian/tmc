#include "port_debug_verbose.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool Port_DebugVerbose = false;

void Port_DebugVerbose_Init(void) {
    const char* env = getenv("TMC_VERBOSE");
    if (env && *env && strcmp(env, "0") != 0) {
        Port_DebugVerbose = true;
        fprintf(stderr, "[verbose] TMC_VERBOSE=%s — per-frame stderr logs enabled\n", env);
    }
}
