/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <string>
#include <vector>

/*!
 * \brief Thumbnails to scrub through a video with: one every interval from the start, packed
 * into tiles of columns × rows cells in reading order, the tiles in order. An add-on sets them on
 * the item it plays, as Jellyfin's trickplay tiles or YouTube's storyboards, and the scrub dialog
 * steps through them.
 */
struct ScrubPreviews
{
  int intervalMs{0}; //!< the time between two thumbnails
  int width{0}; //!< a thumbnail's size in pixels, the size of a cell
  int height{0};
  int columns{0}; //!< cells across a tile
  int rows{0}; //!< cells down a tile
  std::vector<std::string> tiles; //!< the tile images, empty when the video has none
  int count{0}; //!< thumbnails there are; the last tile may be part filled

  bool HasPreviews() const { return !tiles.empty(); }
  int CellsPerTile() const { return columns * rows; }
  int Count() const { return count; }
};
