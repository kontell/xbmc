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
    m_reusable(reuseable)
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
  std::unique_lock<std::mutex> lck(m_mutex);
  m_claimed = false;
  // Dropping the claim can be what makes a parked thread retirable (e.g. a run
  // that ended Failed), so wake the loop to re-evaluate rather than leaving it
  // parked until the next manager call.
  m_condition.notify_one();
}

bool CLanguageInvokerThread::execute(const std::string &script, const std::vector<std::string> &arguments)
{
  if (m_invoker == NULL || script.empty())
    return false;

  std::unique_lock<std::mutex> lck(m_mutex);

  // Refuse once the loop has decided to finish (m_processDone) or been asked to
  // stop (m_bStop): a restart set here would be dropped by the loop's keepGoing
  // test or notify a condition nobody waits on, and the caller would wait on a
  // script that never runs. Let the caller start a fresh thread instead.
  if (m_processDone || m_bStop)
    return false;

  m_script = script;
  m_args = arguments;

  if (CThread::IsRunning())
  {
    m_restart = true;
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
  if (m_invoker == NULL)
    return;

  std::unique_lock<std::mutex> lckdl(m_mutex);
  bool keepGoing = true;
  while (keepGoing)
  {
    m_restart = false;
    m_executing = true;
    const std::string script = m_script;
    const std::vector<std::string> args = m_args;
    // Execute() talks to the GUI and to other invokers. Holding m_mutex
    // across it deadlocks Home.xml load: a JobWorker in Release()/restart
    // waits for this lock, the app thread waits for that job, and this
    // script waits for the GUI.
    lckdl.unlock();
    m_invoker->Execute(script, args);
    lckdl.lock();
    m_executing = false;

    // The state here is this run's own outcome. A concurrent claimer can only
    // Reset() (to Uninitialized) a thread it has claimed, so a terminal failure
    // is always ours to act on; an Uninitialized seen under someone else's
    // claim is their Reset() and must not retire the thread they will run.
    const InvokerState state = m_invoker->GetState();
    if (state == InvokerStateFailed || state == InvokerStateExecutionDone ||
        (!IsClaimed() && state != InvokerStateScriptDone))
      m_reusable = false;

    m_condition.wait(lckdl,
                     [this] { return m_bStop || m_restart || (!m_reusable && !IsClaimed()); });

    // A claim keeps the loop open, so a stop request does not have to be
    // ignored to protect a script that has just been handed to this thread.
    keepGoing = (m_restart || m_reusable || IsClaimed()) && !m_bStop;
    if (!keepGoing)
      m_processDone = true; // set under m_mutex, before execute() can look again
  }
}

void CLanguageInvokerThread::OnExit()
{
  // OnExit() always runs, including when Process() left by exception, so this
  // is where the thread is guaranteed to be marked finished. Without it a
  // Process() that threw would keep IsProcessDone() false with IsRunning() also
  // false, which IsBusy() reports as busy for the rest of the session.
  {
    std::unique_lock<std::mutex> lck(m_mutex);
    m_processDone = true;
    m_condition.notify_one();
  }

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
