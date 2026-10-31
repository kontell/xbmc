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

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class CAction;
class CFileItemList;

/*!
 * \brief Scrub through the playing video by its preview thumbnails: a strip of them, one per
 * interval, that a press moves by one and a held key moves faster the longer it is held.
 * Select seeks to the one in the middle.
 */
class CGUIDialogVideoScrub : public CGUIDialog
{
public:
  CGUIDialogVideoScrub();
  ~CGUIDialogVideoScrub() override;

  bool OnMessage(CGUIMessage& message) override;
  bool OnAction(const CAction& action) override;

protected:
  void Process(unsigned int currentTime, CDirtyRegionList& dirtyregions) override;
  void OnDeinitWindow(int nextWindowID) override;

private:
  void FillStrip();
  int LastIndex() const;
  void Step(int thumbnails);
  void Move(const CAction& action, int direction);
  void SetIndex(int index);
  void Seek(int index);
  void Prefetch(int tile);
  void ReleasePrefetched();

  ScrubPreviews m_previews;
  std::unique_ptr<CFileItemList> m_items; //!< one per thumbnail, bound to the strip
  int64_t m_totalMs{0};
  int64_t m_startMs{0};
  int m_index{-1};
  int m_tile{-1};
  double m_target{0.0}; // where the input has asked for, in thumbnails
  double m_position{0.0}; // where the strip is on its way there
  double m_rate{0.0}; // thumbnails a second a held key moves the target, signed; 0 when none is
  unsigned int m_lastRepeat{0};
  unsigned int m_lastFrame{0};
  std::vector<std::string> m_prefetched;
};
