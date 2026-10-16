/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "platform/Filesystem.h"
#include "filesystem/SpecialProtocol.h"
#include "utils/URIUtils.h"

#if defined(TARGET_LINUX) || defined(TARGET_WASM)
#include <sys/statvfs.h>
#if defined(TARGET_LINUX)
#include <sys/vfs.h>
#endif
#elif defined(TARGET_DARWIN) || defined(TARGET_FREEBSD)
#include <sys/param.h>
#include <sys/mount.h>
#elif defined(TARGET_ANDROID)
#include <sys/statfs.h>
#endif

#include <cstdint>
#include <cstdlib>
#include <limits.h>
#include <string.h>
#include <unistd.h>

namespace KODI
{
namespace PLATFORM
{
namespace FILESYSTEM
{

space_info space(const std::string& path, std::error_code& ec)
{
  ec.clear();
  space_info sp;
#if defined(TARGET_LINUX) || defined(TARGET_WASM)
  struct statvfs64 fsInfo;
  auto result = statvfs64(CSpecialProtocol::TranslatePath(path).c_str(), &fsInfo);
#else
  struct statfs fsInfo;
  // is 64-bit on android and darwin (10.6SDK + any iOS)
  auto result = statfs(CSpecialProtocol::TranslatePath(path).c_str(), &fsInfo);
#endif

  if (result != 0)
  {
    ec.assign(result, std::system_category());
    sp.available = static_cast<uintmax_t>(-1);
    sp.capacity = static_cast<uintmax_t>(-1);
    sp.free = static_cast<uintmax_t>(-1);
    return sp;
  }
  sp.available = static_cast<uintmax_t>(fsInfo.f_bavail * fsInfo.f_bsize);
  sp.capacity = static_cast<uintmax_t>(fsInfo.f_blocks * fsInfo.f_bsize);
  sp.free = static_cast<uintmax_t>(fsInfo.f_bfree * fsInfo.f_bsize);

  return sp;
}

bool is_network_filesystem(const std::string& path)
{
#if defined(TARGET_LINUX) || defined(TARGET_ANDROID)
  struct statfs fsInfo;
  if (statfs(CSpecialProtocol::TranslatePath(path).c_str(), &fsInfo) != 0)
    return false;

  // The magic numbers of linux/magic.h, written out because older headers lack some
  switch (static_cast<uint32_t>(fsInfo.f_type))
  {
    case 0x6969u: // NFS
    case 0x517Bu: // SMB
    case 0xFF534D42u: // CIFS
    case 0xFE534D42u: // SMB2
    case 0x01021997u: // 9P
    case 0x5346414Fu: // AFS
    case 0x73757245u: // Coda
      return true;
    default:
      return false;
  }
#elif defined(TARGET_DARWIN) || defined(TARGET_FREEBSD)
  struct statfs fsInfo;
  return statfs(CSpecialProtocol::TranslatePath(path).c_str(), &fsInfo) == 0 &&
         (fsInfo.f_flags & MNT_LOCAL) == 0;
#else
  return false;
#endif
}

std::string temp_directory_path(std::error_code &ec)
{
  ec.clear();

  auto result = getenv("TMPDIR");
  if (result)
    return URIUtils::AppendSlash(result);

#if defined(TARGET_FREEBSD)
  // Use /var/tmp on FreeBSD as /tmp can be created in RAM
  // This can be limiting for decompressing archives with large compressed files
  return "/var/tmp/";
#else
  return "/tmp/";
#endif
}

std::string create_temp_directory(std::error_code &ec)
{
  char buf[PATH_MAX];

  auto path = temp_directory_path(ec);

  strncpy(buf, (path + "xbmctempXXXXXX").c_str(), sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  auto tmp = mkdtemp(buf);
  if (!tmp)
  {
    ec.assign(errno, std::system_category());
    return std::string();
  }

  ec.clear();
  return std::string(tmp);
}

std::string temp_file_path(const std::string& suffix, std::error_code& ec)
{
  char tmp[PATH_MAX];

  auto tempPath = create_temp_directory(ec);
  if (ec)
    return std::string();

  tempPath = URIUtils::AddFileToFolder(tempPath, "xbmctempfileXXXXXX" + suffix);
  if (tempPath.length() >= PATH_MAX)
  {
    ec.assign(EOVERFLOW, std::system_category());
    return std::string();
  }

  strncpy(tmp, tempPath.c_str(), sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';

  auto fd = mkstemps(tmp, suffix.length());
  if (fd < 0)
  {
    ec.assign(errno, std::system_category());
    return std::string();
  }

  close(fd);

  ec.clear();
  return std::string(tmp);
}

}
}
}
