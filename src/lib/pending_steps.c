/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license
 *information.
 *--------------------------------------------------------------------------------------------*/
#include <clog/clog.h>
#include <imprint/allocator.h>
#include <nimble-steps/pending_steps.h>

static void nbsPendingStepDebugOutput(const NbsPendingStep* self, int index, const char* name)
{
    // CLOG_INFO("%d: %08X %s (octet count:%zu)", index, self->idForDebug, name,
    // self->payloadLength);
}

void nbsPendingStepInit(NbsPendingStep* self, const uint8_t* payload, size_t payloadLength, StepId idForDebug,
                        struct ImprintAllocatorWithFree* allocatorWithFree)
{
    int code = nbsStepsVerifyStep(payload, payloadLength);
    if (code < 0) {
        CLOG_ERROR("nbsPendingStepInit: not a correctly serialized step. can not read")
        return;
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

void nbsPendingStepsInit(NbsPendingSteps* self, StepId lateJoinStepId, ImprintAllocatorWithFree* allocatorWithFree,
                         Clog log)
{
    self->log = log;
    self->debugCount = 0;
    self->readIndex = 0;
    self->writeIndex = 0;
    self->readId = lateJoinStepId;
    self->expectingWriteId = lateJoinStepId;
    self->receiveMask = NIMBLE_STEPS_PENDING_RECEIVE_MASK_ALL_RECEIVED; // If we don't mark
                                                                        // everything as received,
                                                                        // we will get resent old
                                                                        // steps
    self->allocatorWithFree = allocatorWithFree;
    tc_mem_clear_type_n(self->steps, NIMBLE_STEPS_PENDING_WINDOW_SIZE);
}

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
    if (tc_modulo((self->readId - 1), NIMBLE_STEPS_PENDING_WINDOW_SIZE) != (int) id) {
        return -2;
    }
    int lastReadIndex = tc_modulo((self->readIndex - 1), NIMBLE_STEPS_PENDING_WINDOW_SIZE);
    NbsPendingStep* item = &self->steps[lastReadIndex];
    nbsPendingStepDestroy(item);
    return 0;
}

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

    int code = nbsStepsVerifyStep(item->payload, item->payloadLength);
    if (code < 0) {
        CLOG_C_ERROR(&self->log, "nbsPendingStepsTryRead: not a correctly serialized step. can not read")
        return code;
    }

    return 1;
}

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
            return foundParticipantCount;
        }

        CLOG_C_VERBOSE(&self->log, "writing authoritative %08X of size:%zu", outId, length)
        int result = nbsStepsWrite(target, outId, data, length);
        if (result < 0) {
            return result;
        }
    }

    return 0;
}

uint64_t nbsPendingStepsReceiveMask(const NbsPendingSteps* self, StepId* headId)
{
    *headId = self->expectingWriteId;
    return self->receiveMask;
}

int nbsPendingStepsRanges(StepId headId, StepId tailId, uint64_t mask, NbsPendingRange* ranges, size_t maxRangeCount,
                          size_t stepCountMax)
{
    size_t index = 0;
    bool isInsideRange = false;
    int rangeIndex;
    size_t stepCountTotal = 0;
    for (int i = 63; i >= 0; --i) {
        int bit = (mask >> i) & 0x1;
        if (!bit && !isInsideRange) {
            if (i + tailId >= headId) {
                continue;
            }
            StepId id = headId - i;
            ranges[index].startId = id;
            ranges[index].count = 0;
            isInsideRange = true;
            rangeIndex = i;
        } else if (bit && isInsideRange) {
            size_t count = rangeIndex - i;
            ranges[index].count = count;
            index++;
            if (stepCountTotal + count >= stepCountMax - 1) {
                count = stepCountMax - stepCountTotal;
                ranges[index - 1].count = count;
                return index;
            }
            stepCountTotal += count;
            if (index == maxRangeCount) {
                return index;
            }
            rangeIndex = -1;
            isInsideRange = false;
        }
    }

    return index;
}

void nbsPendingStepsRangesDebugOutput(const NbsPendingRange* ranges, const char* debug, size_t maxCount, Clog log)
{
    CLOG_C_VERBOSE(&log, "--- ranges '%s' number of ranges:%zu", debug, maxCount);
    for (size_t i = 0; i < maxCount; ++i) {
        CLOG_C_VERBOSE(&log, "%zu: %08X count:%zu", i, ranges[i].startId, ranges[i].count);
    }
}

static const char* printBitPosition(size_t count)
{
    static char buf[(64 + 1 + 8 + 8) * 2 + 1];

    int strPos = 0;
    for (size_t i = 0; i < count; ++i) {
        if ((i % 8) == 0) {
            buf[strPos++] = '.';
        } else if ((i % 4) == 0) {
            buf[strPos++] = ' ';
        }
        buf[strPos++] = 48 + ((63 - i) / 10);
    }
    buf[strPos++] = '\n';
    for (size_t i = 0; i < count; ++i) {
        if ((i % 8) == 0) {
            buf[strPos++] = '.';
        } else if ((i % 4) == 0) {
            buf[strPos++] = ' ';
        }
        buf[strPos++] = 48 + ((63 - i) % 10);
    }
    buf[strPos] = 0;
    return buf;
}

static const char* printBits(uint64_t bits)
{
    static char buf[64 + 1 + 8 + 8];

    int strPos = 0;
    for (size_t i = 0; i < 64; ++i) {
        uint64_t test = 1ULL << (63 - i);
        if ((i % 8) == 0) {
            buf[strPos++] = '.';
        } else if ((i % 4) == 0) {
            buf[strPos++] = ' ';
        }
        buf[strPos++] = (bits & test) ? '1' : '0';
    }
    buf[strPos] = 0;
    return buf;
}

void nbsPendingStepsDebugReceiveMaskExt(StepId headStepId, uint64_t receiveMask, const char* debug, Clog log)
{
    CLOG_C_INFO(&log, "'%s' pending steps receiveMask head: %08X mask: \n%s\n%s", debug, headStepId,
                printBitPosition(64), printBits(receiveMask));
}

void nbsPendingStepsDebugReceiveMask(const NbsPendingSteps* self, const char* debug)
{
    nbsPendingStepsDebugReceiveMaskExt(self->expectingWriteId, self->receiveMask, debug, self->log);
}

static int stepIdToIndex(const NbsPendingSteps* self, StepId stepId)
{
    if (stepId < self->readId) {
        // CLOG_DEBUG("we have already dealt with this id %08X %08X", stepId,
        // self->tailId);
        return -2;
    }
    int delta = stepId - self->readId;
    if (delta >= NIMBLE_STEPS_PENDING_WINDOW_SIZE) {
        return -1;
    }
    return (self->readIndex + delta) % NIMBLE_STEPS_PENDING_WINDOW_SIZE;
}

void nbsPendingStepsDebugOutput(const NbsPendingSteps* self, const char* debug, int flags)
{
    // CLOG_INFO("pending steps '%s' count:%zu head: %08X mask: \n%s\n%s", debug,
    // self->debugCount, self->expectingWriteId, printBitPosition(64),
    // printBits(self->receiveMask) );
    for (size_t i = 0; i < NIMBLE_STEPS_PENDING_WINDOW_SIZE; ++i) {
        const char* prefix = "  ";
        const char* prefix2 = "  ";
        // const NbsPendingStep* entry = &self->steps[i];
        int forceThisLine = 0;
        if (i == self->writeIndex) {
            prefix = "H>";
            forceThisLine = 1;
        }
        if (i == self->readIndex) {
            prefix2 = "T>";
            forceThisLine = 1;
        }
    }
}

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
            CLOG_SOFT_ERROR("was already in use with different data %d", index);
            return -3;
        }
        CLOG_SOFT_ERROR("was already in use %d", index);
        return -2;
    } else {

        // CLOG_VERBOSE("setting %08X (index %d) for the first time", stepId,
        // index);
    }
    if (stepId >= self->expectingWriteId) {
        int advanceBits = (stepId - self->expectingWriteId) + 1;
        self->receiveMask <<= advanceBits;
        self->receiveMask |= 0x1;
        self->expectingWriteId = stepId + 1;
        self->writeIndex = index;
    } else {
        // It was a previous step
        size_t bitsFromHead = (self->expectingWriteId - stepId) - 1;
        if (bitsFromHead > 63) {
            CLOG_C_SOFT_ERROR(&self->log, "Illegal protocol bitsFromHead")
            return -44;
        }
        uint64_t maskForThisStep = 1ULL << bitsFromHead;
        self->receiveMask |= maskForThisStep;
    }
    if (existingStep->payload) {
        IMPRINT_FREE(self->allocatorWithFree, (void*) existingStep->payload);
    }
    nbsPendingStepInit(existingStep, payload, payloadLength, stepId, self->allocatorWithFree);
    self->debugCount++;
    return 1;
}
