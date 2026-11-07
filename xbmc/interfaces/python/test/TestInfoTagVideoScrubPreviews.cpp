/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "FileItem.h"
#include "interfaces/legacy/Exception.h"
#include "interfaces/legacy/InfoTagVideo.h"
#include "video/VideoInfoTag.h"

#include <limits>

#include <gtest/gtest.h>

TEST(TestInfoTagVideoScrubPreviews, SetScrubPreviewsStoresThem)
{
  CVideoInfoTag tag;
  XBMCAddon::xbmc::InfoTagVideo::setScrubPreviewsRaw(&tag, 10000, 320, 180, 10, 10,
                                                     {"http://s/t0.jpg", "http://s/t1.jpg"});

  const ScrubPreviews& previews = tag.GetScrubPreviews();
  EXPECT_TRUE(previews.HasPreviews());
  EXPECT_EQ(10000, previews.intervalMs);
  EXPECT_EQ(320, previews.width);
  EXPECT_EQ(180, previews.height);
  EXPECT_EQ(100, previews.CellsPerTile());
  EXPECT_EQ(200, previews.Count()) << "no count given: every cell of every tile";
  ASSERT_EQ(2u, previews.tiles.size());
  EXPECT_EQ("http://s/t1.jpg", previews.tiles[1]);

  XBMCAddon::xbmc::InfoTagVideo::setScrubPreviewsRaw(&tag, 10000, 320, 180, 10, 10,
                                                     {"http://s/t0.jpg", "http://s/t1.jpg"}, 159);
  EXPECT_EQ(159, tag.GetScrubPreviews().Count()) << "the last tile is part filled";
}

TEST(TestInfoTagVideoScrubPreviews, NoTilesClearsThem)
{
  CVideoInfoTag tag;
  XBMCAddon::xbmc::InfoTagVideo::setScrubPreviewsRaw(&tag, 10000, 320, 180, 10, 10, {"t0"});
  XBMCAddon::xbmc::InfoTagVideo::setScrubPreviewsRaw(&tag, 0, 0, 0, 0, 0, {});
  EXPECT_FALSE(tag.GetScrubPreviews().HasPreviews());
}

TEST(TestInfoTagVideoScrubPreviews, RefusesWhatItCannotStepThrough)
{
  CVideoInfoTag tag;
  XBMCAddon::xbmc::InfoTagVideo::setScrubPreviewsRaw(&tag, 10000, 320, 180, 10, 10, {"t0"});

  EXPECT_THROW(
      XBMCAddon::xbmc::InfoTagVideo::setScrubPreviewsRaw(&tag, 0, 320, 180, 10, 10, {"t"}),
      XBMCAddon::WrongTypeException);
  EXPECT_THROW(
      XBMCAddon::xbmc::InfoTagVideo::setScrubPreviewsRaw(&tag, 10000, 0, 180, 1, 1, {"t"}),
      XBMCAddon::WrongTypeException);
  EXPECT_THROW(
      XBMCAddon::xbmc::InfoTagVideo::setScrubPreviewsRaw(&tag, 10000, 320, 180, 0, 1, {"t"}),
      XBMCAddon::WrongTypeException);
  EXPECT_THROW(
      XBMCAddon::xbmc::InfoTagVideo::setScrubPreviewsRaw(&tag, 10000, 320, 180, 1, 1, {""}),
      XBMCAddon::WrongTypeException);
  // cells to a tile beyond an integer, and a count beyond the tiles
  EXPECT_THROW(XBMCAddon::xbmc::InfoTagVideo::setScrubPreviewsRaw(
                   &tag, 10000, 320, 180, std::numeric_limits<int>::max(), 2, {"t"}),
               XBMCAddon::WrongTypeException);
  EXPECT_THROW(
      XBMCAddon::xbmc::InfoTagVideo::setScrubPreviewsRaw(&tag, 10000, 320, 180, 2, 2, {"t"}, 5),
      XBMCAddon::WrongTypeException);
  EXPECT_THROW(
      XBMCAddon::xbmc::InfoTagVideo::setScrubPreviewsRaw(&tag, 10000, 320, 180, 2, 2, {"t"}, -1),
      XBMCAddon::WrongTypeException);

  EXPECT_EQ(1u, tag.GetScrubPreviews().tiles.size());
  EXPECT_EQ(100, tag.GetScrubPreviews().Count());
}

TEST(TestInfoTagVideoScrubPreviews, AnUpdateWithoutPreviewsKeepsThem)
{
  CFileItem playing;
  XBMCAddon::xbmc::InfoTagVideo::setScrubPreviewsRaw(playing.GetVideoInfoTag(), 5000, 160, 90, 5, 5,
                                                     {"old"});

  CFileItem fromDatabase;
  fromDatabase.GetVideoInfoTag()->SetTitle("title");
  playing.UpdateInfo(fromDatabase);
  EXPECT_EQ("title", playing.GetVideoInfoTag()->GetTitle());
  EXPECT_EQ("old", playing.GetVideoInfoTag()->GetScrubPreviews().tiles.at(0));

  CFileItem fromAddon;
  XBMCAddon::xbmc::InfoTagVideo::setScrubPreviewsRaw(fromAddon.GetVideoInfoTag(), 10000, 320, 180,
                                                     10, 10, {"new"});
  playing.UpdateInfo(fromAddon);
  EXPECT_EQ("new", playing.GetVideoInfoTag()->GetScrubPreviews().tiles.at(0));
}

TEST(TestInfoTagVideoScrubPreviews, MergeTakesTheResolvedItems)
{
  CVideoInfoTag item;
  XBMCAddon::xbmc::InfoTagVideo::setScrubPreviewsRaw(&item, 5000, 160, 90, 5, 5, {"old"});
  CVideoInfoTag resolved;
  XBMCAddon::xbmc::InfoTagVideo::setScrubPreviewsRaw(&resolved, 10000, 320, 180, 10, 10,
                                                     {"new"});

  item.Merge(resolved);
  EXPECT_EQ("new", item.GetScrubPreviews().tiles.at(0));

  CVideoInfoTag none;
  item.Merge(none);
  EXPECT_FALSE(item.GetScrubPreviews().HasPreviews());

  item.Reset();
  EXPECT_FALSE(item.GetScrubPreviews().HasPreviews());
}
