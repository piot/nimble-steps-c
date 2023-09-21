/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
#ifndef NIMBLE_STEPS_TYPES_H
#define NIMBLE_STEPS_TYPES_H

#include <stdint.h>

typedef uint32_t StepId;

static const StepId NIMBLE_STEP_MAX = 0xffffffff;

static const size_t NimbleStepMaxSingleStepOctetCount = 64;
static const size_t NimbleStepMaxParticipantCount = 16;
static const size_t NimbleStepMaxCombinedStepOctetCount = NimbleStepMaxSingleStepOctetCount *
                                                          NimbleStepMaxParticipantCount;

#endif
