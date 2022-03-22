/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
#ifndef NIMBLE_SERVER_STEPS_H
#define NIMBLE_SERVER_STEPS_H

#include <discoid/circular_buffer.h>

typedef uint32_t StepId;

static const StepId NIMBLE_STEP_MAX = 0xffffffff;

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
} NbsSteps;

int nbsStepsVerifyStep(const uint8_t* payload, size_t octetCount);
size_t nbsStepsDropped(const NbsSteps* self, StepId firstReadStepId);
void nbsStepsInit(NbsSteps* self, size_t maxTarget, StepId startId);
void nbsStepsDestroy(NbsSteps* self);
void nbsStepsReInit(NbsSteps* self, StepId initialId);
void nbsStepsReset(NbsSteps* self);
size_t nbsStepsCount(const NbsSteps* self);
int nbsStepsRead(NbsSteps* self, StepId* stepId, uint8_t* data, size_t maxTarget);
int nbsStepsWrite(NbsSteps* self, StepId stepId, const uint8_t* data, size_t stepSize);
int nbsStepsPeek(NbsSteps* self, StepId* stepId);
int nbsStepsDiscard(NbsSteps* self, StepId* stepId);
int nbsStepsDiscardUpTo(NbsSteps* self, StepId stepIdToDiscardTo);
int nbsStepsReadAtStep(const NbsSteps* self, StepId stepId, uint8_t* data, size_t maxTarget);
int nbsStepsDiscardIncluding(NbsSteps* self, StepId stepIdToDiscardTo);
int nbsStepsAllowedToAdd(const NbsSteps* self);
void nbsStepsDebugOutput(const NbsSteps* self, const char* debug, int flags);

#endif // NIMBLE_SERVER_STEPS_H
