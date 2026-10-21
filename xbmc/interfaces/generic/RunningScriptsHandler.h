/*
 *  Copyright (C) 2017-2021 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "interfaces/generic/ScriptInvocationManager.h"
#include "interfaces/generic/ScriptRunner.h"
#include "threads/CriticalSection.h"
#include "threads/Event.h"

#include <cstdint>
#include <map>
#include <mutex>

template<class TScript>
class CRunningScriptsHandler : protected CScriptRunner
{
protected:
  using HandleType = int;

  CRunningScriptsHandler() = default;
  virtual ~CRunningScriptsHandler() = default;

  using CScriptRunner::ExecuteScript;
  using CScriptRunner::GetAddon;
  using CScriptRunner::SetDone;

  bool RunScript(TScript* script, const ADDON::AddonPtr& addon, const CURL& url, bool resume)
  {
    if (script == nullptr || addon == nullptr)
      return false;

    // Hold the reusable interpreter for as long as this handle is live: a parked
    // one claimed here, or the one this run publishes, which ExecuteAsync
    // attaches to the claim. Destroying the claim - here or on any early return
    // - is what hands it back, so a concurrent listing cannot claim the
    // interpreter and reuse this handle before RemoveScriptHandle() below.
    CReusableInvokerClaim claim =
        CScriptInvocationManager::GetInstance().ClaimReusableInvoker(addon->LibPath());

    // reuse the claimed script handle or get a new one if necessary
    HandleType handle;
    if (claim.IsValid() && claim.GetPluginHandle() >= 0)
    {
      handle = claim.GetPluginHandle();
      ReuseScriptHandle(handle, script);
    }
    else
    {
      // Either nothing was parked, or the interpreter was left by a handle-less
      // run and carries no handle for this listing's callbacks. A new handle
      // serves both: ExecuteAsync binds it to the interpreter the claim holds.
      handle = GetNewScriptHandle(script);
    }

    // run the script
    auto result = CScriptRunner::RunScript(addon, url, handle, resume, &claim);

    // remove the script handle if necessary
    RemoveScriptHandle(handle);

    return result;
  }

  static HandleType GetNewScriptHandle(TScript* script)
  {
    std::unique_lock lock(s_critical);
    uint32_t handle = ++s_scriptHandleCounter;
    s_scriptHandles[handle] = script;

    return handle;
  }

  static void ReuseScriptHandle(HandleType handle, TScript* script)
  {
    std::unique_lock lock(s_critical);
    s_scriptHandles[handle] = script;
  }

  static void RemoveScriptHandle(HandleType handle)
  {
    std::unique_lock lock(s_critical);
    s_scriptHandles.erase(handle);
  }

  static TScript* GetScriptFromHandle(HandleType handle)
  {
    std::unique_lock lock(s_critical);
    auto scriptHandle = s_scriptHandles.find(handle);
    if (scriptHandle == s_scriptHandles.end())
      return nullptr;

    return scriptHandle->second;
  }

  static inline CCriticalSection& GetScriptsLock() { return s_critical; }

private:
  static std::map<HandleType, TScript*> s_scriptHandles;
  static CCriticalSection s_critical;
  static HandleType s_scriptHandleCounter;
};

template<class TScript>
std::map<typename CRunningScriptsHandler<TScript>::HandleType, TScript*>
    CRunningScriptsHandler<TScript>::s_scriptHandles;
template<class TScript>
CCriticalSection CRunningScriptsHandler<TScript>::s_critical;
template<class TScript>
typename CRunningScriptsHandler<TScript>::HandleType
    CRunningScriptsHandler<TScript>::s_scriptHandleCounter = 0;
