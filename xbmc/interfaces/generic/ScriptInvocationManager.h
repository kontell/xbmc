/*
 *  Copyright (C) 2013-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "addons/IAddon.h"
#include "interfaces/generic/ILanguageInvoker.h"
#include "threads/CriticalSection.h"

#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

class CLanguageInvokerThread;
class CScriptInvocationManager;

/*!
 * \brief RAII lease on the reusable language invoker slot.
 *
 * A claim keeps CScriptInvocationManager from handing the reusable invoker to
 * another script. ClaimReusableInvoker fills it with a parked interpreter and
 * the plugin handle that interpreter was created for. A claim that holds
 * nothing is attached by ExecuteAsync to the interpreter the caller's run
 * publishes, so the run that creates a reusable interpreter holds it the same
 * way a run that reuses one does. Dropping the claim - on scope exit, an early
 * return, or an exception - is what puts the interpreter back in circulation,
 * so a caller cannot leak the leased state by forgetting to clean up.
 */
class CReusableInvokerClaim
{
public:
  CReusableInvokerClaim() = default;
  ~CReusableInvokerClaim() { Release(); }

  CReusableInvokerClaim(CReusableInvokerClaim&& other) noexcept { *this = std::move(other); }
  CReusableInvokerClaim& operator=(CReusableInvokerClaim&& other) noexcept;

  CReusableInvokerClaim(const CReusableInvokerClaim&) = delete;
  CReusableInvokerClaim& operator=(const CReusableInvokerClaim&) = delete;

  //! \brief True while the claim holds an interpreter.
  bool IsValid() const { return m_thread != nullptr; }

  //! \brief The plugin handle the held interpreter serves, or -1.
  int GetPluginHandle() const { return m_pluginHandle; }

  //! \brief Drop the claim. Safe to call on an empty claim and more than once.
  void Release();

private:
  friend class CScriptInvocationManager;
  CReusableInvokerClaim(std::shared_ptr<CLanguageInvokerThread> thread, int pluginHandle)
    : m_thread(std::move(thread)),
      m_pluginHandle(pluginHandle)
  {
  }

  std::shared_ptr<CLanguageInvokerThread> m_thread;
  int m_pluginHandle{-1};
};

class CScriptInvocationManager
{
public:
  static CScriptInvocationManager& GetInstance();

  void Process();
  void Uninitialize();

  void RegisterLanguageInvocationHandler(ILanguageInvocationHandler *invocationHandler, const std::string &extension);
  void RegisterLanguageInvocationHandler(ILanguageInvocationHandler *invocationHandler, const std::set<std::string> &extensions);
  void UnregisterLanguageInvocationHandler(ILanguageInvocationHandler *invocationHandler);
  bool HasLanguageInvoker(const std::string& script) const;

  /*!
   * \brief Returns the invoker to run the given script with.
   *
   * \param script Path to the script to be executed
   * \param claim (Optional) The caller's claim on the reusable invoker. One that
   *        holds an interpreter resolves to that interpreter's invoker.
   * \return The invoker to use, or a new one if nothing was claimed
   */
  std::shared_ptr<ILanguageInvoker> GetLanguageInvoker(
      const std::string& script, const CReusableInvokerClaim* claim = nullptr);

  /*!
   * \brief Claim the reusable invoker for the given script, if there is one.
   *
   * A valid claim gives the caller exclusive use of a parked interpreter and the
   * plugin handle it was created for. Hold it for as long as the script runs and
   * pass it to ExecuteAsync; destroying it hands the invoker back. An empty claim
   * means nothing was parked; passed to ExecuteAsync, it is attached to the
   * interpreter that run publishes, if it publishes one.
   *
   * \param script Path to the script about to be executed
   * \return The claim, empty if nothing reusable was available
   */
  CReusableInvokerClaim ClaimReusableInvoker(const std::string& script);

  /*!
   * \brief Executes the given script asynchronously in a separate thread.
   *
   * \param script Path to the script to be executed
   * \param addon (Optional) Addon to which the script belongs
   * \param arguments (Optional) List of arguments passed to the script
   * \param reuseable (Optional) Whether the script's interpreter may be parked
   *        for reuse afterwards
   * \param pluginHandle (Optional) The plugin handle the script runs with
   * \param claim (Optional) The caller's claim on the reusable invoker, see
   *        ClaimReusableInvoker. Without one the call takes its own, held for
   *        the hand-off.
   * \return -1 if an error occurred, otherwise the ID of the script
   */
  int ExecuteAsync(const std::string& script,
                   const ADDON::AddonPtr& addon = ADDON::AddonPtr(),
                   const std::vector<std::string>& arguments = std::vector<std::string>(),
                   bool reuseable = false,
                   int pluginHandle = -1,
                   CReusableInvokerClaim* claim = nullptr);
  /*!
  * \brief Executes the given script asynchronously in a separate thread.
  *
  * \param script Path to the script to be executed
  * \param languageInvoker Language invoker to be used to execute the script
  * \param addon (Optional) Addon to which the script belongs
  * \param arguments (Optional) List of arguments passed to the script
  * \param reuseable (Optional) Whether the script's interpreter may be parked
  *        for reuse afterwards
  * \param pluginHandle (Optional) The plugin handle the script runs with
  * \param claim (Optional) The claim languageInvoker was resolved from, if any.
  *        An empty claim is attached to the interpreter published for this run.
  * \return -1 if an error occurred, otherwise the ID of the script
  */
  int ExecuteAsync(const std::string& script,
                   const std::shared_ptr<ILanguageInvoker>& languageInvoker,
                   const ADDON::AddonPtr& addon = ADDON::AddonPtr(),
                   const std::vector<std::string>& arguments = std::vector<std::string>(),
                   bool reuseable = false,
                   int pluginHandle = -1,
                   CReusableInvokerClaim* claim = nullptr);

  /*!
  * \brief Executes the given script synchronously.
  *
  * \details The script is actually executed asynchronously but the calling
  * thread is blocked until either the script has finished or the given timeout
  * has expired. If the given timeout has expired the script's execution is
  * stopped and depending on the specified wait behaviour we wait for the
  * script's execution to finish or not.
  *
  * \param script Path to the script to be executed
  * \param addon (Optional) Addon to which the script belongs
  * \param arguments (Optional) List of arguments passed to the script
  * \param timeout (Optional) Timeout (in milliseconds) for the script's execution
  * \param waitShutdown (Optional) Whether to wait when having to forcefully stop the script's execution or not.
  * \return -1 if an error occurred, 0 if the script terminated or ETIMEDOUT if the given timeout expired
  */
  int ExecuteSync(const std::string& script,
                  const ADDON::AddonPtr& addon = ADDON::AddonPtr(),
                  const std::vector<std::string>& arguments = std::vector<std::string>(),
                  uint32_t timeoutMs = 0,
                  bool waitShutdown = false);
  /*!
  * \brief Executes the given script synchronously.
  *
  * \details The script is actually executed asynchronously but the calling
  * thread is blocked until either the script has finished or the given timeout
  * has expired. If the given timeout has expired the script's execution is
  * stopped and depending on the specified wait behaviour we wait for the
  * script's execution to finish or not.
  *
  * \param script Path to the script to be executed
  * \param languageInvoker Language invoker to be used to execute the script
  * \param addon (Optional) Addon to which the script belongs
  * \param arguments (Optional) List of arguments passed to the script
  * \param timeout (Optional) Timeout (in milliseconds) for the script's execution
  * \param waitShutdown (Optional) Whether to wait when having to forcefully stop the script's execution or not.
  * \param claim (Optional) The claim languageInvoker was resolved from, if any
  * \return -1 if an error occurred, 0 if the script terminated or ETIMEDOUT if the given timeout expired
  */
  int ExecuteSync(const std::string& script,
                  const std::shared_ptr<ILanguageInvoker>& languageInvoker,
                  const ADDON::AddonPtr& addon = ADDON::AddonPtr(),
                  const std::vector<std::string>& arguments = std::vector<std::string>(),
                  uint32_t timeoutMs = 0,
                  bool waitShutdown = false,
                  CReusableInvokerClaim* claim = nullptr);
  bool Stop(int scriptId, bool wait = false);
  bool Stop(const std::string &scriptPath, bool wait = false);

  /*!
   *\brief Stop all running scripts
   *\param wait if kodi should wait for each script to finish (default false)
  */
  void StopRunningScripts(bool wait = false);

  bool IsRunning(int scriptId) const;
  bool IsRunning(const std::string& scriptPath) const;

protected:
  friend class CLanguageInvokerThread;

  void OnExecutionDone(int scriptId);

private:
  CScriptInvocationManager() = default;
  CScriptInvocationManager(const CScriptInvocationManager&) = delete;
  CScriptInvocationManager const& operator=(CScriptInvocationManager const&) = delete;
  virtual ~CScriptInvocationManager();

  struct LanguageInvokerThread
  {
    std::shared_ptr<CLanguageInvokerThread> thread;
    std::string script;
    bool done;
  };
  using LanguageInvokerThreadMap = std::map<int, LanguageInvokerThread>;
  using LanguageInvocationHandlerMap = std::map<std::string, ILanguageInvocationHandler*>;

  LanguageInvokerThread getInvokerThread(int scriptId) const;
  //! A new invoker from the handler registered for the script's extension.
  //! Caller must hold m_critSection.
  std::shared_ptr<ILanguageInvoker> CreateLanguageInvoker(const std::string& script) const;
  //! True if the last reusable thread is spoken for and keeps the slot (see
  //! CLanguageInvokerThread::IsSpokenFor()). Caller must hold m_critSection.
  bool LastInvokerOccupied() const;

  LanguageInvocationHandlerMap m_invocationHandlers;
  LanguageInvokerThreadMap m_scripts;
  std::shared_ptr<CLanguageInvokerThread> m_lastInvokerThread;
  int m_lastPluginHandle = -1;

  std::map<std::string, int> m_scriptPaths;
  int m_nextId = 0;
  mutable CCriticalSection m_critSection;
};
