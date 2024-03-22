/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
#include <clog/clog.h>
#include <imprint/allocator.h>
#include <mash/murmur.h>
#include <nimble-steps/pending_steps.h>

/// Initializes a pending step

/// @param self pending steps
/// @param payload payload
/// @param payloadLength length of payload
/// @param idForDebug the id associated with this step
/// @param allocatorWithFree allocator used to free the copy on nbsPendingStepDestroy.
void nbsPendingStepInit(NbsPendingStep* self, const uint8_t* payload, size_t payloadLength, StepId idForDebug,
                        struct ImprintAllocatorWithFree* allocatorWithFree)
{
    int code = nbsStepsVerifyStep(payload, payloadLength);
    if (code < 0) {
        CLOG_ERROR("nbsPendingStepInit: not a correctly serialized step. can not read")
        // return;
    }

    self->idForDebug = idForDebug;
    self->payloadLength = payloadLength;
    if (payloadLength > 0) {
        self->payload = IMPRINT_ALLOC((ImprintAllocator*) allocatorWithFree, payloadLength, "nbsPendingStepInit");
    } else {
        self->payload = 0;
    }
    self->isInUse = 1;
    self->allocatorWithFree = allocatorWithFree;
    tc_memcpy_octets((uint8_t*) self->payload, payload, payloadLength);
}

void nbsPendingStepDestroy(NbsPendingStep* self)
{
    IMPRINT_FREE(self->allocatorWithFree, self->payload);
    self->payloadLength = 0;
    self->payload = 0;
    self->idForDebug = NIMBLE_STEP_MAX;
}

/// Pending Steps are for steps that can be received out of sequence and in different ranges
/// using an unreliable datagram transport.
/// Typically used on the client to receive steps from the server and send a receive bitmask back
/// @param self pending steps
/// @param lateJoinStepId which tickId to start receiving from
/// @param allocatorWithFree allocator for each individual received step
/// @param log the log to be used
void nbsPendingStepsInit(NbsPendingSteps* self, StepId lateJoinStepId, ImprintAllocatorWithFree* allocatorWithFree,
                         Clog log)
{
    self->log = log;
    self->debugCount = 0;
    self->readIndex = 0;
    self->writeIndex = 0;
    self->readId = lateJoinStepId;
    self->allocatorWithFree = allocatorWithFree;
    nimbleStepsReceiveMaskInit(&self->receiveMask, lateJoinStepId);
    tc_mem_clear_type_n(self->steps, NIMBLE_STEPS_PENDING_WINDOW_SIZE);
}

/// Resets the pending steps. Usually used for when it is needed to skip ahead
/// @param self pending steps
/// @param lateJoinStepId tickId to start receiving from
void nbsPendingStepsReset(NbsPendingSteps* self, StepId lateJoinStepId)
{
    nbsPendingStepsInit(self, lateJoinStepId, self->allocatorWithFree, self->log);
}

bool nbsPendingStepsCanBeAdvanced(const NbsPendingSteps* self)
{
    return (self->steps[self->readIndex].payload != 0);
}

int nbsPendingStepsReadDestroy(NbsPendingSteps* self, StepId id)
{
    if (tc_modulo((int) (self->readId - 1), NIMBLE_STEPS_PENDING_WINDOW_SIZE) != (int) id) {
        return -2;
    }
    int lastReadIndex = tc_modulo((self->readIndex - 1), NIMBLE_STEPS_PENDING_WINDOW_SIZE);
    NbsPendingStep* item = &self->steps[lastReadIndex];
    nbsPendingStepDestroy(item);
    return 0;
}

/// Tries to read a single step from the pending steps
/// Always returns them in order from the last one (except when reset).
/// @note it doesn't copy the data, only sets the pointer to the existing data
/// @param self pending steps
/// @param outData the pointer to the static data
/// @param outLength the number of octets in outData
/// @param outId the TickId for the step
/// @return number of pending steps returned, or negative value on error
int nbsPendingStepsTryRead(NbsPendingSteps* self, const uint8_t** outData, size_t* outLength, StepId* outId)
{
    if (self->debugCount == 0) {
        CLOG_C_WARN(&self->log, "there are no pending steps in the buffer to read")
        *outLength = 0;
        *outId = 0;
        *outData = 0;
        return 0;
    }

    NbsPendingStep* item = &self->steps[self->readIndex];
    if (!item->isInUse) {
        *outLength = 0;
        *outId = 0;
        *outData = 0;
        return 0;
    }

    // nbsPendingStepDebugOutput(item, self->readIndex,  "read");
    self->readIndex = ++self->readIndex % NIMBLE_STEPS_PENDING_WINDOW_SIZE;
    self->debugCount--;
    *outData = item->payload;
    *outLength = item->payloadLength;
    *outId = self->readId++;
    item->isInUse = 0;

    // CLOG_C_INFO(&self->log, "read step %08X hash:%08X", *outId, mashMurmurHash3(item->payload, item->payloadLength))

    int code = nbsStepsVerifyStep(item->payload, item->payloadLength);
    if (code < 0) {
        CLOG_C_ERROR(&self->log, "nbsPendingStepsTryRead: not a correctly serialized step. can not read")
        // return code;
    }

    return 1;
}

/// Moves steps from pending steps to a target in order steps
/// @param target target buffer to copy into
/// @param self pending steps
/// @return zero on success or negative on error
int nbsPendingStepsCopy(NbsSteps* target, NbsPendingSteps* self)
{
    const uint8_t* data;
    size_t length;
    StepId outId;

    while (nbsStepsAllowedToAdd(target)) {
        if (self->debugCount == 0) {
            return 0;
        }

        int count = nbsPendingStepsTryRead(self, &data, &length, &outId);
        if (count == 0) {
            return 0;
        }

        int foundParticipantCount = nbsStepsVerifyStep(data, length);
        if (foundParticipantCount < 0) {
            CLOG_C_ERROR(&self->log, "nbsPendingStepsCopy: could not verify step of size:%zu", length)
            // return foundParticipantCount;
        }

        // CLOG_C_VERBOSE(&self->log, "writing authoritative %08X of size:%zu", outId, length)
        int result = nbsStepsWrite(target, outId, data, length);
        if (result < 0) {
            return result;
        }
    }

    return 0;
}

uint64_t nbsPendingStepsReceiveMask(const NbsPendingSteps* self, StepId* headId)
{
    *headId = self->receiveMask.expectingWriteId;
    return self->receiveMask.receiveMask;
}

/// Calculates the ranges to send to the remote given a range of TickIds and a receive mask.
/// @param maskStartsAtOneLessStepId from which step id to apply the mask backwards in time
/// @param maximumAvailablePlusOneStepId the number of steps available to send
/// @param mask the receive mask
/// @param ranges target ranges array
/// @param maxRangeCount maximum number of ranges in ranges array.
/// @param stepCountMax the maximum number of octets for each step
/// @return the number of ranges produced or negative on error
int nbsPendingStepsRanges(StepId maskStartsAtOneLessStepId, StepId maximumAvailablePlusOneStepId, uint64_t mask,
                          NbsPendingRange* ranges, size_t maxRangeCount, size_t stepCountMax)
{
    size_t index = 0;
    bool isInsideRange = false;
    int rangeIndex = 0;
    size_t stepCountTotal = 0;
    for (int i = 63; i >= 0; --i) {
        int bit = (mask >> i) & 0x1;
        if (!bit && !isInsideRange) {
            if ((size_t) i + maskStartsAtOneLessStepId >= maximumAvailablePlusOneStepId) {
                CLOG_DEBUG("found start but skipping since %d + %d > %d start", i, maximumAvailablePlusOneStepId,
                           maskStartsAtOneLessStepId)
                continue;
            }
            StepId id = maskStartsAtOneLessStepId - (StepId) i - 1;
            CLOG_DEBUG("found start %u", id)
            ranges[index].startId = id;
            ranges[index].count = 0;
            isInsideRange = true;
            rangeIndex = i;
        } else if (bit && isInsideRange) {
            size_t count = (size_t) (rangeIndex - i);
            CLOG_DEBUG("received a step and finishing the range with count %zu", count)
            ranges[index].count = count;
            index++;
            if (stepCountTotal + count >= stepCountMax - 1) {
                count = stepCountMax - stepCountTotal;
                ranges[index - 1].count = count;
                return (int) index;
            }
            stepCountTotal += count;
            if (index == maxRangeCount) {
                return (int) index;
            }
            rangeIndex = -1;
            isInsideRange = false;
        }
    }

    if (isInsideRange) {
        CLOG_DEBUG("add last range %d", rangeIndex)
        ranges[index - 1].count = (size_t) rangeIndex;
        index++;
    }

    return (int) index;
}

void nbsPendingStepsRangesDebugOutput(const NbsPendingRange* ranges, const char* debug, size_t maxCount, Clog log)
{
#if defined CLOG_LOG_ENABLED
    CLOG_C_VERBOSE(&log, "--- ranges '%s' number of ranges:%zu", debug, maxCount)
    for (size_t i = 0; i < maxCount; ++i) {
        CLOG_C_VERBOSE(&log, "%zu: %08X - %08X (count:%zu)", i, ranges[i].startId,
                       (StepId) (ranges[i].startId + ranges[i].count - 1), ranges[i].count)
    }
#else
    (void) ranges;
    (void) debug;
    (void) maxCount;
    (void) log;
#endif
}

static int stepIdToIndex(const NbsPendingSteps* self, StepId stepId)
{
    if (stepId < self->readId) {
        // CLOG_DEBUG("we have already dealt with this id %08X %08X", stepId,
        // self->tailId);
        return -2;
    }
    int delta = (int) (stepId - self->readId);
    if (delta >= NIMBLE_STEPS_PENDING_WINDOW_SIZE) {
        return -1;
    }
    return (self->readIndex + delta) % NIMBLE_STEPS_PENDING_WINDOW_SIZE;
}

void nbsPendingStepsDebugOutput(const NbsPendingSteps* self, const char* debug, int flags)
{
    (void) self;
    (void) debug;
    (void) flags;
}

/// Tries to set a pending step with the specified TickId.
/// @param self pending steps
/// @param stepId stepId to set
/// @param payload application specific step payload
/// @param payloadLength payload length
/// @return 1 on success, negative on error
int nbsPendingStepsTrySet(NbsPendingSteps* self, StepId stepId, const uint8_t* payload, size_t payloadLength)
{
    int index = stepIdToIndex(self, stepId);
    if (index < 0) {
        // CLOG_SOFT_ERROR("something is wrong with the step to index: %d", index)
        return 0;
    }

    // CLOG_DEBUG("found step %08X => index %d", stepId, index);

    NbsPendingStep* existingStep = &self->steps[index];
    if (existingStep->isInUse) {
        if (existingStep->idForDebug == stepId && existingStep->payloadLength == payloadLength) {
            if (tc_memcmp(existingStep->payload, payload, payloadLength) == 0) {
                return 0;
            }
            CLOG_ERROR("was already in use with different data %d", index)
            // return -3;
        }
        CLOG_SOFT_ERROR("was already in use %d", index)
        return -2;
    } else {

        // CLOG_VERBOSE("setting %08X (index %d) for the first time", stepId,
        // index);
    }
    if (stepId >= self->receiveMask.expectingWriteId) {
        self->writeIndex = index;
    }

    int maskError = nimbleStepsReceiveMaskReceivedStep(&self->receiveMask, stepId);
    if (maskError < 0) {
        CLOG_C_SOFT_ERROR(&self->log, "could not update receive mask %d", maskError)
        return -1;
    }

    if (existingStep->payload) {
        IMPRINT_FREE(self->allocatorWithFree, (void*) existingStep->payload);
    }

    // CLOG_C_VERBOSE(&self->log, "set pending step %08X hash:%08X", stepId, mashMurmurHash3(payload, payloadLength))

    nbsPendingStepInit(existingStep, payload, payloadLength, stepId, self->allocatorWithFree);
    self->debugCount++;
    return 1;
}
