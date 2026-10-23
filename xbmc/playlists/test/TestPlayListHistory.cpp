/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "FileItem.h"
#include "FileItemList.h"
#include "filesystem/File.h"
#include "filesystem/SpecialProtocol.h"
#include "music/Album.h"
#include "music/MusicDatabase.h"
#include "music/tags/MusicInfoTag.h"
#include "playlists/PlayList.h"
#include "playlists/PlayListHistory.h"
#include "settings/AdvancedSettings.h"
#include "test/TestUtils.h"
#include "utils/ItemProperties.h"
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

CSong CueSong(const std::string& file, const std::string& title, int track, int start, int end)
{
  CSong song;
  song.strTitle = title;
  song.strFileName = file;
  song.iTrack = track;
  song.iStartOffset = start;
  song.iEndOffset = end;
  return song;
}

PLAYLIST::CPlayListHistoryTrack SavedTrack(const std::string& path,
                                           const std::string& title,
                                           int start = 0,
                                           int end = 0)
{
  PLAYLIST::CPlayListHistoryTrack track;
  track.path = path;
  track.title = title;
  track.startOffset = start;
  track.endOffset = end;
  return track;
}

class TestPlayListHistory : public ::testing::Test
{
protected:
  void SetUp() override
  {
    m_marker = XBMC_CREATETEMPFILE(".xml");
    m_folder = CXBMCTestUtils::Instance().TempFileDirectory(m_marker);
    m_file = URIUtils::AddFileToFolder(m_folder, "playlisthistory.xml");
    XFILE::CFile::Delete(m_file);
    XFILE::CFile::Delete(m_file + ".tmp");
  }

  void TearDown() override
  {
    XFILE::CFile::Delete(m_file);
    XFILE::CFile::Delete(m_file + ".tmp");
    XBMC_DELETETEMPFILE(m_marker);
  }

  XFILE::CFile* m_marker{nullptr};
  std::string m_folder;
  std::string m_file;
};

class TestPlayListHistoryLibrary : public ::testing::Test
{
protected:
  void SetUp() override
  {
    m_settings.type = "sqlite3";
    m_settings.host = CSpecialProtocol::TranslatePath("special://temp/");
    ASSERT_EQ(CDatabase::ConnectionState::STATE_CONNECTED,
              m_db.Connect("TestPlayListHistoryLibrary", m_settings, true));
  }

  void TearDown() override
  {
    m_db.Close();
    XFILE::CFile::Delete(m_settings.host + "TestPlayListHistoryLibrary.db");
  }

  DatabaseSettings m_settings;
  CMusicDatabase m_db;
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

TEST(TestPlayListHistoryEntry, ARepeatHasTheSamePartsOfItsFiles)
{
  const PLAYLIST::CPlayListHistoryEntry entry = Entry(2, 0, 10.0);
  PLAYLIST::CPlayListHistoryEntry other = entry;
  other.tracks[1].startOffset = 40000;
  EXPECT_FALSE(other.IsRepeatOf(entry));
}

TEST(TestPlayListHistoryEntry, GetItemsPlaysOnlyTheSavedPartOfAFile)
{
  PLAYLIST::CPlayListHistoryEntry entry = Entry(2, 0, 0.0);
  entry.tracks[1].startOffset = 40000;
  entry.tracks[1].endOffset = 80000;

  CFileItemList items;
  entry.GetItems(items, nullptr);
  ASSERT_EQ(items.Size(), 2);
  EXPECT_EQ(items[0]->GetStartOffset(), 0);
  EXPECT_EQ(items[0]->GetEndOffset(), 0);
  EXPECT_FALSE(items[0]->HasProperty(ITEM::PROPERTY::ITEM_START));
  EXPECT_EQ(items[1]->GetStartOffset(), 40000);
  EXPECT_EQ(items[1]->GetEndOffset(), 80000);
  EXPECT_EQ(items[1]->GetProperty(ITEM::PROPERTY::ITEM_START).asInteger(), 40000);
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

TEST_F(TestPlayListHistoryLibrary, GetItemsQueuesTheLibrarySongForATrackThatHasOne)
{
  CAlbum album;
  album.strAlbum = "Album";
  CSong plugin;
  plugin.strTitle = "One";
  plugin.strFileName = "plugin://plugin.audio.test/music/album/1.flac";
  plugin.iTimesPlayed = 3;
  album.songs.emplace_back(plugin);
  CSong file;
  file.strTitle = "Two";
  file.strFileName = "/music/album/2.flac";
  file.iTimesPlayed = 1;
  album.songs.emplace_back(file);
  ASSERT_TRUE(m_db.AddAlbum(album, -1));

  PLAYLIST::CPlayListHistoryEntry entry;
  for (const std::string& path : {plugin.strFileName, file.strFileName,
                                  std::string("/music/elsewhere/3.flac")})
  {
    PLAYLIST::CPlayListHistoryTrack track;
    track.path = path;
    track.title = "Saved";
    entry.tracks.emplace_back(std::move(track));
  }

  CFileItemList items;
  entry.GetItems(items, &m_db);
  ASSERT_EQ(items.Size(), 3);

  const MUSIC_INFO::CMusicInfoTag* tag = items[0]->GetMusicInfoTag();
  EXPECT_EQ(tag->GetDatabaseId(), album.songs[0].idSong);
  EXPECT_EQ(tag->GetPlayCount(), 3);
  EXPECT_EQ(tag->GetTitle(), "One");
  EXPECT_EQ(items[0]->GetDynPath(), plugin.strFileName);

  EXPECT_EQ(items[1]->GetMusicInfoTag()->GetDatabaseId(), album.songs[1].idSong);
  EXPECT_EQ(items[1]->GetMusicInfoTag()->GetPlayCount(), 1);

  EXPECT_EQ(items[2]->GetMusicInfoTag()->GetDatabaseId(), -1);
  EXPECT_EQ(items[2]->GetMusicInfoTag()->GetTitle(), "Saved");
  EXPECT_EQ(items[2]->GetPath(), "/music/elsewhere/3.flac");
}

TEST_F(TestPlayListHistory, AddReplacesTheSavedHistory)
{
  PLAYLIST::CPlayList playlist;
  playlist.Add(Item("/music/1.flac", "One", "Artist", "Album", 200));

  PLAYLIST::CPlayListHistory history(m_folder);
  history.Add(PLAYLIST::CPlayListHistoryEntry::FromPlayList(playlist, 0, 10.0, false));
  history.Add(PLAYLIST::CPlayListHistoryEntry::FromPlayList(playlist, 0, 20.0, false));

  PLAYLIST::CPlayListHistory reloaded(m_folder);
  ASSERT_EQ(reloaded.Entries().size(), 2u);
  EXPECT_DOUBLE_EQ(reloaded.Entries().front().seconds, 20.0);
}

TEST_F(TestPlayListHistory, LoadReadsANewHistoryLeftBesideTheFile)
{
  PLAYLIST::CPlayList playlist;
  playlist.Add(Item("/music/1.flac", "One", "Artist", "Album", 200));
  {
    PLAYLIST::CPlayListHistory history(m_folder);
    history.Add(PLAYLIST::CPlayListHistoryEntry::FromPlayList(playlist, 0, 42.5, false));
  }
  // As a quit leaves it between removing the old history and renaming the new one into place.
  ASSERT_TRUE(XFILE::CFile::Rename(m_file, m_file + ".tmp"));

  PLAYLIST::CPlayListHistory reloaded(m_folder);
  ASSERT_EQ(reloaded.Entries().size(), 1u);
  EXPECT_DOUBLE_EQ(reloaded.Entries().front().seconds, 42.5);
}

TEST_F(TestPlayListHistory, AddKeepsThePartOfTheFileEachCueSheetTrackPlays)
{
  PLAYLIST::CPlayList playlist;
  playlist.Add(std::make_shared<CFileItem>(CueSong("/music/tones.flac", "Low", 1, 0, 40000)));
  playlist.Add(
      std::make_shared<CFileItem>(CueSong("/music/tones.flac", "Middle", 2, 40000, 80000)));
  playlist.Add(std::make_shared<CFileItem>(CueSong("/music/tones.flac", "High", 3, 80000, 0)));
  // A start offset moved to resume part-way in.
  playlist[1]->SetStartOffset(52000);

  PLAYLIST::CPlayListHistory history(m_folder);
  history.Add(PLAYLIST::CPlayListHistoryEntry::FromPlayList(playlist, 1, 12.0, false));

  PLAYLIST::CPlayListHistory reloaded(m_folder);
  ASSERT_EQ(reloaded.Entries().size(), 1u);
  const auto& tracks = reloaded.Entries().front().tracks;
  ASSERT_EQ(tracks.size(), 3u);
  EXPECT_EQ(tracks[0].startOffset, 0);
  EXPECT_EQ(tracks[0].endOffset, 40000);
  EXPECT_EQ(tracks[1].startOffset, 40000);
  EXPECT_EQ(tracks[1].endOffset, 80000);
  EXPECT_EQ(tracks[2].startOffset, 80000);
  EXPECT_EQ(tracks[2].endOffset, 0);
}

TEST_F(TestPlayListHistoryLibrary, GetItemsQueuesTheCueSheetSongThatWasSaved)
{
  const std::string file = "/music/tones/tones.flac";
  CAlbum album;
  album.strAlbum = "Tones";
  album.songs.emplace_back(CueSong(file, "Tone", 1, 0, 40000));
  album.songs.emplace_back(CueSong(file, "Tone", 2, 40000, 80000));
  album.songs.emplace_back(CueSong(file, "High", 3, 80000, 0));
  ASSERT_TRUE(m_db.AddAlbum(album, -1));

  // The first two share a title and were saved with their parts; the third was saved without.
  PLAYLIST::CPlayListHistoryEntry entry;
  entry.tracks.push_back(SavedTrack(file, "Tone", 40000, 80000));
  entry.tracks.push_back(SavedTrack(file, "Tone", 0, 40000));
  entry.tracks.push_back(SavedTrack(file, "High"));

  CFileItemList items;
  entry.GetItems(items, &m_db);
  ASSERT_EQ(items.Size(), 3);
  EXPECT_EQ(items[0]->GetMusicInfoTag()->GetDatabaseId(), album.songs[1].idSong);
  EXPECT_EQ(items[0]->GetStartOffset(), 40000);
  EXPECT_EQ(items[0]->GetEndOffset(), 80000);
  EXPECT_EQ(items[1]->GetMusicInfoTag()->GetDatabaseId(), album.songs[0].idSong);
  EXPECT_EQ(items[2]->GetMusicInfoTag()->GetDatabaseId(), album.songs[2].idSong);
}
