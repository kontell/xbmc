/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "DVDClock.h"

#include "VideoReferenceClock.h"
#include "cores/VideoPlayer/Interface/TimingConstants.h"
#include "utils/MathUtils.h"
#include "utils/TimeUtils.h"
#include "utils/log.h"

#include <inttypes.h>
#include <math.h>
#include <memory>
#include <mutex>

CDVDClock::CDVDClock()
{
  std::unique_lock lock(m_systemsection);

  m_pauseClock = 0;
  m_bReset = true;
  m_paused = false;
  m_speedAfterPause = DVD_PLAYSPEED_PAUSE;
  m_iDisc = 0;
  m_maxspeedadjust = 5.0;
  m_systemAdjust = 0;
  m_speedAdjust = 0;
  m_startClock = 0;
  m_vSyncAdjust = 0;
  m_frameTime = DVD_TIME_BASE / 60.0;

  m_videoRefClock = std::make_unique<CVideoReferenceClock>();
  m_lastSystemTime = m_videoRefClock->GetTime();
  m_systemOffset = m_videoRefClock->GetTime();
  m_systemFrequency = CurrentHostFrequency();
  m_systemUsed = m_systemFrequency;
}

CDVDClock::~CDVDClock() = default;

// Returns the current absolute clock in units of DVD_TIME_BASE (usually microseconds).
double CDVDClock::GetAbsoluteClock(bool interpolated /*= true*/)
{
  std::unique_lock lock(m_systemsection);

  int64_t current;
  current = m_videoRefClock->GetTime(interpolated);

  return SystemToAbsolute(current);
}

double CDVDClock::GetClock(bool interpolated /*= true*/)
{
  std::unique_lock lock(m_critSection);

  int64_t current = m_videoRefClock->GetTime(interpolated);
  m_systemAdjust += m_speedAdjust * (current - m_lastSystemTime);
  m_lastSystemTime = current;

  return SystemToPlaying(current);
}

double CDVDClock::GetClock(double& absolute, bool interpolated /*= true*/)
{
  int64_t current = m_videoRefClock->GetTime(interpolated);

  // SystemToPlaying() reads the speed changes the other threads queue under m_critSection
  std::unique_lock clockLock(m_critSection);
  std::unique_lock lock(m_systemsection);
  absolute = SystemToAbsolute(current);

  m_systemAdjust += m_speedAdjust * (current - m_lastSystemTime);
  m_lastSystemTime = current;

  return SystemToPlaying(current);
}

void CDVDClock::SetVsyncAdjust(double adjustment)
{
  std::unique_lock lock(m_critSection);
  m_vSyncAdjust = adjustment;
}

double CDVDClock::GetVsyncAdjust()
{
  std::unique_lock lock(m_critSection);
  return m_vSyncAdjust;
}

void CDVDClock::Pause(bool pause)
{
  std::unique_lock lock(m_critSection);

  if (pause && !m_paused)
  {
    // the last speed asked for is the one to resume at
    if (!m_pendingSpeeds.empty())
      SetSpeed(m_pendingSpeeds.back().speed);

    if (!m_pauseClock)
      m_speedAfterPause = m_systemFrequency * DVD_PLAYSPEED_NORMAL / m_systemUsed;
    else
      m_speedAfterPause = DVD_PLAYSPEED_PAUSE;

    SetSpeed(DVD_PLAYSPEED_PAUSE);
    m_paused = true;
  }
  else if (!pause && m_paused)
  {
    m_paused = false;
    SetSpeed(m_speedAfterPause);
  }
}

bool CDVDClock::IsPaused() const
{
  std::unique_lock lock(m_critSection);
  return m_pauseClock != 0;
}

void CDVDClock::Advance(double time)
{
  std::unique_lock lock(m_critSection);

  if (m_pauseClock)
  {
    m_pauseClock += time / DVD_TIME_BASE * m_systemFrequency;
  }
}

void CDVDClock::SetSpeed(int iSpeed)
{
  // this will sometimes be a little bit of due to rounding errors, ie clock might jump a bit when changing speed
  std::unique_lock lock(m_critSection);

  CommitDueSpeed(m_videoRefClock->GetTime());
  m_pendingSpeeds.clear();

  if (m_paused)
  {
    m_speedAfterPause = iSpeed;
    return;
  }

  if (iSpeed == DVD_PLAYSPEED_PAUSE)
  {
    if (!m_pauseClock)
      m_pauseClock = m_videoRefClock->GetTime();
    return;
  }

  int64_t current;
  int64_t newfreq = m_systemFrequency * DVD_PLAYSPEED_NORMAL / iSpeed;

  current = m_videoRefClock->GetTime();
  if (m_pauseClock)
  {
    m_startClock += current - m_pauseClock;
    m_pauseClock = 0;
  }

  m_startClock = current - (int64_t)((double)(current - m_startClock) * newfreq / m_systemUsed);
  m_systemUsed = newfreq;
}

void CDVDClock::SetSpeedAt(int iSpeed, double absolute)
{
  std::unique_lock lock(m_critSection);

  const int64_t current = m_videoRefClock->GetTime();
  const int64_t at = AbsoluteToSystem(absolute);
  if (m_paused || m_pauseClock || m_bReset || iSpeed == DVD_PLAYSPEED_PAUSE || at <= current)
  {
    SetSpeed(iSpeed);
    return;
  }

  CommitDueSpeed(current);
  // output is heard in the order it is asked for, so a change due no later than one still to
  // come takes its place
  while (!m_pendingSpeeds.empty() && m_pendingSpeeds.back().system >= at)
    m_pendingSpeeds.pop_back();
  m_pendingSpeeds.push_back({iSpeed, at, m_systemFrequency * DVD_PLAYSPEED_NORMAL / iSpeed});
}

void CDVDClock::CommitDueSpeed(int64_t system)
{
  while (!m_pendingSpeeds.empty() && system >= m_pendingSpeeds.front().system)
  {
    const PendingSpeed& due = m_pendingSpeeds.front();
    // as SetSpeed() does, at the time the change was due
    m_startClock =
        due.system - static_cast<int64_t>(static_cast<double>(due.system - m_startClock) *
                                          due.used / m_systemUsed);
    m_systemUsed = due.used;
    m_pendingSpeeds.pop_front();
  }
}

void CDVDClock::SetSpeedAdjust(double adjust)
{
  CLog::Log(LOGDEBUG, "CDVDClock::SetSpeedAdjust - adjusted:{:f}", adjust);

  std::unique_lock lock(m_critSection);
  m_speedAdjust = adjust;
}

double CDVDClock::GetSpeedAdjust() const
{
  std::unique_lock lock(m_critSection);
  return m_speedAdjust;
}

double CDVDClock::ErrorAdjust(double error, const char* log)
{
  std::unique_lock lock(m_critSection);

  double clock, absolute, adjustment;
  clock = GetClock(absolute);

  // skip minor updates while speed adjust is active
  // -> adjusting buffer levels
  if (m_speedAdjust != 0 && error < DVD_MSEC_TO_TIME(100))
  {
    return 0;
  }

  adjustment = error;

  if (m_vSyncAdjust != 0)
  {
    // Audio ahead is more noticeable then audio behind video.
    // Correct if aufio is more than 20ms ahead or more then
    // 27ms behind. In a worst case scenario we switch from
    // 20ms ahead to 21ms behind (for fps of 23.976)
    if (error > 0.02 * DVD_TIME_BASE)
      adjustment = m_frameTime;
    else if (error < -0.027 * DVD_TIME_BASE)
      adjustment = -m_frameTime;
    else
      adjustment = 0;
  }

  if (adjustment == 0)
    return 0;

  Discontinuity(clock+adjustment, absolute);

  CLog::Log(LOGDEBUG, "CDVDClock::ErrorAdjust - {} - error:{:f}, adjusted:{:f}", log, error,
            adjustment);
  return adjustment;
}

void CDVDClock::Discontinuity(double clock, double absolute)
{
  std::unique_lock lock(m_critSection);
  CommitDueSpeed(AbsoluteToSystem(absolute));
  m_startClock = AbsoluteToSystem(absolute);
  if(m_pauseClock)
    m_pauseClock = m_startClock;
  m_iDisc = clock;
  m_bReset = false;
  m_systemAdjust = 0;
  m_speedAdjust = 0;
}

void CDVDClock::SetMaxSpeedAdjust(double speed)
{
  std::unique_lock lock(m_speedsection);

  m_maxspeedadjust = speed;
}

//returns the refreshrate if the videoreferenceclock is running, -1 otherwise
int CDVDClock::UpdateFramerate(double fps, double* interval /*= NULL*/)
{
  //sent with fps of 0 means we are not playing video
  if(fps == 0.0)
    return -1;

  m_frameTime = 1/fps * DVD_TIME_BASE;

  //check if the videoreferenceclock is running, will return -1 if not
  double rate = m_videoRefClock->GetRefreshRate(interval);

  if (rate <= 0)
    return -1;

  std::unique_lock lock(m_speedsection);

  double weight = (rate * 2) / fps;

  //set the speed of the videoreferenceclock based on fps, refreshrate and maximum speed adjust set by user
  if (m_maxspeedadjust > 0.05)
  {
    if (weight / MathUtils::round_int(weight) < 1.0 + m_maxspeedadjust / 100.0 &&
      weight / MathUtils::round_int(weight) > 1.0 - m_maxspeedadjust / 100.0)
      weight = MathUtils::round_int(weight);
  }
  double speed = (rate * 2.0 ) / (fps * weight);
  lock.unlock();

  m_videoRefClock->SetSpeed(speed);

  return rate;
}

bool CDVDClock::GetClockInfo(int& MissedVblanks, double& ClockSpeed, double& RefreshRate) const
{
  return m_videoRefClock->GetClockInfo(MissedVblanks, ClockSpeed, RefreshRate);
}

double CDVDClock::SystemToAbsolute(int64_t system) const
{
  return DVD_TIME_BASE * (double)(system - m_systemOffset) / m_systemFrequency;
}

int64_t CDVDClock::AbsoluteToSystem(double absolute) const
{
  return (int64_t)(absolute / DVD_TIME_BASE * m_systemFrequency) + m_systemOffset;
}

double CDVDClock::SystemToPlaying(int64_t system)
{
  int64_t current;

  if (m_bReset)
  {
    m_startClock = system;
    m_systemUsed = m_systemFrequency;
    if(m_pauseClock)
      m_pauseClock = m_startClock;
    m_iDisc = 0;
    m_systemAdjust = 0;
    m_speedAdjust = 0;
    m_vSyncAdjust = 0;
    m_bReset = false;
    m_pendingSpeeds.clear();
  }

  if (m_pauseClock)
    current = m_pauseClock;
  else
    current = system;

  // speed changes that have come due but are not yet taken run from their own times
  int64_t from = m_startClock - m_systemAdjust;
  int64_t used = m_systemUsed;
  double playing = 0.0;
  for (const PendingSpeed& change : m_pendingSpeeds)
  {
    if (change.system >= current)
      break;
    playing += static_cast<double>(change.system - from) / used;
    from = change.system;
    used = change.used;
  }
  playing += static_cast<double>(current - from) / used;

  return DVD_TIME_BASE * playing + m_iDisc;
}

double CDVDClock::GetClockSpeed() const
{
  std::unique_lock lock(m_critSection);

  const int64_t current = m_videoRefClock->GetTime();
  int64_t used = m_systemUsed;
  for (const PendingSpeed& change : m_pendingSpeeds)
  {
    if (change.system > current)
      break;
    used = change.used;
  }
  double speed = (double)m_systemFrequency / used;
  return m_videoRefClock->GetSpeed() * speed + m_speedAdjust;
}
