/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
#include "utest.h"
#include <imprint/linear_allocator.h>
#include <nimble-steps/pending_steps.h>

UTEST(NimbleSteps, verifyReceiveMask)
{
    NbsPendingRange targetRanges[4];

    StepId startId = 53;
    StepId lastId = 80;
    uint64_t mask = NimbleStepsReceiveMaskAllReceived & (~0b110);

    int rangeCount = nbsPendingStepsRanges(startId - 1, lastId, mask, targetRanges, 4, 20);

    Clog log;

    log.config = &g_clog;
    log.constantPrefix = "verifyReceiveMask";

    nbsPendingStepsRangesDebugOutput(targetRanges, "debug", rangeCount, log);

    ASSERT_EQ(1, rangeCount);
    ASSERT_EQ(2, targetRanges[0].count);
    ASSERT_EQ(startId - 3, targetRanges[0].startId);
}

UTEST(NimbleSteps, verifyReceiveMask2)
{
    Clog log;

    log.config = &g_clog;
    log.constantPrefix = "verifyReceiveMask2";

    NimbleStepsReceiveMask receiveMask;

    StepId startId = 50;

    nimbleStepsReceiveMaskInit(&receiveMask, startId);

    StepId receivedId = startId + 2;
    nimbleStepsReceiveMaskReceivedStep(&receiveMask, receivedId);
    nimbleStepsReceiveMaskDebugMask(&receiveMask, "test2", log);

    ASSERT_EQ(receivedId + 1, receiveMask.expectingWriteId);
    NimbleStepsReceiveMaskBits expectedMask = NimbleStepsReceiveMaskAllReceived & (~0b110);

    CLOG_DEBUG("%04X vs %04X", expectedMask, receiveMask.receiveMask)

    ASSERT_EQ(expectedMask, receiveMask.receiveMask);
}

UTEST(NimbleSteps, verifyReceiveOldStep)
{
    Clog log;

    log.config = &g_clog;
    log.constantPrefix = "verifyReceiveMask2";

    NimbleStepsReceiveMask receiveMask;

    StepId startId = 50;

    nimbleStepsReceiveMaskInit(&receiveMask, startId);

    StepId receivedId = startId + 10;
    nimbleStepsReceiveMaskReceivedStep(&receiveMask, receivedId);
    nimbleStepsReceiveMaskDebugMask(&receiveMask, "first receive", log);

    ASSERT_EQ(receivedId + 1, receiveMask.expectingWriteId);
    NimbleStepsReceiveMaskBits expectedMask = NimbleStepsReceiveMaskAllReceived & (~0b11111111110);

    CLOG_DEBUG("%04X vs %04X", expectedMask, receiveMask.receiveMask)
    ASSERT_EQ(expectedMask, receiveMask.receiveMask);

    StepId oldStepId = startId + 4;
    NimbleStepsReceiveMaskBits expectedMaskAfterOld = NimbleStepsReceiveMaskAllReceived & (~0b11110111110);
    nimbleStepsReceiveMaskReceivedStep(&receiveMask, oldStepId);
    nimbleStepsReceiveMaskDebugMask(&receiveMask, "after old step", log);

    CLOG_DEBUG("%04X vs %04X", expectedMaskAfterOld, receiveMask.receiveMask)
    ASSERT_EQ(expectedMaskAfterOld, receiveMask.receiveMask);
    ASSERT_EQ(receivedId + 1, receiveMask.expectingWriteId);
}

UTEST(NimbleSteps, receivedTooFarInTheFuture)
{
    Clog log;

    log.config = &g_clog;
    log.constantPrefix = "verifyReceiveMask2";

    NimbleStepsReceiveMask receiveMask;

    StepId startId = 50;

    nimbleStepsReceiveMaskInit(&receiveMask, startId);

    StepId receivedId = startId + 64;
    int error = nimbleStepsReceiveMaskReceivedStep(&receiveMask, receivedId);
    ASSERT_TRUE(error < 0);
}

UTEST(NimbleSteps, receivedTooFarInThePast)
{
    Clog log;

    log.config = &g_clog;
    log.constantPrefix = "verifyReceiveMask2";

    NimbleStepsReceiveMask receiveMask;

    StepId startId = 99;

    nimbleStepsReceiveMaskInit(&receiveMask, startId);

    StepId receivedId = startId - 64;
    int error = nimbleStepsReceiveMaskReceivedStep(&receiveMask, receivedId);
    ASSERT_EQ(0, error);

    StepId receivedId2 = startId - 64 - 1;
    int error2 = nimbleStepsReceiveMaskReceivedStep(&receiveMask, receivedId2);
    ASSERT_LT(error2, 0);
}
