/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "interfaces/legacy/InfoTagMusic.h"
#include "music/tags/MusicInfoTag.h"

#include <chrono>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST(TestInfoTagMusic, SetChaptersStoresThemInMilliseconds)
{
  MUSIC_INFO::CMusicInfoTag tag;
  XBMCAddon::xbmc::InfoTagMusic::setChaptersRaw(
      &tag, {{0.0, 754.2504, "Opening Credits"}, {754.2504, 2311.0, "Chapter 1"}});

  const auto& marks = tag.GetChapterMarks();
  ASSERT_EQ(2u, marks.size());
  EXPECT_EQ("Opening Credits", marks[0].name);
  EXPECT_EQ(0ms, marks[0].startTimeMs);
  EXPECT_EQ(754250ms, marks[0].endTimeMs);
  EXPECT_EQ("Chapter 1", marks[1].name);
  EXPECT_EQ(754250ms, marks[1].startTimeMs);
  EXPECT_EQ(2311000ms, marks[1].endTimeMs);
}

TEST(TestInfoTagMusic, SetChaptersWithNoneClearsThem)
{
  MUSIC_INFO::CMusicInfoTag tag;
  XBMCAddon::xbmc::InfoTagMusic::setChaptersRaw(&tag, {{0.0, 60.0, "One"}, {60.0, 120.0, "Two"}});
  XBMCAddon::xbmc::InfoTagMusic::setChaptersRaw(&tag, {});
  EXPECT_TRUE(tag.GetChapterMarks().empty());
}
