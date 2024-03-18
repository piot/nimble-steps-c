/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
#ifndef NIMBLE_SERVER_STEPS_H
#define NIMBLE_SERVER_STEPS_H

#include <clog/clog.h>
#include <discoid/circular_buffer.h>
#include <stdbool.h>
#include <nimble-steps/types.h>


#define NBS_WINDOW_SIZE (240)
#define NBS_RETREAT(index) tc_modulo((index - 1), NBS_WINDOW_SIZE)

typedef struct StepInfo {
    size_t positionInBuffer;
    size_t octetCount;
    StepId stepId;
    uint64_t optionalTime;
} StepInfo;

typedef struct NbsSteps {
    DiscoidBuffer stepsData;
    size_t stepsCount;
    size_t waitCounter;
    StepId expectedWriteId;
    StepId expectedReadId;
    StepInfo infos[NBS_WINDOW_SIZE];
    size_t infoHeadIndex;
    size_t infoTailIndex;
    bool isInitialized;
    Clog log;
} NbsSteps;

int nbsStepsVerifyStep(const uint8_t* payload, size_t octetCount);
size_t nbsStepsDropped(const NbsSteps* self, StepId firstReadStepId);
void nbsStepsInit(NbsSteps* self, struct ImprintAllocator* allocator, size_t maxTarget, Clog log);
void nbsStepsReInit(NbsSteps* self, StepId initialId);
void nbsStepsReset(NbsSteps* self);
bool nbsStepsLatestStepId(const NbsSteps* self, StepId* id);
size_t nbsStepsCount(const NbsSteps* self);
int nbsStepsRead(NbsSteps* self, StepId* stepId, uint8_t* data, size_t maxTarget);
int nbsStepsWrite(NbsSteps* self, StepId stepId, const uint8_t* data, size_t stepSize);
bool nbsStepsPeek(NbsSteps* self, StepId* stepId);
int nbsStepsDiscard(NbsSteps* self, StepId* stepId);
int nbsStepsDiscardUpTo(NbsSteps* self, StepId stepIdToDiscardTo);
int nbsStepsDiscardIncluding(NbsSteps* self, StepId stepIdToDiscardTo);
int nbsStepsDiscardCount(NbsSteps* self, size_t stepCountToDiscard);
bool nbsStepsAllowedToAdd(const NbsSteps* self);
int nbsStepsGetIndexForStep(const NbsSteps* self, StepId stepId);
int nbsStepsReadAtIndex(const NbsSteps* self, int infoIndex, uint8_t* data, size_t maxTarget);
void nbsStepsDebugOutput(const NbsSteps* self, const char* debug, int flags);

#endif
