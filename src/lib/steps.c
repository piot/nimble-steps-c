/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
#include <clog/clog.h>
#include <flood/in_stream.h>
#include <nimble-steps/steps.h>
#include <stdbool.h>
#include <tiny-libc/tiny_libc.h>

int nbsStepsVerifyStep(const uint8_t* payload, size_t octetCount)
{
    if (octetCount < 4) {
        CLOG_SOFT_ERROR("combined step is too small");
        return -1;
    }

    FldInStream stepInStream;
    fldInStreamInit(&stepInStream, payload, octetCount);

    uint8_t participantCountInStep;
    fldInStreamReadUInt8(&stepInStream, &participantCountInStep);

    if (participantCountInStep > 64) {
        CLOG_SOFT_ERROR("combined step: participant count is too high");
        return -4;
    }

    for (size_t i = 0; i < participantCountInStep; ++i) {
        uint8_t participantId;
        fldInStreamReadUInt8(&stepInStream, &participantId);
        if (participantId > 4) {
            CLOG_SOFT_ERROR("combined step: participantId is too high");
            return -3;
        }

        uint8_t octetCountForStep;
        fldInStreamReadUInt8(&stepInStream, &octetCountForStep);
        if (octetCountForStep < 1) {
            CLOG_SOFT_ERROR("combined step: an individual step must be at least one octet");
            return -6;
        }

        stepInStream.p += octetCountForStep;
        stepInStream.pos += octetCountForStep;
        if (stepInStream.pos > stepInStream.size) {
            return -2;
        }
    }

    return participantCountInStep;
}

void nbsStepsReInit(NbsSteps* self, StepId initialId)
{
    self->stepsCount = 0;
    self->expectedWriteId = initialId;
    self->expectedReadId = initialId;
    self->infoHeadIndex = 0;
    self->infoTailIndex = 0;
    discoidBufferReset(&self->stepsData);
}


void nbsStepsReset(NbsSteps* self)
{
    nbsStepsReInit(self, NIMBLE_STEP_MAX);
}

void nbsStepsInit(NbsSteps* self, struct ImprintAllocator* allocator, size_t maxOctets, StepId initialId)
{
    tc_mem_clear_type(self);
    discoidBufferInit(&self->stepsData, allocator, maxOctets);
    nbsStepsReInit(self, initialId);
}

void nbsStepsDestroy(NbsSteps* self)
{
    discoidBufferDestroy(&self->stepsData);
}

int nbsStepsAllowedToAdd(const NbsSteps* self)
{
    return self->stepsCount < 24;
}


#define NBS_ADVANCE(index) index = (index + 1) % NBS_WINDOW_SIZE
//#define NBS_RETREAT(index) index = tc_modulo((index - 1),  NBS_WINDOW_SIZE)

static int advanceInfoTail(NbsSteps* self, const StepInfo** outInfo)
{
    const StepInfo* info = &self->infos[self->infoTailIndex];
    NBS_ADVANCE(self->infoTailIndex);

    if (info->stepId != self->expectedReadId) {
        CLOG_ERROR("expected to read %d but encountered %d", self->expectedReadId, info->stepId);
        *outInfo = 0;
        return -3;
    }
    self->expectedReadId++;
    self->stepsCount--;

    *outInfo = info;
    return 0;
}

static int nbsStepsReadHelper(NbsSteps* self, const StepInfo* info, uint8_t* data, size_t maxTarget)
{
    if (info->octetCount > maxTarget) {
        CLOG_ERROR("wrong octet count in steps data");
        return -3;
    }

    int errorCode = discoidBufferRead(&self->stepsData, data, info->octetCount);
    if (errorCode < 0) {
        return errorCode;
    }

    return info->octetCount;
}

int nbsStepsRead(NbsSteps* self, StepId* stepId, uint8_t* data, size_t maxTarget)
{
    if (self->stepsCount == 0) {
        return -2;
    }

    const StepInfo* info;

    int errorCode = advanceInfoTail(self, &info);
    if (errorCode < 0) {
        return errorCode;
    }

    *stepId = info->stepId;

    return nbsStepsReadHelper(self, info, data, maxTarget);
}

int nbsStepsReadAtStep(const NbsSteps* self, StepId stepId, uint8_t* data, size_t maxTarget)
{
    if (self->stepsCount == 0) {
        CLOG_WARN("read at steps: no steps stored");
        return -2;
    }

    int infoIndexBackward = self->expectedWriteId - stepId;
    if (infoIndexBackward < 0) {
        CLOG_WARN("read at steps: this is a stepId in the future %08X %08X", self->expectedWriteId, stepId);
        return -3;
    }

    if ((size_t) infoIndexBackward > self->stepsCount) {
        CLOG_WARN("read at steps: you want to read steps (for serialization) that happened too long ago. "
                  "ExpectedWrite: %08X first:%08X firstA:%08X count:%zu,  stepID: %08X index:%d",
                  self->expectedWriteId, (StepId)(self->expectedWriteId - self->stepsCount), self->expectedReadId,
                  self->stepsCount, stepId, infoIndexBackward);
        return -4;
    }

    int infoIndex = tc_modulo((self->infoHeadIndex - infoIndexBackward), NBS_WINDOW_SIZE);

    const StepInfo* info = &self->infos[infoIndex];
    // CLOG_DEBUG("readAtStep: fetching info at index %d. Found step %08X octet count %d", infoIndex, info->stepId,
    // info->octetCount);

    if (info->octetCount > maxTarget) {
        CLOG_WARN("read at steps: target buffer is too small %zu %zu", info->octetCount, maxTarget);
        return -5;
    }

    discoidBufferPeek(&self->stepsData, info->positionInBuffer, data, info->octetCount);

    int verifyError = nbsStepsVerifyStep(data, info->octetCount);
    if (verifyError < 0) {
        CLOG_SOFT_ERROR("wrong step stored in discoid buffer");
        return verifyError;
    }

    return info->octetCount;
}

int nbsStepsDiscard(struct NbsSteps* self, StepId* stepId)
{
    const StepInfo* info;

    int errorCode = advanceInfoTail(self, &info);
    if (errorCode < 0) {
        CLOG_SOFT_ERROR("couldn't advance tail")
        return errorCode;
    }

    return discoidBufferSkip(&self->stepsData, info->octetCount);
}

int nbsStepsDiscardUpTo(NbsSteps* self, StepId stepIdToDiscardTo)
{
    if (self->stepsCount == 0) {
        return 0;
    }

    if (stepIdToDiscardTo <= self->expectedReadId) {
        if (stepIdToDiscardTo < self->expectedReadId) {
            CLOG_WARN("this happened a while back: %08X vs our start %08X", stepIdToDiscardTo, self->expectedReadId);
        }
        return 0;
    }

    size_t discardedCount = 0;
    while (1) {
        StepId discardedStepId;
        if (self->expectedReadId == stepIdToDiscardTo) {
            break;
        }
        if (self->stepsCount == 0) {
            break;
        }
        int errorCode = nbsStepsDiscard(self, &discardedStepId);
        if (errorCode < 0) {
            return errorCode;
        }
        discardedCount++;
    }

    return discardedCount;
}

int nbsStepsWrite(NbsSteps* self, StepId stepId, const uint8_t* data, size_t stepSize)
{
    if (stepSize > 1024) {
        CLOG_ERROR("wrong stuff in steps data")
        return -3;
    }

    if (self->stepsCount == NBS_WINDOW_SIZE) {
        CLOG_ERROR("buffer is full. Do not know how to handle it. %zu out of %d", self->stepsCount, NBS_WINDOW_SIZE)
        return -6;
    }

    if (self->expectedWriteId != stepId) {
        CLOG_SOFT_ERROR("expected write %d but got %d", self->expectedWriteId, stepId)
        return -4;
    }

    int code = nbsStepsVerifyStep(data, stepSize);
    if (code < 0) {
        CLOG_ERROR("not a correctly serialized step. can not add")
        return code;
    }

    self->expectedWriteId++;

    StepInfo* info = &self->infos[self->infoHeadIndex];
    info->stepId = stepId;
    info->octetCount = stepSize;
    info->positionInBuffer = self->stepsData.writeIndex;
    CLOG_INFO("nbsStepsWrite stepId: %08X infoHead: %zu pos: %zu octetCount: %zu stored steps: %zu", stepId, self->infoHeadIndex, info->positionInBuffer,
     info->octetCount, self->stepsCount+1)
    NBS_ADVANCE(self->infoHeadIndex);

    int errorCode;

    errorCode = discoidBufferWrite(&self->stepsData, data, stepSize);
    if (errorCode < 0) {
        CLOG_SOFT_ERROR("couldn't write to buffer %d", errorCode)
        return errorCode;
    }

    self->stepsCount++;

    return stepSize;
}

int nbsStepsPeek(NbsSteps* self, StepId* stepId)
{
    *stepId = self->expectedReadId;

    return 0;
}

bool nbsStepsLatestStepId(const NbsSteps* self, StepId* id)
{
    if (self->stepsCount == 0) {
        *id = NIMBLE_STEP_MAX;
        return false;
    }

    *id = self->expectedWriteId - 1;

    return true;
}

void nbsStepsDebugOutput(const NbsSteps* self, const char* debug, int flags)
{

    uint8_t tempStepBuffer[1024];
    size_t count = self->stepsCount;
    if (count == 0) {
        CLOG_INFO("=== nimble steps '%s' empty", debug)
    } else {
        CLOG_INFO("=== nimble steps '%s' from %u to %u (count:%zu)", debug, self->expectedReadId,
                  self->expectedWriteId - 1, count)
    }
    char extraInfo[1024];
    StepId stepIdToShow = self->expectedReadId;
    for (size_t i = 0; i < count; ++i) {
        int readCount = nbsStepsReadAtStep(self, stepIdToShow, tempStepBuffer, 1024);
            extraInfo[0] = 0;
        CLOG_INFO("  %zu: %08X (octet count: %d)  %s", i, stepIdToShow, readCount, extraInfo)
        stepIdToShow++;
    }
}
