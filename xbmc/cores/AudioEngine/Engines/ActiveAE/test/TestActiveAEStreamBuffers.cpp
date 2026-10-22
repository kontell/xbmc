/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "cores/AudioEngine/Engines/ActiveAE/ActiveAEBuffer.h"
#include "cores/AudioEngine/Engines/ActiveAE/ActiveAEStream.h"

#include <deque>

#include <gtest/gtest.h>

using namespace ActiveAE;

namespace
{
constexpr unsigned int FRAMES = 1024;
constexpr unsigned int RATE = 48000;
constexpr double BUFFER = static_cast<double>(FRAMES) / RATE;

AEAudioFormat Format()
{
  AEAudioFormat format;
  format.m_dataFormat = AE_FMT_FLOATP;
  format.m_sampleRate = RATE;
  format.m_channelLayout = AE_CH_LAYOUT_2_0;
  format.m_frames = FRAMES;
  return format;
}

// Puts full buffers in a queue, where the stage before would leave them
void Queue(CActiveAEBufferPool& pool, std::deque<CSampleBuffer*>& queue, int buffers)
{
  for (int i = 0; i < buffers; ++i)
  {
    CSampleBuffer* buffer = pool.GetFreeBuffer();
    ASSERT_NE(buffer, nullptr);
    buffer->pkt->nb_samples = FRAMES;
    queue.push_back(buffer);
  }
}
} // namespace

TEST(TestActiveAEStreamBuffers, AtempoDelayIsInTheTimeOfItsInput)
{
  for (const float tempo : {2.0f, 1.5f, 0.5f})
  {
    CActiveAEBufferPool source(Format());
    ASSERT_TRUE(source.Create(400));
    CActiveAEBufferPoolAtempo atempo(Format());
    ASSERT_TRUE(atempo.Create(400));
    atempo.SetTempo(tempo);

    Queue(source, atempo.m_inputSamples, 2);
    Queue(source, atempo.m_outputSamples, 3);

    // each buffer of output stands for tempo buffers of input
    EXPECT_NEAR(atempo.GetDelay(), (2 + 3 * static_cast<double>(tempo)) * BUFFER, 1e-5)
        << "tempo " << tempo;
    // and has taken the tempo
    EXPECT_NEAR(atempo.GetRateDelay(), 3 * BUFFER, 1e-5) << "tempo " << tempo;

    atempo.Flush();
  }
}

TEST(TestActiveAEStreamBuffers, DelayIsInTheStreamsTime)
{
  // ratios beyond the atempo threshold, which the atempo stage plays
  for (const double rr : {0.5, 2.0 / 3.0, 2.0})
  {
    CActiveAEBufferPool source(Format());
    ASSERT_TRUE(source.Create(400));
    CActiveAEStreamBuffers buffers(Format(), Format(), AE_QUALITY_MID);
    ASSERT_TRUE(buffers.Create(400, false, false));
    buffers.SetRR(rr, 0.02);

    Queue(source, buffers.m_inputSamples, 2);
    Queue(source, buffers.m_outputSamples, 3);

    // The engine plays the delay at the ratio: the input at it, and the output in its own length,
    // having taken the ratio already.
    EXPECT_NEAR(static_cast<double>(buffers.GetDelay()) * buffers.GetRR(), (2 * rr + 3) * BUFFER,
                1e-5)
        << "ratio " << rr;
    EXPECT_NEAR(buffers.GetRateDelay(), 3 * BUFFER, 1e-5) << "ratio " << rr;

    buffers.Flush();
  }
}
