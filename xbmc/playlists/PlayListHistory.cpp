/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "PlayListHistory.h"

#include "FileItem.h"
#include "FileItemList.h"
#include "ServiceBroker.h"
#include "URL.h"
#include "filesystem/File.h"
#include "music/MusicDatabase.h"
#include "music/Song.h"
#include "music/tags/MusicInfoTag.h"
#include "playlists/PlayList.h"
#include "resources/LocalizeStrings.h"
#include "resources/ResourcesComponent.h"
#include "utils/ArtTypes.h"
#include "utils/FileUtils.h"
#include "utils/StringUtils.h"
#include "utils/TimeFormat.h"
#include "utils/URIUtils.h"
#include "utils/XBMCTinyXML2.h"
#include "utils/XMLUtils.h"
#include "utils/log.h"

#include <algorithm>
#include <map>

namespace KODI::PLAYLIST
{
namespace
{
constexpr const char* HISTORY_FILE = "playlisthistory.xml";

// An offset shorter than this is not worth resuming at.
constexpr double MIN_RESUME_SECONDS = 5.0;

// Two captures of the same track this close together are one event reported twice.
constexpr double REPEAT_TOLERANCE_SECONDS = 1.0;

const std::string& Localized(uint32_t id)
{
  return CServiceBroker::GetResourcesComponent().GetLocalizeStrings().Get(id);
}

// The directory and file name the music database stores a song under.
void SplitPath(const std::string& fileNameAndPath, std::string& path, std::string& fileName)
{
  URIUtils::Split(fileNameAndPath, path, fileName);
  if (URIUtils::IsURL(fileNameAndPath))
  {
    const CURL url(fileNameAndPath);
    if (!url.GetProtocolOptions().empty())
      path += "|" + url.GetProtocolOptions();
  }
}
} // namespace

int CPlayListHistoryEntry::Current() const
{
  if (tracks.empty())
    return 0;
  return std::clamp(position, 0, static_cast<int>(tracks.size()) - 1);
}

int CPlayListHistoryEntry::Unplayed() const
{
  return std::max(0, static_cast<int>(tracks.size()) - Current() - 1);
}

std::string CPlayListHistoryEntry::Title() const
{
  if (tracks.empty())
    return StringUtils::Format("0 {}", Localized(36921));

  // The value every track has for a field, or an empty string.
  const auto shared = [this](const auto& field) -> std::string
  {
    const std::string& first = field(tracks.front());
    if (first.empty() ||
        !std::ranges::all_of(tracks, [&](const auto& track) { return field(track) == first; }))
      return "";
    return first;
  };

  const std::string album = shared([](const CPlayListHistoryTrack& track) { return track.album; });
  const std::string artist =
      shared([](const CPlayListHistoryTrack& track) { return track.artist; });
  if (!album.empty())
    return artist.empty() ? album : StringUtils::Format("{} - {}", album, artist);
  if (!artist.empty())
    return StringUtils::Format("{} - {} {}", artist, tracks.size(), Localized(36921));

  const CPlayListHistoryTrack& playing = tracks[Current()];
  if (!playing.title.empty())
  {
    if (tracks.size() == 1)
      return playing.title;
    return StringUtils::Format(Localized(40812), playing.title,
                               static_cast<int>(tracks.size()) - 1);
  }
  return StringUtils::Format("{} {}", tracks.size(), Localized(36921));
}

std::string CPlayListHistoryEntry::Detail() const
{
  std::string when = saved.IsValid() ? saved.GetAsLocalizedDateTime(false, false) : "";
  if (playedThrough)
    return Localized(40810) + "[CR]" + when;

  if (seconds >= 1.0)
    when = StringUtils::Format(Localized(40811), when,
                               StringUtils::SecondsToTimeString(static_cast<long>(seconds)));
  return StringUtils::Format(Localized(40809), Current() + 1, static_cast<int>(tracks.size()),
                             Unplayed()) +
         "[CR]" + when;
}

std::string CPlayListHistoryEntry::Thumb() const
{
  return tracks.empty() ? "" : tracks[Current()].thumb;
}

std::pair<int, double> CPlayListHistoryEntry::Start(bool fromTrackStart) const
{
  if (playedThrough || tracks.empty())
    return {0, 0.0};
  if (fromTrackStart || seconds < MIN_RESUME_SECONDS)
    return {Current(), 0.0};
  return {Current(), seconds};
}

bool CPlayListHistoryEntry::IsRepeatOf(const CPlayListHistoryEntry& other) const
{
  return position == other.position &&
         std::abs(seconds - other.seconds) < REPEAT_TOLERANCE_SECONDS &&
         std::ranges::equal(
             tracks, other.tracks, [](const auto& a, const auto& b) { return a.path == b.path; });
}

void CPlayListHistoryEntry::GetItems(CFileItemList& items) const
{
  CMusicDatabase db;
  GetItems(items, db.Open() ? &db : nullptr);
}

void CPlayListHistoryEntry::GetItems(CFileItemList& items, CMusicDatabase* library) const
{
  // The songs of each folder of the playlist, queried once.
  std::map<std::string, std::map<std::string, std::vector<CSong>>> folders;

  for (const CPlayListHistoryTrack& track : tracks)
  {
    std::shared_ptr<CFileItem> item;
    if (library)
    {
      std::string folder;
      std::string fileName;
      SplitPath(track.path, folder, fileName);
      auto songs = folders.find(folder);
      if (songs == folders.end())
      {
        songs = folders.emplace(folder, std::map<std::string, std::vector<CSong>>{}).first;
        library->GetSongsByPath(folder, songs->second);
      }
      // Keyed by the folder and file name joined, as the database builds a song's path.
      const auto song = songs->second.find(URIUtils::AddFileToFolder(folder, fileName));
      if (song != songs->second.end() && !song->second.empty())
      {
        // A cue sheet gives several songs one file; take the one that was saved.
        const auto byTitle = std::ranges::find_if(song->second, [&track](const CSong& s)
                                                  { return s.strTitle == track.title; });
        const CSong& found = byTitle != song->second.end() ? *byTitle : song->second.front();
        item = std::make_shared<CFileItem>(found);
        item->SetDynPath(found.strFileName);
      }
    }

    if (!item)
    {
      item = std::make_shared<CFileItem>(track.path, false);
      item->SetLabel(track.title.empty() ? URIUtils::GetFileName(track.path) : track.title);
      MUSIC_INFO::CMusicInfoTag* tag = item->GetMusicInfoTag();
      tag->SetTitle(track.title);
      tag->SetArtist(track.artist);
      tag->SetAlbum(track.album);
      tag->SetDuration(track.duration);
      tag->SetLoaded(true);
    }

    if (!track.thumb.empty() && !item->HasArt(ART::TYPE::THUMB))
      item->SetArt(ART::TYPE::THUMB, track.thumb);
    if (!track.fanart.empty() && !item->HasArt(ART::TYPE::FANART))
      item->SetArt(ART::TYPE::FANART, track.fanart);
    items.Add(std::move(item));
  }
}

CPlayListHistory::CPlayListHistory(std::string folder)
{
  ReInit(std::move(folder));
}

void CPlayListHistory::ReInit(std::string folder)
{
  m_entries.clear();
  m_file = folder.empty() ? "" : URIUtils::AddFileToFolder(folder, HISTORY_FILE);
  if (!m_file.empty())
    Load();
}

CPlayListHistoryEntry CPlayListHistoryEntry::FromPlayList(const CPlayList& playlist,
                                                          int position,
                                                          double seconds,
                                                          bool playedThrough)
{
  CPlayListHistoryEntry entry;
  entry.tracks.reserve(playlist.size());
  for (int i = 0; i < playlist.size(); ++i)
  {
    const std::shared_ptr<const CFileItem> item = playlist[i];
    CPlayListHistoryTrack track;
    track.path = item->GetDynPath();
    track.thumb = item->GetArt(ART::TYPE::THUMB);
    track.fanart = item->GetArt(ART::TYPE::FANART);
    if (item->HasMusicInfoTag())
    {
      const MUSIC_INFO::CMusicInfoTag* tag = item->GetMusicInfoTag();
      track.title = tag->GetTitle();
      track.artist = tag->GetAlbumArtistString();
      if (track.artist.empty())
        track.artist = tag->GetArtistString();
      track.album = tag->GetAlbum();
      track.duration = tag->GetDuration();
    }
    if (track.title.empty())
      track.title = item->GetLabel();
    entry.tracks.emplace_back(std::move(track));
  }
  if (entry.tracks.empty())
    return entry;

  entry.position = std::clamp(position, 0, static_cast<int>(entry.tracks.size()) - 1);
  entry.seconds = std::max(0.0, seconds);
  entry.playedThrough = playedThrough;
  entry.saved = CDateTime::GetCurrentDateTime();
  return entry;
}

void CPlayListHistory::Add(CPlayListHistoryEntry entry)
{
  if (entry.tracks.empty())
    return;
  if (!m_entries.empty() && entry.IsRepeatOf(m_entries.front()))
    return;

  m_entries.insert(m_entries.begin(), std::move(entry));
  if (m_entries.size() > MAX_ENTRIES)
    m_entries.resize(MAX_ENTRIES);
  Save();
}

bool CPlayListHistory::Load()
{
  if (!CFileUtils::Exists(m_file))
    return false;

  CXBMCTinyXML2 doc;
  if (!doc.LoadFile(m_file))
  {
    CLog::LogF(LOGERROR, "Unable to load {}", m_file);
    return false;
  }
  const tinyxml2::XMLElement* root = doc.RootElement();
  if (!root || !StringUtils::EqualsNoCase(root->Value(), "playlisthistory"))
  {
    CLog::LogF(LOGERROR, "{} is not a playlist history", m_file);
    return false;
  }

  for (const tinyxml2::XMLElement* node = root->FirstChildElement("playlist"); node;
       node = node->NextSiblingElement("playlist"))
  {
    CPlayListHistoryEntry entry;
    entry.position = node->IntAttribute("position", 0);
    entry.seconds = node->DoubleAttribute("seconds", 0.0);
    entry.playedThrough = node->BoolAttribute("playedthrough", false);
    entry.saved.SetFromDBDateTime(XMLUtils::GetAttribute(node, "saved"));
    for (const tinyxml2::XMLElement* trackNode = node->FirstChildElement("track"); trackNode;
         trackNode = trackNode->NextSiblingElement("track"))
    {
      CPlayListHistoryTrack track;
      XMLUtils::GetString(trackNode, "path", track.path);
      XMLUtils::GetString(trackNode, "title", track.title);
      XMLUtils::GetString(trackNode, "artist", track.artist);
      XMLUtils::GetString(trackNode, "album", track.album);
      XMLUtils::GetString(trackNode, "thumb", track.thumb);
      XMLUtils::GetString(trackNode, "fanart", track.fanart);
      XMLUtils::GetInt(trackNode, "duration", track.duration);
      if (!track.path.empty())
        entry.tracks.emplace_back(std::move(track));
    }
    if (!entry.tracks.empty())
      m_entries.emplace_back(std::move(entry));
  }
  if (m_entries.size() > MAX_ENTRIES)
    m_entries.resize(MAX_ENTRIES);
  return true;
}

bool CPlayListHistory::Save() const
{
  if (m_file.empty())
    return false;

  CXBMCTinyXML2 doc;
  tinyxml2::XMLElement* root = doc.NewElement("playlisthistory");
  root->SetAttribute("version", 1);
  doc.InsertEndChild(root);

  for (const CPlayListHistoryEntry& entry : m_entries)
  {
    tinyxml2::XMLElement* node = doc.NewElement("playlist");
    node->SetAttribute("saved", entry.saved.GetAsDBDateTime().c_str());
    node->SetAttribute("position", entry.position);
    node->SetAttribute("seconds", StringUtils::Format("{:.3f}", entry.seconds).c_str());
    node->SetAttribute("playedthrough", entry.playedThrough);
    for (const CPlayListHistoryTrack& track : entry.tracks)
    {
      tinyxml2::XMLElement* trackNode = doc.NewElement("track");
      XMLUtils::SetString(trackNode, "path", track.path);
      if (!track.title.empty())
        XMLUtils::SetString(trackNode, "title", track.title);
      if (!track.artist.empty())
        XMLUtils::SetString(trackNode, "artist", track.artist);
      if (!track.album.empty())
        XMLUtils::SetString(trackNode, "album", track.album);
      if (track.duration > 0)
        XMLUtils::SetInt(trackNode, "duration", track.duration);
      if (!track.thumb.empty())
        XMLUtils::SetString(trackNode, "thumb", track.thumb);
      if (!track.fanart.empty())
        XMLUtils::SetString(trackNode, "fanart", track.fanart);
      node->InsertEndChild(trackNode);
    }
    root->InsertEndChild(node);
  }

  // Written beside the file and renamed, so a quit mid-write leaves the old history.
  const std::string temp = m_file + ".tmp";
  if (!doc.SaveFile(temp) || !XFILE::CFile::Rename(temp, m_file))
  {
    CLog::LogF(LOGERROR, "Unable to save {}", m_file);
    XFILE::CFile::Delete(temp);
    return false;
  }
  return true;
}

} // namespace KODI::PLAYLIST
