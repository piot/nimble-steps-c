/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
#ifndef NIMBLE_STEPS_EXAMPLE_PENDING_STEPS_H
#define NIMBLE_STEPS_EXAMPLE_PENDING_STEPS_H

#define NIMBLE_STEPS_PENDING_WINDOW_SIZE (64)
#include <nimble-steps/steps.h>

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

struct FldInStream;

typedef struct NbsPendingStep {
    const uint8_t* payload;
    size_t payloadLength;
    StepId idForDebug;
    int isInUse;
} NbsPendingStep;

typedef struct NbsPendingRange {
    StepId startId;
    size_t count;
} NbsPendingRange;

#define NIMBLE_STEPS_PENDING_RECEIVE_MASK_ALL_RECEIVED (UINT64_MAX)

void nbsPendingStepInit(NbsPendingStep* self, const uint8_t* payload, size_t payloadLength, StepId idForDebug);
void nbsPendingStepDestroy(NbsPendingStep* self);


typedef struct NbsPendingSteps {
    NbsPendingStep steps[NIMBLE_STEPS_PENDING_WINDOW_SIZE];
    int writeIndex;
    int readIndex;
    size_t debugCount;
    StepId expectingWriteId;
    StepId readId;
    uint64_t receiveMask;
} NbsPendingSteps;

int nbsPendingStepsRanges(StepId headId, StepId tailId, uint64_t mask, NbsPendingRange* ranges, size_t maxCount,
                          size_t stepMaxCount);
void nbsPendingStepsRangesDebugOutput(const NbsPendingRange* ranges, const char* debug, size_t maxCount);

void nbsPendingStepsInit(NbsPendingSteps* self, StepId lateJoinStepId);
void nbsPendingStepsReset(NbsPendingSteps* self, StepId lateJoinStepId);
void nbsPendingStepsSerializeIn(NbsPendingSteps* self, struct FldInStream* stream);
int nbsPendingStepsCanBeAdvanced(const NbsPendingSteps* self);
int nbsPendingStepsTryRead(NbsPendingSteps* self, const uint8_t** outData, size_t* outLength, StepId* outId);
int nbsPendingStepsReadDestroy(NbsPendingSteps* self, StepId id);
int nbsPendingStepsHasStep(const NbsPendingSteps* self, StepId stepId);
uint64_t nbsPendingStepsReceiveMask(const NbsPendingSteps* self, StepId* headId);
int nbsPendingStepsTrySet(NbsPendingSteps* self, StepId stepId, const uint8_t* payload, size_t payloadLength);
void nbsPendingStepsDebugOutput(const NbsPendingSteps* self, const char* debug, int flags);
void nbsPendingStepsDebugReceiveMask(const NbsPendingSteps* self, const char* debug);
void nbsPendingStepsDebugReceiveMaskExt(StepId headStepId, uint64_t receiveMask, const char* debug);
bool nbsPendingStepsLatestStepId(const NbsPendingSteps* self, StepId* id);

#endif // NIMBLE_STEPS_EXAMPLE_PENDING_STEPS_H
