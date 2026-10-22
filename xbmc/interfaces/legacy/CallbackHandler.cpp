/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "CallbackHandler.h"

#include "AddonUtils.h"
#include "commons/Exception.h"
#include "utils/log.h"

#include <algorithm>
#include <deque>
#include <map>
#include <mutex>

namespace XBMCAddon
{
  class AsyncCallbackMessage : public AddonClass
  {
  public:
    AddonClass::Ref<Callback> cb;
    AddonClass::Ref<RetardedAsyncCallbackHandler> handler;
    AsyncCallbackMessage(Callback* _cb, RetardedAsyncCallbackHandler* _handler) :
      cb(_cb), handler(_handler) { XBMC_TRACE; }
  };

  //********************************************************************
  // This holds the callback messages which will be executed, one queue per
  //  thread that will execute them (keyed by the handler's queueKey). A pump
  //  takes the messages of its own queue, oldest first, and never looks at
  //  another thread's: a thread that does not pump leaves its own messages
  //  waiting and costs every other thread nothing. It doesn't seem to work
  //  correctly with the Ref object so we'll go with Ref*'s
  //********************************************************************
  typedef std::deque<AddonClass::Ref<AsyncCallbackMessage>> CallbackQueue;
  typedef std::map<const void*, CallbackQueue> CallbackQueues;
  //********************************************************************

  static CCriticalSection critSection;
  static CallbackQueues g_callQueues;

  RetardedAsyncCallbackHandler::RetardedAsyncCallbackHandler(const void* queueKey)
    : m_queueKey(queueKey)
  {
    XBMC_TRACE;
  }

  void RetardedAsyncCallbackHandler::invokeCallback(Callback* cb)
  {
    XBMC_TRACE;
    std::unique_lock lock(critSection);
    g_callQueues[m_queueKey].emplace_back(new AsyncCallbackMessage(cb, this));
  }

  RetardedAsyncCallbackHandler::~RetardedAsyncCallbackHandler()
  {
    XBMC_TRACE;
    // A queued message holds a reference to its handler, so no message of
    //  this handler is in a queue when it is destroyed. A pump pops its
    //  message before it releases it, so from makePendingCalls this runs
    //  with the message already gone. clearPendingCalls' erase-remove
    //  overwrites a removed message while it is still an element, and the
    //  queues' own destruction at exit destroys one in place: from either,
    //  the queues must not be touched from inside this.
  }

  void RetardedAsyncCallbackHandler::makePendingCalls(const void* queueKey)
  {
    XBMC_TRACE;
    std::unique_lock lock(critSection);
    CallbackQueues::iterator queue = g_callQueues.find(queueKey);
    while (queue != g_callQueues.end() && !queue->second.empty())
    {
      {
        // remove it from the queue. No matter what we're done with
        //  this. Even if it doesn't execute for some reason.
        AddonClass::Ref<AsyncCallbackMessage> p(queue->second.front());
        queue->second.pop_front();

        // we need to release the critSection lock prior to grabbing the
        //  lock on the object. Not doing so results in deadlocks. We no
        //  longer are accessing the queue so it's fine to do this now
        XBMCAddonUtils::InvertSingleLockGuard unlock(lock);

        // the queue is this thread's, but the object may have been
        //  unregistered since the message was queued (its interpreter
        //  ending, the object deallocating): then the message is dropped,
        //  as clearPendingCalls would have dropped it.
        if (p->handler->isStateOk(p->cb->getObject()))
        {
          // make sure the object is not deallocating

          // we need to grab the object lock to see if the object of the call
          //  is deallocating. holding this lock should prevent it from
          //  deallocating during the execution of this call.
#ifdef ENABLE_XBMC_TRACE_API
          CLog::Log(LOGDEBUG, "{}NEWADDON executing callback 0x{:x}", _tg.getSpaces(),
                    (long)(p->cb.get()));
#endif
          AddonClass* obj = (p->cb->getObject());
          AddonClass::Ref<AddonClass> ref(obj);
          std::unique_lock lock2(*obj);
          if (!p->cb->getObject()->isDeallocating())
          {
            try
            {
              // need to make the call
              p->cb->executeCallback();
            }
            catch (XbmcCommons::Exception& e) { e.LogThrowMessage(); }
            catch (...)
            {
              CLog::Log(LOGERROR, "Unknown exception while executing callback {:#x}",
                        reinterpret_cast<int64_t>(p->cb.get()));
            }
          }
        }
        // the lock is taken back here, and then the message is released:
        //  that may be the last reference to its callback and handler, so
        //  it happens before the queue is looked up again below, with no
        //  iterator held across it.
      }

      // the queue may have been removed and re-created by another thread
      //  while the lock was released, so it is looked up again rather than
      //  reached through the old iterator
      queue = g_callQueues.find(queueKey);
    }
    if (queue != g_callQueues.end() && queue->second.empty())
      g_callQueues.erase(queue);
  }

  void RetardedAsyncCallbackHandler::clearPendingCalls(void* userData)
  {
    XBMC_TRACE;
    std::unique_lock lock(critSection);
    CallbackQueues::iterator queue = g_callQueues.begin();
    while (queue != g_callQueues.end())
    {
      CallbackQueue& messages = queue->second;
      messages.erase(std::remove_if(messages.begin(), messages.end(),
                                    [userData](const AddonClass::Ref<AsyncCallbackMessage>& p)
                                    {
                                      if (!p->handler->shouldRemoveCallback(p->cb->getObject(),
                                                                            userData))
                                        return false;
#ifdef ENABLE_XBMC_TRACE_API
                                      CLog::Log(LOGDEBUG,
                                                "NEWADDON removing callback 0x{:x} for "
                                                "PyThreadState 0x{:x} from queue",
                                                (long)(p->cb.get()), (long)userData);
#endif
                                      return true;
                                    }),
                     messages.end());
      if (messages.empty())
        queue = g_callQueues.erase(queue);
      else
        ++queue;
    }
  }
}
