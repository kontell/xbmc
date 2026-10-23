/*
 *  Copyright (C) 2013-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "interfaces/generic/ILanguageInvoker.h"
#include "threads/Thread.h"

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

class CScriptInvocationManager;

class CLanguageInvokerThread : public ILanguageInvoker, protected CThread
{
public:
  CLanguageInvokerThread(std::shared_ptr<ILanguageInvoker> invoker,
                         CScriptInvocationManager* invocationManager,
                         bool reuseable);
  ~CLanguageInvokerThread() override;

  virtual InvokerState GetState() const;

  std::shared_ptr<ILanguageInvoker> GetInvoker() const { return m_invoker; }

  bool Reuseable(const std::string& script) const
  {
    // Parked is reached only after a run the thread is allowed to reuse ended
    // cleanly, so it already means "an idle interpreter is waiting". A claim is
    // the reservation; a would-be new claimer may only take an unclaimed one.
    std::unique_lock<std::mutex> lck(m_mutex);
    return m_phase == Phase::Parked && !m_claimed && !m_bStop && m_script == script;
  }

  /*!
   * \brief Try to take the claim on this thread.
   *
   * A claim marks the thread as spoken for from the moment it is handed out
   * until its owner drops it, which is what covers the gap between Reset() and
   * the script actually running. Only one claim can be held at a time.
   *
   * \return True if the claim was taken, false if somebody else already holds it
   */
  bool Claim();

  //! \brief Drop the claim taken by Claim().
  void ReleaseClaim();

  bool IsClaimed() const { return m_claimed; }

  //! \brief True once Release()/stop() has asked this thread to finish.
  //! Deliberately not IsStopping(): that is an ILanguageInvoker virtual meaning
  //! "the invoker reached InvokerStateStopping", which is a different question.
  bool IsStopRequested() const { return m_bStop; }

  //! \brief True once the Process() loop has left and will accept no more work.
  bool IsProcessDone() const
  {
    std::unique_lock<std::mutex> lck(m_mutex);
    return m_phase == Phase::Done;
  }

  /*!
   * \brief True while this thread must keep the reusable slot.
   *
   * A claim reserves the thread whatever it is doing, and a thread handed a
   * script it has not started yet (Starting, Queued) would drop that script if
   * released. A running script is not lost: after Release() the thread finishes
   * it and exits, so a newer reusable run can take the slot.
   */
  bool IsSpokenFor() const
  {
    std::unique_lock<std::mutex> lck(m_mutex);
    return m_claimed || m_phase == Phase::Starting || m_phase == Phase::Queued;
  }

  virtual void Release();

protected:
  bool execute(const std::string &script, const std::vector<std::string> &arguments) override;
  bool stop(bool wait) override;

  void OnStartup() override;
  void Process() override;
  void OnExit() override;

private:
  //! \brief The thread's lifecycle, guarded by m_mutex.
  enum class Phase
  {
    Starting,  //!< created and published, the loop has not taken a script yet
    Queued,    //!< execute() has handed the running loop a new script to take
    Running,   //!< the loop is inside the invoker's Execute()
    Parked,    //!< a reusable run finished; idle and available for reuse
    Done,      //!< the loop has left and accepts no more work
  };

  std::shared_ptr<ILanguageInvoker> m_invoker;
  CScriptInvocationManager *m_invocationManager;
  std::string m_script;
  std::vector<std::string> m_args;

  //! Guards m_script, m_args and m_phase. Taken while m_critSection is held
  //! (CScriptInvocationManager -> Reuseable()/Release()); never the other way
  //! round. CThread::m_bStop is atomic; it is written under this lock only to
  //! pair with m_condition.
  mutable std::mutex m_mutex;
  std::condition_variable m_condition;
  Phase m_phase = Phase::Starting;
  //! Whether this thread may be parked for reuse at all; a one-shot or service
  //! invoker is created with this false and always retires after its run.
  const bool m_mayReuse;
  std::atomic<bool> m_claimed{false};
};
