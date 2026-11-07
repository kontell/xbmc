/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "cores/DataCacheCore.h"

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

// A player stores its time on a cadence; between two stores the reading moves on at the
// playing rate. The bounds below leave room for the sleep overshooting on a loaded box.

TEST(TestDataCacheCore, PlayTimeMovesOnBetweenStoresAtThePlayingRate)
{
  CDataCacheCore cache;
  cache.SetSpeed(1.0f, 1.0f);
  cache.SetPlayTimes(0, 10000, 0, 60000);

  std::this_thread::sleep_for(150ms);

  const int64_t time = cache.GetPlayTime();
  EXPECT_GE(time, 10140);
  EXPECT_LE(time, 10600);
}

TEST(TestDataCacheCore, PlayTimeMovesOnAtTheTempo)
{
  CDataCacheCore cache;
  cache.SetSpeed(2.0f, 1.0f);
  cache.SetPlayTimes(0, 10000, 0, 60000);

  std::this_thread::sleep_for(150ms);

  const int64_t time = cache.GetPlayTime();
  EXPECT_GE(time, 10280);
  EXPECT_LE(time, 11200);
}

TEST(TestDataCacheCore, PlayTimeMovesOnAtTheTrickplaySpeedWithATempoKept)
{
  CDataCacheCore cache;
  cache.SetSpeed(1.5f, 2.0f);
  const auto stored = std::chrono::steady_clock::now();
  cache.SetPlayTimes(0, 10000, 0, 60000);

  std::this_thread::sleep_for(150ms);

  const int64_t moved = cache.GetPlayTime() - 10000;
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - stored)
                           .count();
  // at the speed, not at the speed times the tempo
  EXPECT_GE(moved, 290);
  EXPECT_LE(moved, 2 * elapsed + 2);
}

TEST(TestDataCacheCore, PlayTimeHoldsWhilePaused)
{
  CDataCacheCore cache;
  cache.SetSpeed(1.0f, 0.0f);
  cache.SetPlayTimes(0, 10000, 0, 60000);

  std::this_thread::sleep_for(60ms);

  EXPECT_EQ(cache.GetPlayTime(), 10000);
}

TEST(TestDataCacheCore, PlayTimeHoldsWhileTheClockIsStopped)
{
  CDataCacheCore cache;
  cache.SetSpeed(1.0f, 1.0f);
  cache.SetPlayTimes(0, 10000, 0, 60000, false);

  std::this_thread::sleep_for(60ms);

  EXPECT_EQ(cache.GetPlayTime(), 10000);
}

TEST(TestDataCacheCore, PlayTimeKeepsItsPlaceWhenPaused)
{
  CDataCacheCore cache;
  cache.SetSpeed(1.0f, 1.0f);
  cache.SetPlayTimes(0, 10000, 0, 60000);

  std::this_thread::sleep_for(150ms);
  cache.SetSpeed(1.0f, 0.0f);

  const int64_t paused = cache.GetPlayTime();
  EXPECT_GE(paused, 10140);
  EXPECT_LE(paused, 10600);

  std::this_thread::sleep_for(60ms);
  EXPECT_EQ(cache.GetPlayTime(), paused);
}

TEST(TestDataCacheCore, PlayTimeKeepsItsPlaceWhenTheTempoChanges)
{
  CDataCacheCore cache;
  cache.SetSpeed(1.0f, 1.0f);
  cache.SetPlayTimes(0, 10000, 0, 60000);

  std::this_thread::sleep_for(150ms);
  const int64_t before = cache.GetPlayTime();
  cache.SetSpeed(2.0f, 1.0f);
  const int64_t after = cache.GetPlayTime();

  // the time already played is not played again at the new tempo
  EXPECT_GE(after, before);
  EXPECT_LE(after, before + 50);

  std::this_thread::sleep_for(100ms);
  EXPECT_GE(cache.GetPlayTime(), after + 190);
}

TEST(TestDataCacheCore, PlayTimeNeverRunsPastTheEnd)
{
  CDataCacheCore cache;
  cache.SetSpeed(1.0f, 32.0f);
  cache.SetPlayTimes(0, 59950, 0, 60000);

  std::this_thread::sleep_for(100ms);

  EXPECT_EQ(cache.GetPlayTime(), 60000);
}

TEST(TestDataCacheCore, PlayTimeIsWhatWasStoredBeforeAnyStore)
{
  CDataCacheCore cache;
  cache.SetSpeed(1.0f, 1.0f);

  EXPECT_EQ(cache.GetPlayTime(), 0);
}
