/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "FileItemList.h"
#include "filesystem/File.h"
#include "filesystem/SpecialProtocol.h"
#include "music/Album.h"
#include "music/MusicDatabase.h"
#include "music/Song.h"
#include "settings/AdvancedSettings.h"

#include <gtest/gtest.h>

namespace
{
constexpr const char* DB_NAME = "TestMusicDatabase";

class TestMusicDatabase : public testing::Test
{
protected:
  void SetUp() override
  {
    m_settings.type = "sqlite3";
    m_settings.host = CSpecialProtocol::TranslatePath("special://temp/");
    ASSERT_EQ(CDatabase::ConnectionState::STATE_CONNECTED, m_db.Connect(DB_NAME, m_settings, true));
  }

  void TearDown() override
  {
    m_db.Close();
    XFILE::CFile::Delete(m_settings.host + DB_NAME + ".db");
  }

  void ExpectNoScratchTables()
  {
    EXPECT_EQ("0", m_db.GetSingleValue("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' "
                                       "AND name IN ('tempDisco', 'tempAlbum')"));
  }

  DatabaseSettings m_settings;
  CMusicDatabase m_db;
};
} // namespace

TEST_F(TestMusicDatabase, DiscographyCleansUpAfterSuccess)
{
  ASSERT_TRUE(m_db.ExecuteQuery("INSERT INTO discography VALUES (2, 'Album', '2026', 'release')"));
  CFileItemList items;
  EXPECT_TRUE(m_db.GetArtistDiscography(2, items));
  EXPECT_EQ(1, items.Size());
  ExpectNoScratchTables();
}

TEST_F(TestMusicDatabase, DiscographyCleansUpAfterEmptyResult)
{
  CFileItemList items;
  EXPECT_TRUE(m_db.GetArtistDiscography(2, items));
  EXPECT_EQ(0, items.Size());
  ExpectNoScratchTables();
}

TEST_F(TestMusicDatabase, DiscographyCleansUpAfterQueryError)
{
  ASSERT_TRUE(m_db.ExecuteQuery("DROP TABLE discography"));
  CFileItemList items;
  EXPECT_FALSE(m_db.GetArtistDiscography(2, items));
  ExpectNoScratchTables();
}

TEST_F(TestMusicDatabase, DiscographyRecoversLeftoverTables)
{
  ASSERT_TRUE(m_db.ExecuteQuery("CREATE TABLE tempDisco (strAlbum TEXT)"));
  ASSERT_TRUE(m_db.ExecuteQuery("CREATE TABLE tempAlbum (strAlbum TEXT)"));
  CFileItemList items;
  EXPECT_TRUE(m_db.GetArtistDiscography(2, items));
  EXPECT_EQ(0, items.Size());
  ExpectNoScratchTables();
}

TEST_F(TestMusicDatabase, CommitTransactionSucceedsWithoutAGUI)
{
  // The test process has no GUI, so there are no library info booleans to refresh
  m_db.BeginTransaction();
  ASSERT_TRUE(m_db.ExecuteQuery("INSERT INTO discography VALUES (2, 'Album', '2026', 'release')"));
  EXPECT_TRUE(m_db.CommitTransaction());
  EXPECT_EQ("1", m_db.GetSingleValue("SELECT COUNT(*) FROM discography"));
}

namespace
{
constexpr const char* DATE_ADDED = "2020-01-02 03:04:05";

// An album of one song, as the scanner hands it over
CAlbum AlbumOfOneSong(const std::string& file)
{
  CSong song;
  song.strTitle = "Song";
  song.strFileName = file;
  song.dateAdded.SetFromDBDateTime(DATE_ADDED);
  CAlbum album;
  album.strAlbum = "Album";
  album.songs.push_back(song);
  return album;
}
} // namespace

TEST_F(TestMusicDatabase, APluginSongKeepsTheDateAddedItCarries)
{
  CAlbum album{AlbumOfOneSong("plugin://plugin.audio.test/album/1.mp3")};
  ASSERT_TRUE(m_db.AddAlbum(album, -1));

  EXPECT_EQ(DATE_ADDED, m_db.GetSingleValue("SELECT dateAdded FROM song"));
  EXPECT_EQ(DATE_ADDED, m_db.GetSingleValue("SELECT dateAdded FROM album"));
}

TEST_F(TestMusicDatabase, UpdatingAPluginSongKeepsItsDateAdded)
{
  CAlbum album{AlbumOfOneSong("plugin://plugin.audio.test/album/1.mp3")};
  ASSERT_TRUE(m_db.AddAlbum(album, -1));
  CSong song;
  ASSERT_TRUE(m_db.GetSong(album.songs[0].idSong, song));

  song.iTimesPlayed = 1;
  ASSERT_TRUE(m_db.UpdateSong(song));

  EXPECT_EQ(DATE_ADDED, m_db.GetSingleValue("SELECT dateAdded FROM song"));
}

TEST_F(TestMusicDatabase, ASongFileIsDatedByTheFile)
{
  const std::string file{CSpecialProtocol::TranslatePath("special://temp/TestMusicDatabase.mp3")};
  XFILE::CFile handle;
  ASSERT_TRUE(handle.OpenForWrite(file, true));
  handle.Close();
  CAlbum album{AlbumOfOneSong(file)};

  const bool added{m_db.AddAlbum(album, -1)};
  XFILE::CFile::Delete(file);

  ASSERT_TRUE(added);
  EXPECT_NE(DATE_ADDED, m_db.GetSingleValue("SELECT dateAdded FROM song"));
}
