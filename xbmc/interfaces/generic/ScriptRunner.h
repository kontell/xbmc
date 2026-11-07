/*
 *  Copyright (C) 2017-2021 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "URL.h"
#include "addons/IAddon.h"
#include "threads/Event.h"

#include <chrono>
#include <string>

class CReusableInvokerClaim;

class CScriptRunner
{
protected:
  CScriptRunner();
  virtual ~CScriptRunner() = default;

  virtual bool IsSuccessful() const = 0;
  virtual bool IsCancelled() const = 0;

  ADDON::AddonPtr GetAddon() const;

  /*!
   * \brief Run the add-on script for the given URL and wait for its result.
   *
   * \param claim (Optional) The caller's claim on the reusable invoker, held for
   *        the whole run. See CScriptInvocationManager::ClaimReusableInvoker.
   * \param quietTime How long the caller can wait before the GUI shows that it is waiting.
   *        On the GUI thread the wait is under the busy dialog; for this long first the GUI
   *        runs on as it is, with no dialog. Nothing on another thread.
   */
  bool RunScript(const ADDON::AddonPtr& addon,
                 const CURL& url,
                 int handle,
                 bool resume,
                 CReusableInvokerClaim* claim = nullptr,
                 std::chrono::milliseconds quietTime = {});

  void SetDone();

  static int ExecuteScript(const ADDON::AddonPtr& addon, const CURL& url, bool resume);
  static int ExecuteScript(const ADDON::AddonPtr& addon,
                           CURL url,
                           int handle,
                           bool resume,
                           CReusableInvokerClaim* claim = nullptr);

private:
  bool RunScriptInternal(const ADDON::AddonPtr& addon,
                         const CURL& url,
                         int handle,
                         bool resume,
                         bool wait = true,
                         CReusableInvokerClaim* claim = nullptr,
                         std::chrono::milliseconds quietTime = {});
  bool WaitOnScriptResult(int scriptId,
                          const std::string& path,
                          const std::string& name,
                          std::chrono::milliseconds quietTime);

  ADDON::AddonPtr m_addon;

  CEvent m_scriptDone;
};
