/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <chrono>
#include <string>

// Struct to store chapter information of a song or a video, from its file or set by an add-on
struct ChapterDetails
{
  std::string name;
  std::chrono::milliseconds startTimeMs{0}; // All chapter timings are in milliseconds
  std::chrono::milliseconds endTimeMs{0};
};
