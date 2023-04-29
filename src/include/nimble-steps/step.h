/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
#ifndef NIMBLE_SERVER_STEP_H
#define NIMBLE_SERVER_STEP_H

#include <stdint.h>
#include <stdlib.h>

typedef struct NimbleStep {
    const uint8_t* payload;
    size_t octetCount;
} NimbleStep;

#endif
