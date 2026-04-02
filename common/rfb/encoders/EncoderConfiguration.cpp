/* Copyright (C) 2025 Kasm.  All Rights Reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */
#include "EncoderConfiguration.h"

namespace rfb {
    EncoderConfiguration::Range h264_quality_range = {0, 51};
    EncoderConfiguration::Range h264_allowed_range = {0, 50};
    EncoderConfiguration::Range av1_quality_range = {0, 63};
    EncoderConfiguration::Range av1_allowed_range = {1, 62};

    static inline std::array<EncoderConfiguration, static_cast<size_t>(KasmVideoEncoders::Encoder::unavailable) + 1>
        EncoderConfigurations = {
            // AV1
            // av1_vaapi
            EncoderConfiguration{av1_quality_range, av1_allowed_range, {}},
            // av1_ffmpeg_vaapi
            EncoderConfiguration{av1_quality_range, av1_allowed_range, {}, 0},
            // av1_nvenc
            EncoderConfiguration{av1_quality_range, av1_allowed_range, {18, 23, 28, 39, 62}, 0},
            // av1_software
            // profile AV_PROFILE_AV1_MAIN/FF_PROFILE_AV1_MAIN = 0
            EncoderConfiguration{av1_quality_range, av1_allowed_range, {18, 23, 28, 39, 62}, 0},

            // H.265
            // h265_vaapi
            // profile AV_PROFILE_HEVC_MAIN/FF_PROFILE_HEVC_MAIN = 1
            EncoderConfiguration{h264_quality_range, h264_quality_range, {18, 23, 28, 39, 51}},
            // h265_ffmpeg_vaapi
            EncoderConfiguration{h264_quality_range, h264_allowed_range, {18, 23, 28, 39, 50}, 1},
            // h265_nvenc
            EncoderConfiguration{h264_quality_range, h264_allowed_range, {18, 23, 28, 39, 50}, 1},

            // H.264
            // h264_vaapi
            EncoderConfiguration{h264_quality_range, h264_quality_range, {18, 23, 28, 33, 51}},
            // h264_ffmpeg_vaapi
            // profile  AV_PROFILE_H264_MAIN = 77
            EncoderConfiguration{h264_quality_range, h264_allowed_range, {12, 16, 25, 39, 50}, 77},
            // h264_nvenc
            EncoderConfiguration{h264_quality_range, h264_allowed_range, {18, 23, 28, 39, 50}, 77},
            // h264_software
            // profile AV_PROFILE_H264_CONSTRAINED_BASELINE/AV_PROFILE_H264_CONSTRAINED_BASELINE = (66|(1<<9))
            EncoderConfiguration{h264_quality_range, h264_allowed_range, {9, 18, 25, 39, 50}, (66 | 1 << 9)},

            EncoderConfiguration{}
    };

    // Compile-time check: EncoderConfigurations must match Encoder enum count (excluding unavailable)
    static_assert(EncoderConfigurations.size() == static_cast<size_t>(KasmVideoEncoders::Encoder::unavailable) + 1,
        "EncoderSettingsArray size must match KasmVideoEncoders::Encoder enum count.");

    const EncoderConfiguration &EncoderConfiguration::get_configuration(KasmVideoEncoders::Encoder encoder) {
        return EncoderConfigurations[static_cast<uint8_t>(encoder)];
    }
} // namespace rfb
