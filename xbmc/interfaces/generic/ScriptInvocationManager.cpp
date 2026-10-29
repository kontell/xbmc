/*
 *  Copyright (C) 2013-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "ScriptInvocationManager.h"

#include "interfaces/generic/ILanguageInvocationHandler.h"
#include "interfaces/generic/ILanguageInvoker.h"
#include "interfaces/generic/LanguageInvokerThread.h"
#include "utils/FileUtils.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/XTimeUtils.h"
#include "utils/log.h"

#include <algorithm>
#include <cerrno>
#include <memory>
#include <mutex>
#include <tuple>
#include <utility>
#include <vector>

CReusableInvokerClaim& CReusableInvokerClaim::operator=(CReusableInvokerClaim&& other) noexcept
{
  if (this != &other)
  {
    Release();
    m_thread = std::move(other.m_thread);
    m_pluginHandle = other.m_pluginHandle;
    other.m_thread = nullptr;
    other.m_pluginHandle = -1;
  }
  return *this;
}

void CReusableInvokerClaim::Release()
{
  if (m_thread)
    m_thread->ReleaseClaim();

  m_thread = nullptr;
  m_pluginHandle = -1;
}

CScriptInvocationManager::~CScriptInvocationManager()
{
  Uninitialize();
}

CScriptInvocationManager& CScriptInvocationManager::GetInstance()
{
  static CScriptInvocationManager s_instance;
  return s_instance;
}

void CScriptInvocationManager::Process()
{
  std::unique_lock lock(m_critSection);
  // go through all active threads and find and remove all which are done
  std::erase_if(m_scripts,
                [&paths = m_scriptPaths](const auto& it)
                {
                  const auto& [key, script] = it;
                  if (script.done)
                    paths.erase(script.script);
                  return script.done;
                });

  // we can leave the lock now
  lock.unlock();

  // let the invocation handlers do their processing
  std::ranges::for_each(m_invocationHandlers, [](auto& handler) { handler.second->Process(); });
}

void CScriptInvocationManager::Uninitialize()
{
  std::unique_lock lock(m_critSection);

  // execute Process() once more to handle the remaining scripts
  Process();

  // it is safe to release early, thread must be in m_scripts too
  m_lastInvokerThread = nullptr;
  m_lastPluginHandle = -1;

  // make sure all scripts are done
  std::vector<LanguageInvokerThread> tempList;
  std::ranges::transform(m_scripts, std::back_inserter(tempList),
                         [](const auto& script) { return script.second; });

  m_scripts.clear();
  m_scriptPaths.clear();

  // we can leave the lock now
  lock.unlock();

  // finally stop and remove the finished threads but we do it outside of any
  // locks in case of any callbacks from the stop or destruction logic of
  // CLanguageInvokerThread or the ILanguageInvoker implementation
  std::ranges::for_each(tempList,
                        [](auto& val)
                        {
                          if (!val.done)
                            val.thread->Stop(true);
                        });

  lock.lock();

  tempList.clear();

  // uninitialize all invocation handlers and then remove them
  std::ranges::for_each(m_invocationHandlers,
                        [](auto& handler) { handler.second->Uninitialize(); });

  m_invocationHandlers.clear();
}

void CScriptInvocationManager::RegisterLanguageInvocationHandler(
    ILanguageInvocationHandler* invocationHandler, const std::string& extension)
{
  if (invocationHandler == nullptr || extension.empty())
    return;

  std::string ext = extension;
  StringUtils::ToLower(ext);
  if (!StringUtils::StartsWithNoCase(ext, "."))
    ext = "." + ext;

  std::unique_lock lock(m_critSection);

  const bool known =
      std::ranges::any_of(m_invocationHandlers, [invocationHandler](const auto& handler)
                          { return handler.second == invocationHandler; });

  bool inserted;
  std::tie(std::ignore, inserted) = m_invocationHandlers.emplace(ext, invocationHandler);

  // automatically initialize the invocation handler if it's a new one
  if (inserted && !known)
    invocationHandler->Initialize();
}

void CScriptInvocationManager::RegisterLanguageInvocationHandler(
    ILanguageInvocationHandler* invocationHandler, const std::set<std::string>& extensions)
{
  if (invocationHandler == nullptr || extensions.empty())
    return;

  std::ranges::for_each(extensions, [&invocationHandler, this](const auto& extension)
                        { RegisterLanguageInvocationHandler(invocationHandler, extension); });
}

void CScriptInvocationManager::UnregisterLanguageInvocationHandler(ILanguageInvocationHandler *invocationHandler)
{
  if (invocationHandler == nullptr)
    return;

  std::unique_lock lock(m_critSection);
  //  get all extensions of the given language invoker
  std::erase_if(m_invocationHandlers, [&invocationHandler](const auto& handler)
                { return handler.second == invocationHandler; });

  // automatically uninitialize the invocation handler
  invocationHandler->Uninitialize();
}

bool CScriptInvocationManager::HasLanguageInvoker(const std::string &script) const
{
  std::string extension = URIUtils::GetExtension(script);
  StringUtils::ToLower(extension);

  std::unique_lock lock(m_critSection);
  const auto it = m_invocationHandlers.find(extension);
  return it != m_invocationHandlers.end() && it->second != nullptr;
}

CReusableInvokerClaim CScriptInvocationManager::ClaimReusableInvoker(const std::string& script)
{
  std::unique_lock lock(m_critSection);

  if (m_lastInvokerThread && m_lastInvokerThread->Reuseable(script) &&
      m_lastInvokerThread->Claim())
  {
    m_lastInvokerThread->GetInvoker()->Reset();
    CLog::Log(LOGDEBUG, "{} - claimed reusable invoker (plugin handle {}) on LanguageInvokerThread {}",
              __FUNCTION__, m_lastPluginHandle, m_lastInvokerThread->GetId());
    return {m_lastInvokerThread, m_lastPluginHandle};
  }
  return {};
}

std::shared_ptr<ILanguageInvoker> CScriptInvocationManager::GetLanguageInvoker(
    const std::string& script, const CReusableInvokerClaim* claim /* = nullptr */)
{
  // The claim was granted for this script and keeps the interpreter from being
  // handed to anyone else; a stop that lands between here and the hand-off is
  // caught when the thread refuses the script.
  if (claim != nullptr && claim->m_thread)
    return claim->m_thread->GetInvoker();

  std::unique_lock lock(m_critSection);
  return CreateLanguageInvoker(script);
}

std::shared_ptr<ILanguageInvoker> CScriptInvocationManager::CreateLanguageInvoker(
    const std::string& script) const
{
  std::string extension = URIUtils::GetExtension(script);
  StringUtils::ToLower(extension);

  const auto it = m_invocationHandlers.find(extension);
  if (it != m_invocationHandlers.end() && it->second != nullptr)
    return it->second->CreateInvoker();

  return {};
}

int CScriptInvocationManager::ExecuteAsync(
    const std::string& script,
    const ADDON::AddonPtr& addon /* = ADDON::AddonPtr() */,
    const std::vector<std::string>& arguments /* = std::vector<std::string>() */,
    bool reuseable /* = false */,
    int pluginHandle /* = -1 */,
    CReusableInvokerClaim* claim /* = nullptr */)
{
  if (script.empty())
    return -1;

  if (!CFileUtils::Exists(script, false))
  {
    CLog::Log(LOGERROR, "{} - Not executing non-existing script {}", __FUNCTION__, script);
    return -1;
  }

  // A caller with no claim of its own (a handle-less run, a one-shot) still
  // reuses a parked interpreter of its script; its lease lasts for the hand-off.
  CReusableInvokerClaim ownClaim;
  if (claim == nullptr)
  {
    ownClaim = ClaimReusableInvoker(script);
    claim = &ownClaim;
  }

  auto invoker = GetLanguageInvoker(script, claim);
  return ExecuteAsync(script, invoker, addon, arguments, reuseable, pluginHandle, claim);
}

int CScriptInvocationManager::ExecuteAsync(
    const std::string& script,
    const std::shared_ptr<ILanguageInvoker>& languageInvoker,
    const ADDON::AddonPtr& addon /* = ADDON::AddonPtr() */,
    const std::vector<std::string>& arguments /* = std::vector<std::string>() */,
    bool reuseable /* = false */,
    int pluginHandle /* = -1 */,
    CReusableInvokerClaim* claim /* = nullptr */)
{
  if (script.empty() || languageInvoker == nullptr)
    return -1;

  if (!CFileUtils::Exists(script, false))
  {
    CLog::Log(LOGERROR, "{} - Not executing non-existing script {}", __FUNCTION__, script);
    return -1;
  }

  std::unique_lock lock(m_critSection);

  auto invoker = languageInvoker;
  if (claim != nullptr && claim->m_thread && claim->m_thread->GetInvoker() == invoker)
  {
    auto invokerThread = claim->m_thread;
    if (addon != nullptr)
      invokerThread->SetAddon(addon);

    lock.unlock();
    // The claim keeps the thread spoken for across the hand-off.
    if (invokerThread->Execute(script, arguments))
      return invokerThread->GetId();

    // The thread was stopped after it was claimed. Let go of it, so the new
    // interpreter can take the slot under the same claim.
    CLog::Log(LOGDEBUG, "{} - LanguageInvokerThread {} refused {}, it is stopping; using a new one",
              __FUNCTION__, invokerThread->GetId(), script);
    lock.lock();
    invoker = CreateLanguageInvoker(script);
    if (invoker == nullptr)
      return -1;
    claim->Release();
  }

  const bool lastOccupied = LastInvokerOccupied();
  auto invokerThread =
      std::make_shared<CLanguageInvokerThread>(invoker, this, reuseable && !lastOccupied);

  if (addon != nullptr)
    invokerThread->SetAddon(addon);

  const int scriptId = m_nextId++;
  invokerThread->SetId(scriptId);

  LanguageInvokerThread thread = {invokerThread, script, false};
  m_scripts.insert(std::make_pair(scriptId, thread));
  m_scriptPaths.insert(std::make_pair(script, scriptId));

  // Take the slot unless the last thread is spoken for; one that is still
  // running finishes its script after Release() and then exits. Only park a
  // thread there that may be reused (a service add-on, say, runs until shutdown
  // and would otherwise pin the slot for the session).
  if (!lastOccupied && reuseable)
  {
    if (m_lastInvokerThread)
      m_lastInvokerThread->Release();
    m_lastInvokerThread = invokerThread;
    m_lastPluginHandle = pluginHandle;
    // The caller's claim covers the new interpreter from here. A plugin handle
    // lives in s_scriptHandles until its waiter removes it, and the listing
    // holds its claim until then, so a concurrent listing cannot claim this
    // interpreter and reuse the same handle before the run that created it is
    // done, which would let that run's RemoveScriptHandle erase the other's.
    if (claim != nullptr)
    {
      invokerThread->Claim();
      *claim = CReusableInvokerClaim{invokerThread, pluginHandle};
    }
  }

  lock.unlock();
  if (invokerThread->Execute(script, arguments))
    return scriptId;

  // Never started, so OnExit() will not mark it done.
  lock.lock();
  if (claim != nullptr && claim->m_thread == invokerThread)
    claim->Release();
  m_scripts.erase(scriptId);
  if (const auto it = m_scriptPaths.find(script);
      it != m_scriptPaths.end() && it->second == scriptId)
    m_scriptPaths.erase(it);
  if (m_lastInvokerThread == invokerThread)
  {
    m_lastInvokerThread = nullptr;
    m_lastPluginHandle = -1;
  }
  return -1;
}

int CScriptInvocationManager::ExecuteSync(
    const std::string& script,
    const ADDON::AddonPtr& addon /* = ADDON::AddonPtr() */,
    const std::vector<std::string>& arguments /* = std::vector<std::string>() */,
    uint32_t timeoutMs /* = 0 */,
    bool waitShutdown /* = false */)
{
  if (script.empty())
    return -1;

  if (!CFileUtils::Exists(script, false))
  {
    CLog::Log(LOGERROR, "{} - Not executing non-existing script {}", __FUNCTION__, script);
    return -1;
  }

  CReusableInvokerClaim claim = ClaimReusableInvoker(script);
  auto invoker = GetLanguageInvoker(script, &claim);
  return ExecuteSync(script, invoker, addon, arguments, timeoutMs, waitShutdown, &claim);
}

int CScriptInvocationManager::ExecuteSync(
    const std::string& script,
    const std::shared_ptr<ILanguageInvoker>& languageInvoker,
    const ADDON::AddonPtr& addon /* = ADDON::AddonPtr() */,
    const std::vector<std::string>& arguments /* = std::vector<std::string>() */,
    uint32_t timeoutMs /* = 0 */,
    bool waitShutdown /* = false */,
    CReusableInvokerClaim* claim /* = nullptr */)
{
  int scriptId = ExecuteAsync(script, languageInvoker, addon, arguments, false, -1, claim);
  if (scriptId < 0)
    return -1;

  bool timeout = timeoutMs > 0;
  while ((!timeout || timeoutMs > 0) && IsRunning(scriptId))
  {
    unsigned int sleepMs = 100U;
    if (timeout && timeoutMs < sleepMs)
      sleepMs = timeoutMs;

    KODI::TIME::Sleep(std::chrono::milliseconds(sleepMs));

    if (timeout)
      timeoutMs -= sleepMs;
  }

  if (IsRunning(scriptId))
  {
    Stop(scriptId, waitShutdown);
    return ETIMEDOUT;
  }

  return 0;
}

bool CScriptInvocationManager::Stop(int scriptId, bool wait /* = false */)
{
  if (scriptId < 0)
    return false;

  std::unique_lock lock(m_critSection);
  auto invokerThread = getInvokerThread(scriptId).thread;
  if (invokerThread == nullptr)
    return false;

  return invokerThread->Stop(wait);
}

void CScriptInvocationManager::StopRunningScripts(bool wait /* = false */)
{
  std::ranges::for_each(m_scripts,
                        [wait, this](auto& script)
                        {
                          if (!script.second.done)
                            Stop(script.second.script, wait);
                        });
}

void CScriptInvocationManager::NotifyAllScriptsAborting()
{
  std::unique_lock lock(m_critSection);
  for (const auto& handler : m_invocationHandlers)
    handler.second->NotifyScriptAborting(nullptr);
}

bool CScriptInvocationManager::Stop(const std::string &scriptPath, bool wait /* = false */)
{
  if (scriptPath.empty())
    return false;

  std::unique_lock lock(m_critSection);
  const auto script = m_scriptPaths.find(scriptPath);
  if (script == m_scriptPaths.end())
    return false;

  return Stop(script->second, wait);
}

bool CScriptInvocationManager::IsRunning(int scriptId) const
{
  std::unique_lock lock(m_critSection);
  LanguageInvokerThread invokerThread = getInvokerThread(scriptId);
  if (invokerThread.thread == nullptr)
    return false;

  return !invokerThread.done;
}

bool CScriptInvocationManager::IsRunning(const std::string& scriptPath) const
{
  std::unique_lock lock(m_critSection);
  const auto it = m_scriptPaths.find(scriptPath);
  if (it == m_scriptPaths.end())
    return false;

  return IsRunning(it->second);
}

void CScriptInvocationManager::OnExecutionDone(int scriptId)
{
  if (scriptId < 0)
    return;

  std::unique_lock lock(m_critSection);
  const auto script = m_scripts.find(scriptId);
  if (script != m_scripts.end())
    script->second.done = true;
}

bool CScriptInvocationManager::LastInvokerOccupied() const
{
  return m_lastInvokerThread && m_lastInvokerThread->IsSpokenFor();
}

CScriptInvocationManager::LanguageInvokerThread CScriptInvocationManager::getInvokerThread(int scriptId) const
{
  if (scriptId < 0)
    return LanguageInvokerThread();

  const auto script = m_scripts.find(scriptId);
  if (script == m_scripts.end())
    return LanguageInvokerThread();

  return script->second;
}
