// Copyright (c) 2019-2024, The Khronos Group Inc.
//
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "conformance_utils.h"
#include "conformance_framework.h"
#include "utilities/types_and_constants.h"
#include "utilities/throw_helpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include <openxr/openxr.h>
#include <openxr/openxr_reflection.h>

#include <chrono>
#include <thread>

namespace Conformance
{
    static bool tryReadEvent(XrInstance instance, XrEventDataBuffer* evt)
    {
        *evt = {XR_TYPE_EVENT_DATA_BUFFER};
        XrResult res;
        XRC_CHECK_THROW_XRCMD(res = xrPollEvent(instance, evt));
        return res == XR_SUCCESS;
    }

    static bool tryGetNextSessionState(XrInstance instance, XrEventDataSessionStateChanged* evt)
    {
        XrEventDataBuffer buffer;
        while (tryReadEvent(instance, &buffer)) {
            if (buffer.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                *evt = *reinterpret_cast<XrEventDataSessionStateChanged*>(&buffer);
                return true;
            }
        }
        return false;
    }

    static bool waitForNextSessionState(XrInstance instance, XrEventDataSessionStateChanged* evt, std::chrono::nanoseconds duration = 1s)
    {
        CountdownTimer countdown(duration);
        while (!countdown.IsTimeUp()) {
            if (tryGetNextSessionState(instance, evt)) {
                return true;
            }

            std::this_thread::sleep_for(5ms);
        }

        return false;
    }

    static void submitFrame(XrSession session)
    {
        XrFrameState frameState{XR_TYPE_FRAME_STATE};
        XRC_CHECK_THROW_XRCMD(xrWaitFrame(session, nullptr, &frameState));
        XRC_CHECK_THROW_XRCMD(xrBeginFrame(session, nullptr));

        XrFrameEndInfo frameEndInfo{XR_TYPE_FRAME_END_INFO};
        frameEndInfo.displayTime = frameState.predictedDisplayTime;
        frameEndInfo.environmentBlendMode = GetGlobalData().GetOptions().environmentBlendModeValue;
        XRC_CHECK_THROW_XRCMD(xrEndFrame(session, &frameEndInfo));
    }

    static void submitFramesUntilSessionState(XrInstance instance, XrSession session, XrSessionState expectedSessionState,
                                              std::chrono::nanoseconds duration = 30s)
    {
        CAPTURE(expectedSessionState);

        CountdownTimer countdown(duration);
        while (!countdown.IsTimeUp()) {
            XrEventDataSessionStateChanged evt;
            if (tryGetNextSessionState(instance, &evt)) {
                REQUIRE(evt.state == expectedSessionState);
                return;
            }
            submitFrame(session);
        }

        FAIL("Failed to reach expected session state");
    }

    TEST_CASE("SessionState", "")
    {
        AutoBasicInstance instance;

        SECTION("Cycle through all states")
        {
            AutoBasicSession session(AutoBasicSession::createSession, instance);

            REQUIRE(session != XR_NULL_HANDLE_CPP);

            XrEventDataSessionStateChanged evt{};
            REQUIRE(waitForNextSessionState(instance, &evt) == true);
            REQUIRE_MSG(evt.state == XR_SESSION_STATE_IDLE, "Unexpected session state " << evt.state);

            REQUIRE(waitForNextSessionState(instance, &evt) == true);
            REQUIRE_MSG(evt.state == XR_SESSION_STATE_READY, "Unexpected session state " << evt.state);

            XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
            beginInfo.primaryViewConfigurationType = GetGlobalData().GetOptions().viewConfigurationValue;
            REQUIRE(XR_SUCCESS == xrBeginSession(session, &beginInfo));

            submitFramesUntilSessionState(instance, session, XR_SESSION_STATE_SYNCHRONIZED);
            submitFramesUntilSessionState(instance, session, XR_SESSION_STATE_VISIBLE);
            submitFramesUntilSessionState(instance, session, XR_SESSION_STATE_FOCUSED);

            // Runtime should only allow ending a session in the STOPPING state.
            REQUIRE(XR_ERROR_SESSION_NOT_STOPPING == xrEndSession(session));

            REQUIRE(XR_SUCCESS == xrRequestExitSession(session));

            submitFramesUntilSessionState(instance, session, XR_SESSION_STATE_VISIBLE);
            submitFramesUntilSessionState(instance, session, XR_SESSION_STATE_SYNCHRONIZED);
            submitFramesUntilSessionState(instance, session, XR_SESSION_STATE_STOPPING);

            // Runtime should not transition from STOPPING to IDLE until the session has been ended.
            // This will wait 1 second before assuming no such incorrect event will come.
            REQUIRE_MSG(waitForNextSessionState(instance, &evt) == false, "Premature progression from STOPPING to IDLE state");

            REQUIRE(XR_SUCCESS == xrEndSession(session));

            submitFramesUntilSessionState(instance, session, XR_SESSION_STATE_IDLE);

            // https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html#session-lifecycle
            // If the runtime determines that its use of this XR session has
            // concluded, it will transition the session state from
            // XR_SESSION_STATE_IDLE to XR_SESSION_STATE_EXITING.
            submitFramesUntilSessionState(instance, session, XR_SESSION_STATE_EXITING);

            // When the application receives the XR_SESSION_STATE_EXITING
            // event, it releases the resources related to the session and
            // calls xrDestroySession.
        }

        SECTION("xrRequestExitSession Session Not Running")
        {
            SECTION("Session not running before starting")
            {
                AutoBasicSession session(AutoBasicSession::createSession, instance);

                REQUIRE(XR_ERROR_SESSION_NOT_RUNNING == xrRequestExitSession(session));
            }

            SECTION("Session not running after ending")
            {
                // A session is considered running after a successful call to
                // xrBeginSession and remains running until any call is made to
                // xrEndSession. Certain functions are only valid to call when
                // a session is running, such as xrWaitFrame, or else the
                // XR_ERROR_SESSION_NOT_RUNNING error must be returned by the
                // runtime.

                // If session is not running when xrRequestExitSession is
                // called, XR_ERROR_SESSION_NOT_RUNNING must be returned.

                AutoBasicSession session(
                    AutoBasicSession::beginSession | AutoBasicSession::createSpaces | AutoBasicSession::createSwapchains, instance);
                REQUIRE(XR_SUCCESS == xrRequestExitSession(session));

                FrameIterator frameIterator(&session);
                frameIterator.RunToSessionState(XR_SESSION_STATE_STOPPING);
                REQUIRE(XR_SUCCESS == xrEndSession(session));

                // Actually test what we want to test!
                REQUIRE(XR_ERROR_SESSION_NOT_RUNNING == xrRequestExitSession(session));
            }
        }

        // Runtime should not transition from READY to SYNCHRONIZED until one
        // or more frames have been submitted.
        // The exception is if the runtime is transitioning to STOPPING, which
        // should not happen during conformance testing.
        if (GetGlobalData().IsUsingGraphicsPlugin()) {
            SECTION("Advance without frame submission")
            {
                AutoBasicSession session(AutoBasicSession::createSession, instance);

                FrameIterator frameIterator(&session);
                frameIterator.RunToSessionState(XR_SESSION_STATE_READY);

                XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
                beginInfo.primaryViewConfigurationType = GetGlobalData().GetOptions().viewConfigurationValue;
                REQUIRE(XR_SUCCESS == xrBeginSession(session, &beginInfo));

                // This will wait 1 second before assuming no such incorrect event will come.
                XrEventDataSessionStateChanged evt;
                REQUIRE_MSG(waitForNextSessionState(instance, &evt) == false, "Premature progression from READY to SYNCHRONIZED state");
            }
        }
    }
}  // namespace Conformance
