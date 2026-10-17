/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "XBDateTime.h"

#include <string>
#include <utility>
#include <vector>

class CFileItemList;
class CMusicDatabase;

namespace KODI::PLAYLIST
{
class CPlayList;

struct CPlayListHistoryTrack
{
  std::string path;
  std::string title;
  std::string artist;
  std::string album;
  std::string thumb;
  std::string fanart;
  int duration{0};
  // The part of the file a cue sheet track plays, in milliseconds; both 0 for the whole file.
  int startOffset{0};
  int endOffset{0};
};

/*!
 * \brief A music playlist as it was when playback left it.
 */
class CPlayListHistoryEntry
{
public:
  std::vector<CPlayListHistoryTrack> tracks;
  int position{0};
  double seconds{0.0};
  bool playedThrough{false};
  CDateTime saved;

  /*!
   * \brief What a playlist looks like at the moment playback leaves it.
   * \param playlist The playlist.
   * \param position Index of the track that was playing.
   * \param seconds How far into that track playback was.
   * \param playedThrough Whether the last track finished rather than being stopped.
   */
  static CPlayListHistoryEntry FromPlayList(const CPlayList& playlist,
                                            int position,
                                            double seconds,
                                            bool playedThrough);

  /*! \brief Index of the track that was playing, within the tracks. */
  int Current() const;

  /*! \brief Tracks after the one that was playing. */
  int Unplayed() const;

  /*! \brief A name derived from the tracks: the album, the artist or the playing track. */
  std::string Title() const;

  /*! \brief Two lines: where playback was, and when and how far into the track it stopped. */
  std::string Detail() const;

  /*! \brief Thumb of the track that was playing. */
  std::string Thumb() const;

  /*!
   * \brief Where a restore begins.
   * \param fromTrackStart Begin the track at 0 rather than at the saved second.
   * \return The track index and the second within it.
   */
  std::pair<int, double> Start(bool fromTrackStart) const;

  /*! \brief True when the other entry is this one captured again: same tracks, track and second. */
  bool IsRepeatOf(const CPlayListHistoryEntry& other) const;

  /*!
   * \brief Build the items to queue: the library's row for a track that has one, the saved facts otherwise.
   */
  void GetItems(CFileItemList& items) const;

  /*!
   * \brief Build the items to queue, matching tracks against the given library.
   * \param items The items.
   * \param library An open music database, or nullptr to build every item from the saved facts.
   */
  void GetItems(CFileItemList& items, CMusicDatabase* library) const;
};

/*!
 * \brief The music playlists that were replaced, interrupted or stopped, newest first.
 */
class CPlayListHistory
{
public:
  explicit CPlayListHistory(std::string folder = {});

  /*! \brief Load the history kept in the given profile folder. */
  void ReInit(std::string folder);

  /*!
   * \brief Put an entry at the front of the history, unless it repeats the newest one.
   * \param entry The entry.
   * \param keep How many entries to keep.
   */
  void Add(CPlayListHistoryEntry entry, int keep);

  const std::vector<CPlayListHistoryEntry>& Entries() const { return m_entries; }

private:
  bool Load();
  bool Save() const;

  std::string m_file;
  std::vector<CPlayListHistoryEntry> m_entries;
};

} // namespace KODI::PLAYLIST
