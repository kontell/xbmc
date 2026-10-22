/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "interfaces/legacy/Exception.h"
#include "interfaces/legacy/InfoTagMusic.h"
#include "music/tags/MusicInfoTag.h"

#include <chrono>
#include <limits>
#include <utility>
#include <vector>

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

TEST(TestInfoTagMusic, SetChaptersRefusesTimesItCannotStore)
{
  MUSIC_INFO::CMusicInfoTag tag;
  XBMCAddon::xbmc::InfoTagMusic::setChaptersRaw(&tag, {{0.0, 60.0, "One"}, {60.0, 120.0, "Two"}});

  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  const std::vector<std::pair<double, double>> refused = {
      {nan, 60.0}, {0.0, nan}, {0.0, inf}, {-1.0, 60.0}, {60.0, 30.0}, {0.0, 1e17}};
  for (const auto& [start, end] : refused)
  {
    EXPECT_THROW(XBMCAddon::xbmc::InfoTagMusic::setChaptersRaw(
                     &tag, {{0.0, 10.0, "Fine"}, {start, end, "Refused"}}),
                 XBMCAddon::WrongTypeException)
        << start << " to " << end;
  }

  const auto& marks = tag.GetChapterMarks();
  ASSERT_EQ(2u, marks.size());
  EXPECT_EQ("One", marks[0].name);
  EXPECT_EQ("Two", marks[1].name);
}
