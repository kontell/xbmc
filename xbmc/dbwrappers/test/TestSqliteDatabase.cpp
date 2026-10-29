/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "dbwrappers/sqlitedataset.h"
#include "filesystem/File.h"
#include "filesystem/SpecialProtocol.h"

#include <memory>
#include <string>

#include <gtest/gtest.h>

TEST(TestSqliteDatabase, ConnectionUsesWriteAheadLogging)
{
  const std::string host{CSpecialProtocol::TranslatePath("special://temp/")};
  const std::string name{"TestSqliteJournalMode.db"};
  dbiplus::SqliteDatabase db;
  db.setHostName(host.c_str());
  db.setDatabase(name.c_str());
  ASSERT_EQ(static_cast<int>(dbiplus::DB_CONNECTION_OK), db.connect(true));
  db.postconnect();

  std::string mode;
  {
    const std::unique_ptr<dbiplus::Dataset> ds{db.CreateDataset()};
    ASSERT_TRUE(ds->query("SELECT * FROM pragma_journal_mode"));
    ASSERT_FALSE(ds->eof());
    mode = ds->fv(0).get_asString();
    ds->close();
  }
  db.disconnect();
  XFILE::CFile::Delete(host + name);

  EXPECT_EQ("wal", mode);
}
