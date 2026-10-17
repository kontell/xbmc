/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "FileItem.h"
#include "filesystem/File.h"
#include "music/tags/MusicInfoTag.h"
#include "playlists/PlayList.h"
#include "playlists/PlayListHistory.h"
#include "test/TestUtils.h"
#include "utils/URIUtils.h"

#include <memory>
#include <string>

#include <gtest/gtest.h>

using namespace KODI;

namespace
{
PLAYLIST::CPlayListHistoryEntry Entry(int tracks,
                                      int position,
                                      double seconds,
                                      const std::string& artist = "Artist")
{
  PLAYLIST::CPlayListHistoryEntry entry;
  for (int i = 0; i < tracks; ++i)
  {
    PLAYLIST::CPlayListHistoryTrack track;
    track.path = "/music/" + std::to_string(i) + ".flac";
    track.title = "Track " + std::to_string(i);
    track.artist = artist;
    track.album = "Album";
    entry.tracks.emplace_back(std::move(track));
  }
  entry.position = position;
  entry.seconds = seconds;
  return entry;
}

std::shared_ptr<CFileItem> Item(const std::string& path,
                                const std::string& title,
                                const std::string& artist,
                                const std::string& album,
                                int duration)
{
  auto item = std::make_shared<CFileItem>(path, false);
  MUSIC_INFO::CMusicInfoTag* tag = item->GetMusicInfoTag();
  tag->SetTitle(title);
  tag->SetAlbumArtist(artist);
  tag->SetAlbum(album);
  tag->SetDuration(duration);
  tag->SetLoaded(true);
  return item;
}

class TestPlayListHistory : public ::testing::Test
{
protected:
  void SetUp() override
  {
    m_marker = XBMC_CREATETEMPFILE(".xml");
    m_folder = CXBMCTestUtils::Instance().TempFileDirectory(m_marker);
    XFILE::CFile::Delete(URIUtils::AddFileToFolder(m_folder, "playlisthistory.xml"));
  }

  void TearDown() override
  {
    XFILE::CFile::Delete(URIUtils::AddFileToFolder(m_folder, "playlisthistory.xml"));
    XBMC_DELETETEMPFILE(m_marker);
  }

  XFILE::CFile* m_marker{nullptr};
  std::string m_folder;
};
} // namespace

TEST(TestPlayListHistoryEntry, TitleNamesTheAlbumAndItsArtist)
{
  EXPECT_EQ(Entry(3, 0, 0.0).Title(), "Album - Artist");
}

TEST(TestPlayListHistoryEntry, TitleNamesOnlyTheAlbumWhenArtistsDiffer)
{
  PLAYLIST::CPlayListHistoryEntry entry = Entry(3, 0, 0.0);
  entry.tracks[1].artist = "Guest";
  EXPECT_EQ(entry.Title(), "Album");
}

TEST(TestPlayListHistoryEntry, UnplayedCountsTheTracksAfterThePlayingOne)
{
  EXPECT_EQ(Entry(10, 2, 0.0).Unplayed(), 7);
  EXPECT_EQ(Entry(10, 9, 0.0).Unplayed(), 0);
  EXPECT_EQ(Entry(10, 42, 0.0).Current(), 9);
}

TEST(TestPlayListHistoryEntry, StartsWhereItStopped)
{
  EXPECT_EQ(Entry(10, 2, 70.0).Start(false), std::make_pair(2, 70.0));
}

TEST(TestPlayListHistoryEntry, StartsTheTrackFromTheBeginningWhenAsked)
{
  EXPECT_EQ(Entry(10, 2, 70.0).Start(true), std::make_pair(2, 0.0));
}

TEST(TestPlayListHistoryEntry, DropsAnOffsetShorterThanFiveSeconds)
{
  EXPECT_EQ(Entry(10, 2, 3.0).Start(false), std::make_pair(2, 0.0));
}

TEST(TestPlayListHistoryEntry, StartsAPlayedThroughPlaylistAtTheFirstTrack)
{
  PLAYLIST::CPlayListHistoryEntry entry = Entry(10, 9, 240.0);
  entry.playedThrough = true;
  EXPECT_EQ(entry.Start(false), std::make_pair(0, 0.0));
}

TEST(TestPlayListHistoryEntry, ARepeatHasTheSameTracksTrackAndSecond)
{
  const PLAYLIST::CPlayListHistoryEntry entry = Entry(10, 2, 70.4);
  EXPECT_TRUE(Entry(10, 2, 70.9).IsRepeatOf(entry));
  EXPECT_FALSE(Entry(10, 3, 70.4).IsRepeatOf(entry));
  EXPECT_FALSE(Entry(10, 2, 75.0).IsRepeatOf(entry));
  EXPECT_FALSE(Entry(9, 2, 70.4).IsRepeatOf(entry));
}

TEST_F(TestPlayListHistory, AddKeepsThePlaylistAndPersistsIt)
{
  PLAYLIST::CPlayList playlist;
  playlist.Add(Item("/music/1.flac", "One", "Artist", "Album", 200));
  playlist.Add(Item("/music/2.flac", "Two", "Artist", "Album", 210));

  PLAYLIST::CPlayListHistory history(m_folder);
  history.Add(PLAYLIST::CPlayListHistoryEntry::FromPlayList(playlist, 1, 42.5, false));
  ASSERT_EQ(history.Entries().size(), 1u);

  PLAYLIST::CPlayListHistory reloaded(m_folder);
  ASSERT_EQ(reloaded.Entries().size(), 1u);
  const PLAYLIST::CPlayListHistoryEntry& entry = reloaded.Entries().front();
  EXPECT_EQ(entry.position, 1);
  EXPECT_DOUBLE_EQ(entry.seconds, 42.5);
  EXPECT_FALSE(entry.playedThrough);
  EXPECT_TRUE(entry.saved.IsValid());
  ASSERT_EQ(entry.tracks.size(), 2u);
  EXPECT_EQ(entry.tracks[1].path, "/music/2.flac");
  EXPECT_EQ(entry.tracks[1].title, "Two");
  EXPECT_EQ(entry.tracks[1].artist, "Artist");
  EXPECT_EQ(entry.tracks[1].album, "Album");
  EXPECT_EQ(entry.tracks[1].duration, 210);
}

TEST_F(TestPlayListHistory, AddPutsTheNewestFirstAndSkipsARepeat)
{
  PLAYLIST::CPlayList playlist;
  playlist.Add(Item("/music/1.flac", "One", "Artist", "Album", 200));
  playlist.Add(Item("/music/2.flac", "Two", "Artist", "Album", 210));

  PLAYLIST::CPlayListHistory history(m_folder);
  history.Add(PLAYLIST::CPlayListHistoryEntry::FromPlayList(playlist, 0, 10.0, false));
  history.Add(PLAYLIST::CPlayListHistoryEntry::FromPlayList(playlist, 0, 10.5, false));
  ASSERT_EQ(history.Entries().size(), 1u);

  history.Add(PLAYLIST::CPlayListHistoryEntry::FromPlayList(playlist, 1, 30.0, true));
  ASSERT_EQ(history.Entries().size(), 2u);
  EXPECT_EQ(history.Entries().front().position, 1);
  EXPECT_TRUE(history.Entries().front().playedThrough);
}

TEST_F(TestPlayListHistory, AddTrimsToTheMaximum)
{
  PLAYLIST::CPlayList playlist;
  playlist.Add(Item("/music/1.flac", "One", "Artist", "Album", 200));

  PLAYLIST::CPlayListHistory history(m_folder);
  for (int i = 0; i <= PLAYLIST::CPlayListHistory::MAX_ENTRIES; ++i)
    history.Add(PLAYLIST::CPlayListHistoryEntry::FromPlayList(playlist, 0, 10.0 * i, false));
  EXPECT_EQ(history.Entries().size(), static_cast<size_t>(PLAYLIST::CPlayListHistory::MAX_ENTRIES));
  EXPECT_DOUBLE_EQ(history.Entries().front().seconds,
                   10.0 * PLAYLIST::CPlayListHistory::MAX_ENTRIES);
}
