/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "interfaces/legacy/Exception.h"
#include "interfaces/legacy/InfoTagVideo.h"
#include "video/VideoInfoTag.h"

#include <chrono>
#include <limits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST(TestInfoTagVideo, SetChaptersStoresThemInMillisecondsInOrder)
{
  CVideoInfoTag tag;
  XBMCAddon::xbmc::InfoTagVideo::setChaptersRaw(
      &tag, {{67.0, 162.0, "Series preview"}, {0.0, 67.0004, "Introduction"}});

  const auto& marks = tag.GetChapterMarks();
  ASSERT_EQ(2u, marks.size());
  EXPECT_EQ("Introduction", marks[0].name);
  EXPECT_EQ(0ms, marks[0].startTimeMs);
  EXPECT_EQ(67000ms, marks[0].endTimeMs);
  EXPECT_EQ("Series preview", marks[1].name);
  EXPECT_EQ(67000ms, marks[1].startTimeMs);
  EXPECT_EQ(162000ms, marks[1].endTimeMs);
}

TEST(TestInfoTagVideo, SetChaptersWithNoneClearsThem)
{
  CVideoInfoTag tag;
  XBMCAddon::xbmc::InfoTagVideo::setChaptersRaw(&tag, {{0.0, 60.0, "One"}, {60.0, 120.0, "Two"}});
  XBMCAddon::xbmc::InfoTagVideo::setChaptersRaw(&tag, {});
  EXPECT_TRUE(tag.GetChapterMarks().empty());
}

TEST(TestInfoTagVideo, SetChaptersRefusesTimesItCannotStore)
{
  CVideoInfoTag tag;
  XBMCAddon::xbmc::InfoTagVideo::setChaptersRaw(&tag, {{0.0, 60.0, "One"}, {60.0, 120.0, "Two"}});

  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  const std::vector<std::pair<double, double>> refused = {
      {nan, 60.0}, {0.0, nan}, {0.0, inf}, {-1.0, 60.0}, {60.0, 30.0}, {0.0, 1e17}};
  for (const auto& [start, end] : refused)
  {
    EXPECT_THROW(XBMCAddon::xbmc::InfoTagVideo::setChaptersRaw(
                     &tag, {{0.0, 10.0, "Fine"}, {start, end, "Refused"}}),
                 XBMCAddon::WrongTypeException)
        << start << " to " << end;
  }

  const auto& marks = tag.GetChapterMarks();
  ASSERT_EQ(2u, marks.size());
  EXPECT_EQ("One", marks[0].name);
  EXPECT_EQ("Two", marks[1].name);
}
