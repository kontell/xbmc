/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "cores/AudioEngine/Engines/ActiveAE/ActiveAEResampleFFMPEG.h"

#include <vector>

#include <gtest/gtest.h>

extern "C"
{
#include <libavutil/channel_layout.h>
}

using namespace ActiveAE;

namespace
{
// a sink's period at 44.1 kHz, where 1/1.001 is 0.46 of a sample to compensate for each buffer
constexpr int FRAMES = 464;
constexpr int BUFFERS = 1000;
// buffers taken before counting, so that the resampler's own delay is behind it
constexpr int PRIMING = 100;

// How many samples fewer than it takes the resampler gives back over the counted buffers
double SamplesLeftOut(double ratio)
{
  CActiveAEResampleFFMPEG resampler;
  const SampleConfig config{AV_SAMPLE_FMT_FLTP, AV_CH_LAYOUT_STEREO, 2, 44100, 32, 0};
  EXPECT_TRUE(
      resampler.Init(config, config, false, false, 1.0, nullptr, AE_QUALITY_MID, true, 1.0f));

  std::vector<float> left(FRAMES, 0.25f);
  std::vector<float> right(FRAMES, -0.25f);
  std::vector<float> outLeft(2 * FRAMES);
  std::vector<float> outRight(2 * FRAMES);
  uint8_t* src[] = {reinterpret_cast<uint8_t*>(left.data()),
                    reinterpret_cast<uint8_t*>(right.data())};
  uint8_t* dst[] = {reinterpret_cast<uint8_t*>(outLeft.data()),
                    reinterpret_cast<uint8_t*>(outRight.data())};

  int64_t leftOut = 0;
  for (int i = 0; i < BUFFERS; ++i)
  {
    const int got = resampler.Resample(dst, 2 * FRAMES, src, FRAMES, ratio);
    EXPECT_GE(got, 0);
    if (i >= PRIMING)
      leftOut += FRAMES - got;
  }
  return static_cast<double>(leftOut);
}
} // namespace

TEST(TestActiveAEResampleFFMPEG, CompensationKeepsTheFractionOfASample)
{
  constexpr double counted = static_cast<double>(BUFFERS - PRIMING) * FRAMES;

  // faster: 0.46 of a sample out of each buffer, which truncated to a whole sample was none
  const double faster = 1.0 / 1.001;
  EXPECT_NEAR(SamplesLeftOut(faster), counted * (1.0 - faster), 2.0);

  // slower: as much put in
  const double slower = 1.0 / 0.999;
  EXPECT_NEAR(SamplesLeftOut(slower), counted * (1.0 - slower), 2.0);

  // and nothing at the ratio the stream came in at
  EXPECT_NEAR(SamplesLeftOut(1.0), 0.0, 0.5);
}
