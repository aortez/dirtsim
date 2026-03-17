/**
 * @file StateSimRunning_test.cpp
 * @brief Unit tests for UI SimRunning latency bookkeeping.
 */

#include "ui/state-machine/StateMachine.h"
#include "ui/state-machine/states/SimRunning.h"
#include <gtest/gtest.h>

using namespace DirtSim;
using namespace DirtSim::Ui;
using namespace DirtSim::Ui::State;

namespace {

uint64_t steadyClockNs(std::chrono::steady_clock::time_point timePoint)
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(timePoint.time_since_epoch()).count());
}

} // namespace

TEST(UiStateSimRunningTest, DisplayFlushRecordsLatencyStatsForPendingLiveInputMeasurement)
{
    SimRunning state;

    const auto serverSendTime = std::chrono::steady_clock::now();
    const auto observedTime = serverSendTime - std::chrono::milliseconds(6);
    const auto requestTime = observedTime + std::chrono::milliseconds(2);
    const auto latchTime = requestTime + std::chrono::milliseconds(3);
    const auto receiveTime = serverSendTime + std::chrono::milliseconds(2);
    const auto uiApplyTime = receiveTime + std::chrono::milliseconds(1);
    const auto timerStartTime = uiApplyTime + std::chrono::milliseconds(1);
    const auto flushTime = timerStartTime + std::chrono::milliseconds(2);

    state.pendingDisplayMeasurement = true;
    state.pendingDisplayReceiveTime = receiveTime;
    state.pendingDisplayUiApplyTime = uiApplyTime;
    state.pendingDisplayServerSendTimestampNs = steadyClockNs(serverSendTime);
    state.pendingDisplayControllerObservedTimestampNs = steadyClockNs(observedTime);
    state.pendingDisplayControllerRequestTimestampNs = steadyClockNs(requestTime);
    state.pendingDisplayControllerLatchTimestampNs = steadyClockNs(latchTime);
    state.pendingDisplayControllerSequenceId = 99;
    state.pendingDisplayHandlePhase =
        static_cast<uint8_t>(DisplayLoopPhase::ProcessEventsAfterRender);

    state.onDisplayTimerHandlerStart(timerStartTime);
    state.onDisplayFlush(flushTime);

    ASSERT_EQ(state.displayFrameStagingStats.sampleCount, 1u);
    EXPECT_EQ(state.displayFrameStagingStats.handledAfterRenderCount, 1u);
    EXPECT_EQ(state.displayFrameStagingStats.handledBeforeRenderCount, 0u);
    EXPECT_DOUBLE_EQ(
        state.displayFrameStagingStats.receiveToUiApplyMs.average(
            state.displayFrameStagingStats.sampleCount),
        1.0);
    EXPECT_DOUBLE_EQ(
        state.displayFrameStagingStats.uiApplyToTimerStartMs.average(
            state.displayFrameStagingStats.sampleCount),
        1.0);
    EXPECT_DOUBLE_EQ(
        state.displayFrameStagingStats.timerStartToFlushMs.average(
            state.displayFrameStagingStats.sampleCount),
        2.0);

    ASSERT_EQ(state.displayLatencyCount, 1u);
    EXPECT_DOUBLE_EQ(state.displayLatencyMs.average(state.displayLatencyCount), 6.0);

    ASSERT_EQ(state.liveInputLatencyStats.observedLatencyCount, 1u);
    ASSERT_EQ(state.liveInputLatencyStats.inputLatencyCount, 1u);
    EXPECT_DOUBLE_EQ(
        state.liveInputLatencyStats.observedToRequestMs.average(
            state.liveInputLatencyStats.observedLatencyCount),
        2.0);
    EXPECT_DOUBLE_EQ(
        state.liveInputLatencyStats.observedToLatchMs.average(
            state.liveInputLatencyStats.observedLatencyCount),
        5.0);
    EXPECT_DOUBLE_EQ(
        state.liveInputLatencyStats.requestToLatchMs.average(
            state.liveInputLatencyStats.inputLatencyCount),
        3.0);
    EXPECT_DOUBLE_EQ(
        state.liveInputLatencyStats.requestToDisplayMs.average(
            state.liveInputLatencyStats.inputLatencyCount),
        10.0);
    EXPECT_DOUBLE_EQ(
        state.liveInputLatencyStats.latchToDisplayMs.average(
            state.liveInputLatencyStats.inputLatencyCount),
        7.0);

    EXPECT_FALSE(state.pendingDisplayMeasurement);
    EXPECT_EQ(state.pendingDisplayControllerSequenceId, 0u);
    EXPECT_EQ(state.lastDisplayedControllerSequenceId, 99u);
}

TEST(UiStateSimRunningTest, OutOfOrderSmbResponseFrameIdsDoNotUnderflowFramesAfterLatch)
{
    SimRunning state;

    const auto requestTime = std::chrono::steady_clock::now() - std::chrono::milliseconds(6);
    const auto latchTime = requestTime + std::chrono::milliseconds(2);
    const auto responseDetectedTime = latchTime + std::chrono::milliseconds(1);
    const auto flushTime = responseDetectedTime + std::chrono::milliseconds(2);

    state.pendingDisplaySmbResponseMeasurement = true;
    state.pendingDisplaySmbResponseEventId = 5;
    state.pendingDisplaySmbResponseAppliedFrameId = 100;
    state.pendingDisplaySmbResponseFrameId = 99;
    state.pendingDisplaySmbResponseRequestTimestampNs = steadyClockNs(requestTime);
    state.pendingDisplaySmbResponseLatchTimestampNs = steadyClockNs(latchTime);
    state.pendingDisplaySmbResponseDetectedTimestampNs = steadyClockNs(responseDetectedTime);

    state.onDisplayFlush(flushTime);

    EXPECT_EQ(state.smbResponseLatencyStats.latencyCount, 0u);
    EXPECT_EQ(state.smbResponseLatencyStats.framesAfterLatch.total, 0.0);
    EXPECT_EQ(state.lastDisplayedSmbResponseEventId, 5u);
    EXPECT_FALSE(state.pendingDisplaySmbResponseMeasurement);
    EXPECT_EQ(state.pendingDisplaySmbResponseEventId, 0u);
}
