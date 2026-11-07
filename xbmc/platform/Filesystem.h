/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <cstdint>
#include <string>
#include <system_error>
namespace KODI
{
namespace PLATFORM
{
namespace FILESYSTEM
{
struct space_info {
  std::uintmax_t capacity;
  std::uintmax_t free;
  std::uintmax_t available;
};

space_info space(const std::string &path, std::error_code &ec);

/*!
 * \brief Whether a directory is on a filesystem served over the network, such as NFS or SMB.
 * \param path The directory to look at.
 * \return true if it is; false if it is local or can't be told.
 */
bool is_network_filesystem(const std::string& path);

std::string temp_directory_path(std::error_code &ec);
std::string create_temp_directory(std::error_code &ec);
std::string temp_file_path(const std::string& suffix, std::error_code& ec);
}
}
}
