/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
#include <clog/clog.h>
#include <nimble-steps/receive_mask.h>
#include <stddef.h>

static const char* printBitPosition(size_t count)
{
    static char buf[(64 + 1 + 8 + 8) * 2 + 1];

    int strPos = 0;
    for (size_t i = 0; i < count; ++i) {
        if ((i % 8) == 0) {
            buf[strPos++] = ' ';
        } else if ((i % 4) == 0) {
            buf[strPos++] = '.';
        }
        buf[strPos++] = 48 + ((63 - i) / 10);
    }
    buf[strPos++] = '\n';
    for (size_t i = 0; i < count; ++i) {
        if ((i % 8) == 0) {
            buf[strPos++] = ' ';
        } else if ((i % 4) == 0) {
            buf[strPos++] = '.';
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
            buf[strPos++] = ' ';
        } else if ((i % 4) == 0) {
            buf[strPos++] = '.';
        }
        buf[strPos++] = (bits & test) ? '1' : '0';
    }
    buf[strPos] = 0;
    return buf;
}

static void nimbleStepsReceiveMaskDebugMaskExt(StepId headStepId, uint64_t receiveMask, const char* debug, Clog log)
{
    CLOG_C_INFO(&log, "'%s' pending steps receiveMask head: %08X mask: \n%s\n%s", debug, headStepId,
                printBitPosition(64), printBits(receiveMask));
}

void nimbleStepsReceiveMaskDebugMask(const NimbleStepsReceiveMask* self, const char* debug, Clog log)
{
    nimbleStepsReceiveMaskDebugMaskExt(self->expectingWriteId, self->receiveMask, debug, log);
}

/// Initializes the receive mask logic
/// It keeps a mask of all steps that has been received before the `expectingWriteId`.
/// the steps are in order from lowest bit to highest bit. The lowest bit is always set.
/// `1001` and expectingWriteId = 30 means that packet 29 and 26 was received and 27 and 28 has not been received.
/// @param self
/// @param startId
void nimbleStepsReceiveMaskInit(NimbleStepsReceiveMask* self, StepId startId)
{
    self->receiveMask = NimbleStepsReceiveMaskAllReceived; // If we don't mark
                                                           // everything as received,
                                                           // we will get resent old
                                                           // steps

    self->expectingWriteId = startId;
}

int nimbleStepsReceiveMaskReceivedStep(NimbleStepsReceiveMask* self, StepId stepId)
{
    if (stepId >= self->expectingWriteId) {
        int advanceBits = (stepId - self->expectingWriteId) + 1;
        if (advanceBits > 63) {
            CLOG_SOFT_ERROR("nimble steps receive mask: advancing too far into the future")
            return -51;
        }
        self->receiveMask <<= advanceBits;
        self->receiveMask |= 0x1;
        self->expectingWriteId = stepId + 1;
    } else {
        // It was a previous step
        size_t bitsFromHead = (self->expectingWriteId - stepId) - 1;
        if (bitsFromHead > 63) {
            CLOG_SOFT_ERROR("nimble steps receive mask: too far in the past")
            return -44;
        }
        uint64_t maskForThisStep = 1ULL << bitsFromHead;
#if 1
        if ((self->receiveMask & maskForThisStep) != 0) {
            CLOG_VERBOSE("we have already received stepId %08X", stepId)
        }
#endif
        self->receiveMask |= maskForThisStep;
    }

    return 0;
}
