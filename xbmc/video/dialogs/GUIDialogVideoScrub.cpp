/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "GUIDialogVideoScrub.h"

#include "FileItem.h"
#include "GUILargeTextureManager.h"
#include "ServiceBroker.h"
#include "application/Application.h"
#include "application/ApplicationComponents.h"
#include "application/ApplicationPlayer.h"
#include "guilib/GUIComponent.h"
#include "guilib/GUIImage.h"
#include "guilib/GUIMessage.h"
#include "guilib/TextureManager.h"
#include "guilib/WindowIDs.h"
#include "input/actions/Action.h"
#include "input/actions/ActionIDs.h"
#include "utils/StringUtils.h"
#include "video/VideoInfoTag.h"

#include <algorithm>
#include <cstdlib>

namespace
{
constexpr int CONTROL_PREVIEW = 10;

// A held key moves BASE_RATE thumbnails a second, doubling every TIER_MS of holding, TIERS times
// over: at a 10 s interval that is 30x, 60x, 120x and 240x real time, so a feature film is
// crossed in about half a minute at the top.
constexpr double BASE_RATE = 3.0;
constexpr unsigned int TIER_MS = 2000;
constexpr int TIERS = 3;
// a gap between two repeats longer than this counts as this long
constexpr double MAX_TICK_SECONDS = 0.5;
} // namespace

CGUIDialogVideoScrub::CGUIDialogVideoScrub()
  : CGUIDialog(WINDOW_DIALOG_VIDEO_SCRUB, "DialogVideoScrub.xml")
{
  m_loadType = LOAD_EVERY_TIME;
}

bool CGUIDialogVideoScrub::OnMessage(CGUIMessage& message)
{
  if (message.GetMessage() == GUI_MSG_WINDOW_INIT)
  {
    const auto& components = CServiceBroker::GetAppComponents();
    const auto appPlayer = components.GetComponent<CApplicationPlayer>();
    const CFileItem& item = g_application.CurrentFileItem();
    if (!appPlayer->IsPlayingVideo() || !item.HasVideoInfoTag() ||
        !item.GetVideoInfoTag()->GetScrubPreviews().HasPreviews())
      return false;

    m_previews = item.GetVideoInfoTag()->GetScrubPreviews();
    m_totalMs = appPlayer->GetTotalTime();
    m_startMs = appPlayer->GetTime();
    m_index = -1;
    m_tile = -1;
    m_direction = 0;
    m_pending = 0.0;

    CGUIDialog::OnMessage(message);
    SetIndex(static_cast<int>(m_startMs / m_previews.intervalMs));
    return true;
  }
  return CGUIDialog::OnMessage(message);
}

void CGUIDialogVideoScrub::OnDeinitWindow(int nextWindowID)
{
  ReleasePrefetched();
  CGUIDialog::OnDeinitWindow(nextWindowID);
}

bool CGUIDialogVideoScrub::OnAction(const CAction& action)
{
  switch (action.GetID())
  {
    case ACTION_MOVE_LEFT:
    case ACTION_STEP_BACK:
      Move(action, -1);
      return true;
    case ACTION_MOVE_RIGHT:
    case ACTION_STEP_FORWARD:
      Move(action, 1);
      return true;
    case ACTION_BIG_STEP_BACK:
      Step(-10);
      return true;
    case ACTION_BIG_STEP_FORWARD:
      Step(10);
      return true;
    case ACTION_MOUSE_WHEEL_DOWN:
      Step(-1);
      return true;
    case ACTION_MOUSE_WHEEL_UP:
      Step(1);
      return true;
    case ACTION_SELECT_ITEM:
    case ACTION_PLAYER_PLAY:
    {
      if (m_index >= 0)
      {
        auto& components = CServiceBroker::GetAppComponents();
        const auto appPlayer = components.GetComponent<CApplicationPlayer>();
        appPlayer->SeekTime(static_cast<int64_t>(m_index) * m_previews.intervalMs);
      }
      Close();
      return true;
    }
    default:
      break;
  }
  return CGUIDialog::OnAction(action);
}

int CGUIDialogVideoScrub::LastIndex() const
{
  int64_t last = m_previews.Count() - 1;
  if (m_totalMs > 0)
    last = std::min<int64_t>(last, (m_totalMs - 1) / m_previews.intervalMs);
  return static_cast<int>(std::max<int64_t>(last, 0));
}

void CGUIDialogVideoScrub::Step(int thumbnails)
{
  SetIndex(m_index + thumbnails);
}

void CGUIDialogVideoScrub::Move(const CAction& action, int direction)
{
  const auto now = std::chrono::steady_clock::now();
  const unsigned int held = action.GetHoldTime();
  if (held == 0 || direction != m_direction)
  {
    // a press is one thumbnail, and a hold moves on from there
    m_direction = direction;
    m_pending = 0.0;
    m_lastTick = now;
    Step(direction);
    return;
  }

  const int tier = std::min(static_cast<int>(held / TIER_MS), TIERS);
  const double rate = BASE_RATE * static_cast<double>(1 << tier);
  const double elapsed =
      std::min(std::chrono::duration<double>(now - m_lastTick).count(), MAX_TICK_SECONDS);
  m_lastTick = now;
  m_pending += rate * elapsed;
  const int steps = static_cast<int>(m_pending);
  m_pending -= steps;
  if (steps > 0)
    Step(direction * steps);
}

void CGUIDialogVideoScrub::SetIndex(int index)
{
  index = std::clamp(index, 0, LastIndex());
  const int cells = m_previews.CellsPerTile();
  const int tile = index / cells;
  const int cell = index % cells;
  const float x = static_cast<float>(cell % m_previews.columns * m_previews.width);
  const float y = static_cast<float>(cell / m_previews.columns * m_previews.height);

  auto* image = dynamic_cast<CGUIImage*>(GetControl(CONTROL_PREVIEW));
  if (image)
  {
    // the region before the tile, so the tile is loaded whole; the region is in the tile's own
    // pixels, and follows whatever size the renderer could decode it at
    image->SetRegion(CRect(x, y, x + m_previews.width, y + m_previews.height),
                     static_cast<float>(m_previews.columns * m_previews.width));
    if (tile != m_tile)
      image->SetFileName(m_previews.tiles[tile], false, false);
  }
  m_index = index;
  m_tile = tile;
  Prefetch(tile);

  const int64_t positionMs = static_cast<int64_t>(index) * m_previews.intervalMs;
  const int64_t deltaMs = positionMs - m_startMs;
  SetProperty("scrubtime", StringUtils::SecondsToTimeString(static_cast<long>(positionMs / 1000),
                                                            TIME_FORMAT_GUESS));
  SetProperty("scrubdelta",
              std::string(deltaMs < 0 ? "-" : "+") +
                  StringUtils::SecondsToTimeString(static_cast<long>(std::llabs(deltaMs) / 1000),
                                                   TIME_FORMAT_GUESS));
  SetProperty("scrubpercent",
              m_totalMs > 0 ? static_cast<int>(positionMs * 100 / m_totalMs) : 0);
}

void CGUIDialogVideoScrub::Prefetch(int tile)
{
  // the tiles either side load while this one shows, so a hold does not wait at a tile's edge
  std::vector<std::string> wanted;
  for (const int neighbour : {tile - 1, tile + 1})
  {
    if (neighbour >= 0 && neighbour < static_cast<int>(m_previews.tiles.size()))
      wanted.push_back(m_previews.tiles[neighbour]);
  }

  CGUILargeTextureManager& manager = CServiceBroker::GetGUI()->GetLargeTextureManager();
  for (auto it = m_prefetched.begin(); it != m_prefetched.end();)
  {
    if (std::find(wanted.begin(), wanted.end(), *it) == wanted.end())
    {
      manager.ReleaseImage(*it, 0, 0, CAspectRatio::KEEP, false);
      it = m_prefetched.erase(it);
    }
    else
      ++it;
  }
  for (const std::string& path : wanted)
  {
    if (std::find(m_prefetched.begin(), m_prefetched.end(), path) != m_prefetched.end())
      continue;
    // the same request as the image control's: the whole tile, uncached; the reference it
    // takes is released in ReleasePrefetched() or above
    CTextureArray texture;
    manager.GetImage(path, texture, 0, 0, CAspectRatio::KEEP, false, true, false);
    m_prefetched.push_back(path);
  }
}

void CGUIDialogVideoScrub::ReleasePrefetched()
{
  CGUILargeTextureManager& manager = CServiceBroker::GetGUI()->GetLargeTextureManager();
  for (const std::string& path : m_prefetched)
    manager.ReleaseImage(path, 0, 0, CAspectRatio::KEEP, false);
  m_prefetched.clear();
}
