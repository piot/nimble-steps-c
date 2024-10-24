/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
#include <clog/clog.h>
#include <flood/in_stream.h>
#include <mash/murmur.h>
#include <nimble-steps/steps.h>
#include <stdbool.h>

/// Tries to do a sanity check of a payload to make sure it conforms to the format for a step
/// @param payload application specific payload
/// @param octetCount number of octets
/// @return negative on error
int nbsStepsVerifyStep(const uint8_t* payload, size_t octetCount)
{
    (void) payload;
    if (octetCount < NimbleStepMinimumSingleStepOctetCount) {
        CLOG_SOFT_ERROR("combined step is too small")
        return -1;
    }

    return 0;
}

/// Clears the buffer and sets a new starting TickId
/// @param self steps
/// @param initialId starting tickId for the buffer. The next write must be exactly for this TickId.
void nbsStepsReInit(NbsSteps* self, StepId initialId)
{
    self->stepsCount = 0;
    self->expectedWriteId = initialId;
    self->expectedReadId = initialId;
    self->infoHeadIndex = 0;
    self->infoTailIndex = 0;
    self->isInitialized = true;
    discoidBufferReset(&self->stepsData);
}

/// Puts the buffer in a state where it tries to free as much resources as possible
/// Think of it as just releasing the resources without specifying a new StepId
/// @param self steps
void nbsStepsReset(NbsSteps* self)
{
    nbsStepsReInit(self, NIMBLE_STEP_MAX);
    self->isInitialized = false;
}

/// Initializes the steps buffer and allocates all memory needed
/// All steps written to the buffer must be exactly in order without any gaps
/// @note you must call nbsStepsReInit directly after a call to this function
/// @param self steps
/// @param allocator allocator to use for step allocation
/// @param maxOctetSizeForCombinedStep maximum number of octets for each combined step
/// @param log the log to use
void nbsStepsInit(NbsSteps* self, struct ImprintAllocator* allocator, size_t maxOctetSizeForCombinedStep, Clog log)
{
    tc_mem_clear_type(self);
    self->log = log;
    if (maxOctetSizeForCombinedStep > NimbleStepMaxCombinedStepOctetCount) {
        CLOG_C_ERROR(&self->log,
                     "nbsStepsInit: only supports combined input sizes up to %zu octets, but encountered %zu",
                     NimbleStepMaxCombinedStepOctetCount, maxOctetSizeForCombinedStep)
    }

    size_t bufferOctetSize = maxOctetSizeForCombinedStep * (NBS_WINDOW_SIZE / 2);
    discoidBufferInit(&self->stepsData, allocator, bufferOctetSize);
}

/// Checks if it is possible to write to the buffer
/// @param self steps
/// @return true if possible, false otherwise
bool nbsStepsAllowedToAdd(const NbsSteps* self)
{
    return self->stepsCount < NBS_WINDOW_SIZE / 4;
}

#define NBS_ADVANCE(index) index = (index + 1) % NBS_WINDOW_SIZE
// #define NBS_RETREAT(index) index = tc_modulo((index - 1),  NBS_WINDOW_SIZE)

static int advanceInfoTail(NbsSteps* self, const StepInfo** outInfo)
{
    const StepInfo* info = &self->infos[self->infoTailIndex];
    NBS_ADVANCE(self->infoTailIndex);

    if (info->stepId != self->expectedReadId) {
        CLOG_C_ERROR(&self->log, "expected to read %u but encountered %u", self->expectedReadId, info->stepId)
        //*outInfo = 0;
        // return -3;
    }
    self->expectedReadId++;
    self->stepsCount--;

    *outInfo = info;
    return 0;
}

static int nbsStepsReadHelper(NbsSteps* self, const StepInfo* info, uint8_t* data, size_t maxTarget)
{
    if (info->octetCount > maxTarget) {
        CLOG_C_ERROR(&self->log, "wrong octet count in steps data")
        // return -3;
    }

    int errorCode = discoidBufferRead(&self->stepsData, data, info->octetCount);
    if (errorCode < 0) {
        return errorCode;
    }

    return (int) info->octetCount;
}

/// Reads the next step in the buffer, if any.
/// @param self steps
/// @param stepId to read
/// @param data the step payload will be copied to this
/// @param maxTarget maximum number of octets to copy to data
/// @return octet count for the step read, or negative value on error
int nbsStepsRead(NbsSteps* self, StepId* stepId, uint8_t* data, size_t maxTarget)
{
    if (self->stepsCount == 0) {
        return NimbleStepErrCollectionIsEmpty;
    }

    const StepInfo* info;

    int errorCode = advanceInfoTail(self, &info);
    if (errorCode < 0) {
        return errorCode;
    }

    *stepId = info->stepId;

    return nbsStepsReadHelper(self, info, data, maxTarget);
}

/// Reads the exact step Id. Discards old steps if any.
/// @param self steps
/// @param needStepId to exact stepId to read
/// @param data the step payload will be copied to this
/// @param maxTarget maximum number of octets to copy to data
/// @return octet count for the step read, or negative value on error
int nbsStepsReadExactStepId(NbsSteps* self, StepId needStepId, uint8_t* data, size_t maxTarget)
{
    StepId encounteredStepId;

    if (self->stepsCount == 0) {
        return NimbleStepErrCollectionIsEmpty;
    }

    int readStepOctetCount = nbsStepsRead(self, &encounteredStepId, data, maxTarget);

    // CLOG_VERBOSE("authenticate: party %d, found step:%08X, octet count: %d",participant->inParty->id,
    // encounteredStepId, stepOctetCount);

    if (readStepOctetCount < 0) {
        CLOG_C_SOFT_ERROR(&self->log, "couldn't find exact step %08X error:%d", needStepId, readStepOctetCount)
        return readStepOctetCount;
    }

    if (encounteredStepId != needStepId) {
        CLOG_C_VERBOSE(&self->log,
                       "buffer could not provide the ID the caller was looking for. needed %08X, but got %08X",
                       needStepId, encounteredStepId)
        int discardErr = nbsStepsDiscardUpTo(self, needStepId + 1);
        if (discardErr < 0) {
            CLOG_C_ERROR(&self->log, "could not discard including %d", discardErr)
        }
        return -1;
    }

    return readStepOctetCount;
}

/// Gets an index for a specific tickId (stepId)
/// @param self steps
/// @param stepId id to get the index fo
/// @return negative if stepId is not found
int nbsStepsGetIndexForStep(const NbsSteps* self, StepId stepId)
{
    if (self->stepsCount == 0) {
        CLOG_C_WARN(&self->log, "read at steps: no steps stored")
        return -2;
    }

    for (size_t i = 0U; i < self->stepsCount; ++i) {
        int infoIndex = tc_modulo((int) (self->infoTailIndex + i), NBS_WINDOW_SIZE);
        const StepInfo* info = &self->infos[infoIndex];
        if (info->stepId == stepId) {
            return infoIndex;
        }
    }
    return -1;
}

/// Reads a step at the specified index
/// @param self steps
/// @param infoIndex index of info
/// @param data target buffer
/// @param maxTarget maximum octet count of target buffer
/// @return negative on error
int nbsStepsReadAtIndex(const NbsSteps* self, int infoIndex, uint8_t* data, size_t maxTarget)
{
    if (infoIndex < 0) {
        return -2;
    }
    if (infoIndex >= NBS_WINDOW_SIZE) {
        return -3;
    }

    const StepInfo* info = &self->infos[infoIndex];
    if (info->octetCount > maxTarget) {
        CLOG_C_WARN(&self->log, "read at steps: target buffer is too small %zu %zu", info->octetCount, maxTarget)
        return -5;
    }

    discoidBufferPeek(&self->stepsData, info->positionInBuffer, data, info->octetCount);

    int verifyError = nbsStepsVerifyStep(data, info->octetCount);
    if (verifyError < 0) {
        CLOG_C_SOFT_ERROR(&self->log, "wrong step stored in discoid buffer")
        return verifyError;
    }

    return (int) info->octetCount;
}

/// Discard one step
/// @param self steps
/// @param stepId fills out the TickId for the step
/// @return negative on error
int nbsStepsDiscard(struct NbsSteps* self, StepId* stepId)
{
    const StepInfo* info;

    int errorCode = advanceInfoTail(self, &info);
    if (errorCode < 0) {
        CLOG_C_SOFT_ERROR(&self->log, "couldn't advance tail")
        return errorCode;
    }
    *stepId = info->stepId;

    return discoidBufferSkip(&self->stepsData, info->octetCount);
}

/// Discards up to, but not including the specified TickId.
/// @param self steps
/// @param stepIdToDiscardTo discard up to, but not including this StepId
/// @return number of steps actually discarded or negative on error
int nbsStepsDiscardUpTo(NbsSteps* self, StepId stepIdToDiscardTo)
{
    if (self->stepsCount == 0) {
        return 0;
    }

    if (stepIdToDiscardTo <= self->expectedReadId) {
        if (stepIdToDiscardTo < self->expectedReadId) {
            CLOG_C_WARN(&self->log, "nbsStepsDiscardUpTo: this happened a while back: %08X vs our start %08X",
                        stepIdToDiscardTo, self->expectedReadId)
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

    return (int) discardedCount;
}

/// Discards a number of steps from the buffer
/// @param self steps
/// @param stepCountToDiscard number of steps to discard
/// @return negative on error.
int nbsStepsDiscardCount(NbsSteps* self, size_t stepCountToDiscard)
{
    if (self->stepsCount < stepCountToDiscard) {
        CLOG_C_ERROR(&self->log, "too many to discard")
        // return -99;
    }

    for (size_t i = 0; i < stepCountToDiscard; ++i) {
        StepId discardedStepId;
        int errorCode = nbsStepsDiscard(self, &discardedStepId);
        if (errorCode < 0) {
            return errorCode;
        }
    }

    return 0;
}

/// Writes a step to the buffer
/// The stepId must be one more than the previous one inserted. The specified stepId is only used for debugging.
/// @param self steps
/// @param stepId only used for debugging, must be the expectedWriteId.
/// @param data application specific step payload
/// @param stepSize number of octets in data
/// @return negative on error
int nbsStepsWrite(NbsSteps* self, StepId stepId, const uint8_t* data, size_t stepSize)
{
    if (stepSize > 1024) {
        CLOG_C_ERROR(&self->log, "wrong stuff in steps data")
        // return -3;
    }

    if (self->stepsCount == NBS_WINDOW_SIZE / 2) {
        CLOG_C_ERROR(&self->log, "buffer is full. Do not know how to handle it. %zu out of %d", self->stepsCount,
                     NBS_WINDOW_SIZE)
        // return -6;
    }

    if (self->expectedWriteId != stepId) {
        CLOG_C_ERROR(&self->log, "expected write %08X but got %08X", self->expectedWriteId, stepId)
        // return -4;
    }

    int code = nbsStepsVerifyStep(data, stepSize);
    if (code < 0) {
        CLOG_C_ERROR(&self->log, "not a correctly serialized step. can not add")
        // return code;
    }

    self->expectedWriteId++;

    StepInfo* info = &self->infos[self->infoHeadIndex];
    info->stepId = stepId;
    info->octetCount = stepSize;
    info->positionInBuffer = self->stepsData.writeIndex;
    // CLOG_C_VERBOSE(&self->log,
    //              "nbsStepsWrite stepId: %08X infoHead: %zu pos: %zu "
    //            "octetCount: %zu stored steps: %zu",
    //          stepId, self->infoHeadIndex, info->positionInBuffer, info->octetCount, self->stepsCount + 1)
    NBS_ADVANCE(self->infoHeadIndex);

    int errorCode;

    errorCode = discoidBufferWrite(&self->stepsData, data, stepSize);
    if (errorCode < 0) {
        CLOG_C_SOFT_ERROR(&self->log, "couldn't write to buffer %d", errorCode)
        return errorCode;
    }

    self->stepsCount++;

    return (int) stepSize;
}

/// Checks the tickId of the next step available for reading from the buffer, but does not read it.
/// @param self steps
/// @param stepId id of step to look at
/// @return true if a step existed, false otherwise.
bool nbsStepsPeek(NbsSteps* self, StepId* stepId)
{
    if (self->stepsCount == 0) {
        *stepId = NIMBLE_STEP_MAX;
        return false;
    }

    *stepId = self->expectedReadId;

    return true;
}

/// Returns the latest tickId written to the buffer
/// @param self steps
/// @param id returned id if found
/// @return true if the buffer contains steps, false otherwise.
bool nbsStepsLatestStepId(const NbsSteps* self, StepId* id)
{
    if (self->stepsCount == 0) {
        *id = NIMBLE_STEP_MAX;
        return false;
    }

    *id = self->expectedWriteId - 1;

    return true;
}

/// The number of tickIds that are ahead of what is supposed to be written to the buffer
/// @param self steps
/// @param firstReadStepId the stepId to compare with
/// @return the number of steps ahead the specified firstReadStepId is or zero if not ahead
size_t nbsStepsDropped(const NbsSteps* self, StepId firstReadStepId)
{
    if (firstReadStepId > self->expectedWriteId) {
        return firstReadStepId - self->expectedWriteId;
    }

    return 0;
}

/// Debug logging
/// @param self steps
/// @param debug string description
/// @param flags modify the debug output
void nbsStepsDebugOutput(const NbsSteps* self, const char* debug, int flags)
{
    (void) flags;

#if defined CLOG_LOG_ENABLED
    size_t count = self->stepsCount;
    if (count == 0) {
        CLOG_C_VERBOSE(&self->log, "=== nimble steps '%s' empty", debug)
    } else {
        CLOG_C_VERBOSE(&self->log, "=== nimble steps '%s' from %08X to %08X (count:%zu)", debug, self->expectedReadId,
                       self->expectedWriteId - 1, count)
    }
#if defined STEPS_DEBUG_EXTRA_INFO
    uint8_t tempStepBuffer[1024];
    char extraInfo[1024];
    StepId stepIdToShow = self->expectedReadId;
    for (size_t i = 0; i < count; ++i) {
        CLOG_EXECUTE(int indexToShow = nbsStepsGetIndexForStep(self, stepIdToShow);)
        CLOG_EXECUTE(int readCount = nbsStepsReadAtIndex(self, indexToShow, tempStepBuffer, 1024);)
        extraInfo[0] = 0;
        CLOG_C_VERBOSE(&self->log, "  %zu: %08X (octet count:%d, hash:%08X)  %s", i, stepIdToShow, readCount,
                       mashMurmurHash3(tempStepBuffer, (size_t) readCount), extraInfo)
        stepIdToShow++;
    }
#endif

#else
    (void) self;
    (void) debug;
#endif
}
