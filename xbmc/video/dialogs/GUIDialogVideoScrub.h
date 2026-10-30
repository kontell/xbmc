/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "guilib/GUIDialog.h"
#include "media/ScrubPreviews.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

class CAction;

/*!
 * \brief Scrubs through the playing video by its preview thumbnails. A press moves one
 * thumbnail; a held key moves on at a rate that doubles as the hold goes on; Select seeks to the
 * thumbnail shown, Back leaves the position alone.
 */
class CGUIDialogVideoScrub : public CGUIDialog
{
public:
  CGUIDialogVideoScrub();
  bool OnMessage(CGUIMessage& message) override;
  bool OnAction(const CAction& action) override;

protected:
  void OnDeinitWindow(int nextWindowID) override;

private:
  int LastIndex() const;
  void Step(int thumbnails);
  void Move(const CAction& action, int direction);
  void SetIndex(int index);
  void Prefetch(int tile);
  void ReleasePrefetched();

  ScrubPreviews m_previews;
  int64_t m_totalMs{0};
  int64_t m_startMs{0};
  int m_index{-1};
  int m_tile{-1};
  int m_direction{0};
  double m_pending{0.0}; // thumbnails a hold has earned but not moved yet
  std::chrono::steady_clock::time_point m_lastTick;
  std::vector<std::string> m_prefetched;
};
