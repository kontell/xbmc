/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "cores/VideoPlayer/DVDClock.h"
#include "cores/VideoPlayer/Interface/TimingConstants.h"

#include <thread>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace
{
// The clock runs on the system time here, so the waits are real; the tolerances allow for a
// loaded machine.
constexpr double TOLERANCE = DVD_MSEC_TO_TIME(40);
} // namespace

TEST(TestDVDClock, SetSpeedAtChangesSpeedWhenDue)
{
  CDVDClock clock;
  clock.Discontinuity(0);
  clock.SetSpeed(DVD_PLAYSPEED_NORMAL);

  clock.SetSpeedAt(2 * DVD_PLAYSPEED_NORMAL, clock.GetAbsoluteClock() + DVD_MSEC_TO_TIME(150));
  EXPECT_NEAR(clock.GetClockSpeed(), 1.0, 1e-6);

  std::this_thread::sleep_for(300ms);
  EXPECT_NEAR(clock.GetClockSpeed(), 2.0, 1e-6);
}

TEST(TestDVDClock, ClockIsContinuousWhenTheSpeedChanges)
{
  CDVDClock clock;
  clock.Discontinuity(0);
  clock.SetSpeed(DVD_PLAYSPEED_NORMAL);

  const double due = clock.GetAbsoluteClock() + DVD_MSEC_TO_TIME(100);
  clock.SetSpeedAt(2 * DVD_PLAYSPEED_NORMAL, due);
  std::this_thread::sleep_for(200ms);

  // 100 ms at 1x, then 2x from the due time to the moment of the reading
  double absolute = 0;
  const double now = clock.GetClock(absolute);
  const double expected = DVD_MSEC_TO_TIME(100) + 2 * (absolute - due);
  EXPECT_NEAR(now, expected, TOLERANCE);
}

TEST(TestDVDClock, SetSpeedReplacesAPendingChange)
{
  CDVDClock clock;
  clock.Discontinuity(0);
  clock.SetSpeed(DVD_PLAYSPEED_NORMAL);

  clock.SetSpeedAt(2 * DVD_PLAYSPEED_NORMAL, clock.GetAbsoluteClock() + DVD_MSEC_TO_TIME(100));
  clock.SetSpeed(DVD_PLAYSPEED_NORMAL);
  std::this_thread::sleep_for(200ms);

  EXPECT_NEAR(clock.GetClockSpeed(), 1.0, 1e-6);
}

TEST(TestDVDClock, PauseTakesAPendingChange)
{
  CDVDClock clock;
  clock.Discontinuity(0);
  clock.SetSpeed(DVD_PLAYSPEED_NORMAL);

  clock.SetSpeedAt(2 * DVD_PLAYSPEED_NORMAL, clock.GetAbsoluteClock() + DVD_MSEC_TO_TIME(1000));
  clock.Pause(true);
  clock.Pause(false);

  EXPECT_NEAR(clock.GetClockSpeed(), 2.0, 1e-6);
}
