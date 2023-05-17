/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
#ifndef NIMBLE_STEPS_RECEIVE_MASK_H
#define NIMBLE_STEPS_RECEIVE_MASK_H

#include <nimble-steps/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <clog/clog.h>

typedef uint64_t NimbleStepsReceiveMaskBits;

typedef struct NimbleStepsReceiveMask {
    StepId expectingWriteId;
    NimbleStepsReceiveMaskBits receiveMask;
} NimbleStepsReceiveMask;

static const NimbleStepsReceiveMaskBits NimbleStepsReceiveMaskAllReceived = UINT64_MAX;

void nimbleStepsReceiveMaskInit(NimbleStepsReceiveMask* self, StepId startId);
int nimbleStepsReceiveMaskReceivedStep(NimbleStepsReceiveMask* self, StepId startId);
void nimbleStepsReceiveMaskDebugMask(const NimbleStepsReceiveMask* self, const char* debug, Clog log);

#endif
