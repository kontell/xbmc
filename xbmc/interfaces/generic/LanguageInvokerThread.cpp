/*
 *  Copyright (C) 2013-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "LanguageInvokerThread.h"

#include "ScriptInvocationManager.h"

#include <utility>

CLanguageInvokerThread::CLanguageInvokerThread(std::shared_ptr<ILanguageInvoker> invoker,
                                               CScriptInvocationManager* invocationManager,
                                               bool reuseable)
  : ILanguageInvoker(NULL),
    CThread("LanguageInvoker"),
    m_invoker(std::move(invoker)),
    m_invocationManager(invocationManager),
    m_mayReuse(reuseable)
{ }

CLanguageInvokerThread::~CLanguageInvokerThread()
{
  Stop(true);
}

InvokerState CLanguageInvokerThread::GetState() const
{
  if (m_invoker == NULL)
    return InvokerStateFailed;

  return m_invoker->GetState();
}

void CLanguageInvokerThread::Release()
{
  std::unique_lock<std::mutex> lck(m_mutex);
  m_bStop = true;
  m_condition.notify_one();
}

bool CLanguageInvokerThread::Claim()
{
  bool unclaimed = false;
  return m_claimed.compare_exchange_strong(unclaimed, true);
}

void CLanguageInvokerThread::ReleaseClaim()
{
  // m_claimed is atomic and no wait predicate depends on it, so just drop it. A
  // run that ended non-reusable has already retired itself to Done; the claim
  // holder outlives it and this is a harmless no-op on its phase.
  m_claimed = false;
}

bool CLanguageInvokerThread::execute(const std::string &script, const std::vector<std::string> &arguments)
{
  if (m_invoker == NULL || script.empty())
    return false;

  std::unique_lock<std::mutex> lck(m_mutex);

  // Refuse once the loop has left (Done) or been asked to stop: a restart set
  // here would be dropped by the loop or notify a condition nobody waits on, and
  // the caller would wait on a script that never runs. Start a fresh thread.
  if (m_phase == Phase::Done || m_bStop)
    return false;

  m_script = script;
  m_args = arguments;

  if (CThread::IsRunning())
  {
    m_phase = Phase::Queued;
    m_condition.notify_one();
    return true;
  }

  lck.unlock();
  Create();

  //Todo wait until running

  return true;
}

bool CLanguageInvokerThread::stop(bool wait)
{
  if (m_invoker == NULL)
    return false;

  if (!CThread::IsRunning())
    return false;

  Release();

  bool result = true;
  if (m_invoker->GetState() < InvokerStateExecutionDone)
  {
    // stop the language-specific invoker
    result = m_invoker->Stop(wait);
  }
  // stop the thread
  CThread::StopThread(wait);

  return result;
}

void CLanguageInvokerThread::OnStartup()
{
  if (m_invoker == NULL)
    return;

  m_invoker->SetId(GetId());
  if (m_addon != NULL)
    m_invoker->SetAddon(m_addon);
}

void CLanguageInvokerThread::Process()
{
  std::unique_lock<std::mutex> lckdl(m_mutex);
  if (m_invoker == NULL)
  {
    m_phase = Phase::Done;
    return;
  }

  for (;;)
  {
    m_phase = Phase::Running;
    const std::string script = m_script;
    const std::vector<std::string> args = m_args;
    // Execute() talks to the GUI and to other invokers. Holding m_mutex across
    // it deadlocks Home.xml load: a JobWorker in Release()/restart waits for
    // this lock, the app thread waits for that job, and this script waits for
    // the GUI.
    lckdl.unlock();
    try
    {
      m_invoker->Execute(script, args);
    }
    catch (...)
    {
      // Settle the phase before unwinding: until it is Done, execute() would
      // treat this dying thread as able to take another script.
      lckdl.lock();
      m_phase = Phase::Done;
      m_condition.notify_one();
      throw;
    }
    lckdl.lock();

    // This run's own outcome, read on this thread before any claimer can Reset()
    // the invoker. Only a clean finish on a reuse-enabled thread parks; anything
    // else retires it even with a claim still held, since the claim holder's
    // wait ends on this thread reaching OnExit(), not the reverse.
    const bool reusable = m_mayReuse && m_invoker->GetState() == InvokerStateScriptDone;
    if (m_bStop || !reusable)
    {
      m_phase = Phase::Done;
      break;
    }

    // Park the idle interpreter and wait for the next script or a stop. A restart
    // handed over during the run already set Queued; leave it for the predicate.
    if (m_phase != Phase::Queued)
      m_phase = Phase::Parked;
    m_condition.wait(lckdl, [this] { return m_bStop || m_phase == Phase::Queued; });
    if (m_bStop)
    {
      m_phase = Phase::Done;
      break;
    }
  }
}

void CLanguageInvokerThread::OnExit()
{
  // Process() has already settled the phase at Done on its way out, including
  // the exception path, so there is nothing left to do here but notify the
  // manager the run is done.
  if (m_invoker == NULL)
    return;

  m_invoker->onExecutionDone();
  m_invocationManager->OnExecutionDone(GetId());
}

void CLanguageInvokerThread::OnException()
{
  if (m_invoker == NULL)
    return;

  m_invoker->onExecutionFailed();
  m_invocationManager->OnExecutionDone(GetId());
}
