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
 * \brief RAII lease on the reusable language invoker slot and its plugin handle.
 *
 * A claim keeps CScriptInvocationManager from handing the reusable invoker to
 * another script or releasing it as idle. It takes one of two forms:
 *  - a reuse claim, which carries a parked interpreter and the plugin handle it
 *    was created for, handed out by ClaimReusableInvoker; and
 *  - a reservation, which carries no interpreter yet but holds the slot for the
 *    handle a fresh run is about to publish, handed out by ReserveReusableInvoker.
 * Either way, dropping the claim - on scope exit, an early return, or an
 * exception - is what puts the slot back in circulation, so a caller cannot
 * leak the leased state by forgetting to clean up.
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

  //! \brief True if a parked interpreter was claimed for reuse.
  bool IsValid() const { return m_thread != nullptr; }

  //! \brief The claimed plugin handle, or -1 if nothing was claimed.
  int GetPluginHandle() const { return m_pluginHandle; }

  //! \brief Drop the claim. Safe to call on an empty claim and more than once.
  void Release();

private:
  friend class CScriptInvocationManager;
  //! Reuse form: carries the parked interpreter and its handle.
  CReusableInvokerClaim(std::shared_ptr<CLanguageInvokerThread> thread, int pluginHandle)
    : m_thread(std::move(thread)),
      m_pluginHandle(pluginHandle)
  {
  }
  //! Reservation form: holds the slot for a handle a fresh run will publish.
  CReusableInvokerClaim(CScriptInvocationManager* manager, int pluginHandle)
    : m_manager(manager),
      m_pluginHandle(pluginHandle)
  {
  }

  std::shared_ptr<CLanguageInvokerThread> m_thread;
  CScriptInvocationManager* m_manager{nullptr};
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
   * \param pluginHandle The plugin handle the script will run with, or -1 if it
   *        is not a plugin invocation. A handle matching an outstanding claim
   *        (see ClaimReusableInvoker) resolves to that claim's invoker.
   * \return The invoker to use, or a new one if nothing reusable is available
   */
  std::shared_ptr<ILanguageInvoker> GetLanguageInvoker(const std::string& script,
                                                       int pluginHandle = -1);

  /*!
   * \brief Claim the reusable invoker for the given script, if there is one.
   *
   * A valid claim gives the caller exclusive use of a parked interpreter and the
   * plugin handle it was created for. Hold it for as long as the script runs;
   * destroying it hands the invoker back. An invalid claim means the caller has
   * to allocate its own handle and will get a fresh invoker.
   *
   * \param script Path to the script about to be executed
   * \return The claim, which is invalid if nothing reusable was available
   */
  CReusableInvokerClaim ClaimReusableInvoker(const std::string& script);

  /*!
   * \brief Reserve the reusable slot for a handle a fresh run is about to publish.
   *
   * Used when ClaimReusableInvoker found nothing to reuse and the caller has
   * allocated a new plugin handle. The returned claim holds no interpreter yet;
   * when a reusable run publishes an invoker for this handle it is born claimed,
   * and destroying this claim - after the caller has removed the handle - is
   * what releases it. This keeps a concurrent listing from reusing the handle
   * before the run that created it has finished with it.
   *
   * \param pluginHandle The handle the caller is about to run with
   * \return The reservation claim
   */
  CReusableInvokerClaim ReserveReusableInvoker(int pluginHandle);

  /*!
   * \brief Executes the given script asynchronously in a separate thread.
   *
   * \param script Path to the script to be executed
   * \param addon (Optional) Addon to which the script belongs
   * \param arguments (Optional) List of arguments passed to the script
   * \return -1 if an error occurred, otherwise the ID of the script
   */
  int ExecuteAsync(const std::string& script,
                   const ADDON::AddonPtr& addon = ADDON::AddonPtr(),
                   const std::vector<std::string>& arguments = std::vector<std::string>(),
                   bool reuseable = false,
                   int pluginHandle = -1);
  /*!
  * \brief Executes the given script asynchronously in a separate thread.
  *
  * \param script Path to the script to be executed
  * \param languageInvoker Language invoker to be used to execute the script
  * \param addon (Optional) Addon to which the script belongs
  * \param arguments (Optional) List of arguments passed to the script
  * \return -1 if an error occurred, otherwise the ID of the script
  */
  int ExecuteAsync(const std::string& script,
                   const std::shared_ptr<ILanguageInvoker>& languageInvoker,
                   const ADDON::AddonPtr& addon = ADDON::AddonPtr(),
                   const std::vector<std::string>& arguments = std::vector<std::string>(),
                   bool reuseable = false,
                   int pluginHandle = -1);

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
  * \return -1 if an error occurred, 0 if the script terminated or ETIMEDOUT if the given timeout expired
  */
  int ExecuteSync(const std::string& script,
                  const std::shared_ptr<ILanguageInvoker>& languageInvoker,
                  const ADDON::AddonPtr& addon = ADDON::AddonPtr(),
                  const std::vector<std::string>& arguments = std::vector<std::string>(),
                  uint32_t timeoutMs = 0,
                  bool waitShutdown = false);
  bool Stop(int scriptId, bool wait = false);
  bool Stop(const std::string &scriptPath, bool wait = false);

  /*!
   *\brief Stop all running scripts
   *\param wait if kodi should wait for each script to finish (default false)
  */
  void StopRunningScripts(bool wait = false);

  /*!
   *\brief Tell all running scripts that they are about to be stopped
   *\note Stopping a script waits until it has finished. Notifying all
   *      scripts beforehand lets them shut down in parallel.
   */
  void NotifyAllScriptsAborting();

  bool IsRunning(int scriptId) const;
  bool IsRunning(const std::string& scriptPath) const;

protected:
  friend class CLanguageInvokerThread;
  friend class CReusableInvokerClaim;

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
  void ReleaseLastInvokerIfIdle();
  //! Drop a reservation taken by ReserveReusableInvoker: release the claim on
  //! the reusable slot if it still holds the reserved handle. Called by
  //! CReusableInvokerClaim::Release().
  void ReleaseReservedInvoker(int pluginHandle);
  //! True if the last reusable thread is claimed, has a script queued, or is
  //! running one. Caller must hold m_critSection.
  bool LastInvokerOccupied() const;

  LanguageInvocationHandlerMap m_invocationHandlers;
  LanguageInvokerThreadMap m_scripts;
  std::shared_ptr<CLanguageInvokerThread> m_lastInvokerThread;
  int m_lastPluginHandle = -1;

  std::map<std::string, int> m_scriptPaths;
  int m_nextId = 0;
  mutable CCriticalSection m_critSection;
};
