/*
 *  Copyright (C) 2013-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "LanguageInvokerThread.h"

#include "ScriptInvocationManager.h"
#include "commons/Exception.h"
#include "utils/log.h"

#include <exception>
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
  // No wait predicate reads m_claimed, so dropping it needs no notify.
  m_claimed = false;
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
    const std::string script = m_script;
    const std::vector<std::string> args = m_args;
    // Execute() talks to the GUI and to other invokers. Holding m_mutex
    // across it deadlocks Home.xml load: a JobWorker in Release()/restart
    // waits for this lock, the app thread waits for that job, and this
    // script waits for the GUI.
    lckdl.unlock();
    bool started = false;
    bool threw = false;
    try
    {
      started = m_invoker->Execute(script, args);
    }
    catch (const XbmcCommons::Exception& e)
    {
      e.LogThrowMessage("Execute");
      threw = true;
    }
    catch (const std::exception& e)
    {
      CLog::Log(LOGERROR, "{} - exception out of {}: {}", __FUNCTION__, script, e.what());
      threw = true;
    }
    catch (...)
    {
      CLog::Log(LOGERROR, "{} - exception out of {}", __FUNCTION__, script);
      threw = true;
    }
    lckdl.lock();
    if (threw)
    {
      // Retire the thread without unwinding further, so OnExit() runs as on
      // every other exit; the invoker only has to leave its state fit for it.
      m_processDone = true;
      lckdl.unlock();
      m_invoker->onExecutionFailed();
      return;
    }

    // The state here is this run's own outcome. A concurrent claimer can only
    // Reset() (to Uninitialized) a thread it has claimed, so a terminal failure
    // is always ours to act on; an Uninitialized seen under someone else's
    // claim is their Reset() and must not retire the thread they will run. A
    // run that never started leaves the same Uninitialized under our own
    // claim, which only Execute()'s result tells apart.
    const InvokerState state = m_invoker->GetState();
    if (!started || state == InvokerStateFailed || state == InvokerStateExecutionDone ||
        (!IsClaimed() && state != InvokerStateScriptDone))
      m_reusable = false;

    // Retire a run that is not reusable even while it is claimed: the claim's
    // holder may be waiting for this thread to exit before it lets go.
    m_condition.wait(lckdl, [this] { return m_bStop || m_restart || !m_reusable; });

    keepGoing = (m_restart || m_reusable) && !m_bStop;
    if (!keepGoing)
      m_processDone = true; // set under m_mutex, before execute() can look again
  }
}

void CLanguageInvokerThread::OnExit()
{
  if (m_invoker == NULL)
    return;

  m_invoker->onExecutionDone();
  m_invocationManager->OnExecutionDone(GetId());
}
