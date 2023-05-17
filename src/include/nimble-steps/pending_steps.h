/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
#ifndef NIMBLE_STEPS_PENDING_STEPS_H
#define NIMBLE_STEPS_PENDING_STEPS_H

#define NIMBLE_STEPS_PENDING_WINDOW_SIZE (64)
#include <nimble-steps/receive_mask.h>
#include <nimble-steps/steps.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

struct FldInStream;
struct ImprintAllocator;
struct ImprintAllocatorWithFree;

typedef struct NbsPendingStep {
    const uint8_t* payload;
    size_t payloadLength;
    StepId idForDebug;
    int isInUse;
    struct ImprintAllocatorWithFree* allocatorWithFree;
} NbsPendingStep;

typedef struct NbsPendingRange {
    StepId startId;
    size_t count;
} NbsPendingRange;

void nbsPendingStepInit(NbsPendingStep* self, const uint8_t* payload, size_t payloadLength, StepId idForDebug,
                        struct ImprintAllocatorWithFree* allocator);
void nbsPendingStepDestroy(NbsPendingStep* self);

/// Pending Steps are for steps that can be received out of sequence and in different ranges
/// using an unreliable datagram transport.
/// Typically used on the client to receive steps from the server.
typedef struct NbsPendingSteps {
    NbsPendingStep steps[NIMBLE_STEPS_PENDING_WINDOW_SIZE];
    int writeIndex;
    int readIndex;
    size_t debugCount;
    StepId readId;
    struct ImprintAllocatorWithFree* allocatorWithFree;
    NimbleStepsReceiveMask receiveMask;
    Clog log;
} NbsPendingSteps;

int nbsPendingStepsRanges(StepId maskStartsAtStepId, StepId maximumAvailableStepId, uint64_t mask,
                          NbsPendingRange* ranges, size_t maxCount, size_t stepMaxCount);
void nbsPendingStepsRangesDebugOutput(const NbsPendingRange* ranges, const char* debug, size_t maxCount, Clog log);

void nbsPendingStepsInit(NbsPendingSteps* self, StepId lateJoinStepId,
                         struct ImprintAllocatorWithFree* allocatorWithFree, Clog log);
int nbsPendingStepsCopy(NbsSteps* target, NbsPendingSteps* self);
void nbsPendingStepsReset(NbsPendingSteps* self, StepId lateJoinStepId);
void nbsPendingStepsSerializeIn(NbsPendingSteps* self, struct FldInStream* stream);
bool nbsPendingStepsCanBeAdvanced(const NbsPendingSteps* self);
int nbsPendingStepsTryRead(NbsPendingSteps* self, const uint8_t** outData, size_t* outLength, StepId* outId);
int nbsPendingStepsReadDestroy(NbsPendingSteps* self, StepId id);
bool nbsPendingStepsHasStep(const NbsPendingSteps* self, StepId stepId);
uint64_t nbsPendingStepsReceiveMask(const NbsPendingSteps* self, StepId* headId);
int nbsPendingStepsTrySet(NbsPendingSteps* self, StepId stepId, const uint8_t* payload, size_t payloadLength);
void nbsPendingStepsDebugOutput(const NbsPendingSteps* self, const char* debug, int flags);
void nbsPendingStepsDebugReceiveMask(const NbsPendingSteps* self, const char* debug);
void nbsPendingStepsDebugReceiveMaskExt(StepId headStepId, uint64_t receiveMask, const char* debug, Clog log);

#endif
