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
    static inline std::array<EncoderConfiguration, static_cast<size_t>(KasmVideoEncoders::Encoder::unavailable) + 1> EncoderConfigurations =
        {
            // AV1
            // av1_vaapi
            EncoderConfiguration{0, 0, {}},
            // av1_ffmpeg_vaapi
            EncoderConfiguration{0, 0, {}},
            // av1_nvenc
            EncoderConfiguration{0, 63, {18, 23, 28, 39, 63}},
            // av1_software
            EncoderConfiguration{0, 0, {}},

            // H.265
            // h265_vaapi
            EncoderConfiguration{0, 51, {18, 23, 28, 39, 51}},
            // h265_ffmpeg_vaapi
            EncoderConfiguration{0, 51, {18, 23, 28, 39, 51}},
            // h265_nvenc
            EncoderConfiguration{0, 51, {18, 23, 28, 39, 51}},
            // h265_software
            EncoderConfiguration{0, 50, {18, 23, 28, 39, 50}},

            // H.264
            // h264_vaapi
            EncoderConfiguration{0, 51, {18, 23, 28, 33, 51}},
            // h264_ffmpeg_vaapi
            EncoderConfiguration{0, 51, {12, 16, 25, 39, 51}},
            // h264_nvenc
            EncoderConfiguration{0, 51, {18, 23, 28, 39, 51}},
            // h264_software
            EncoderConfiguration{0, 50, {9, 18, 25, 39, 50}},

            EncoderConfiguration{}
    };

    // Compile-time check: EncoderConfigurations must match Encoder enum count (excluding unavailable)
    static_assert(EncoderConfigurations.size() == static_cast<size_t>(KasmVideoEncoders::Encoder::unavailable) + 1,
        "EncoderSettingsArray size must match KasmVideoEncoders::Encoder enum count.");

    const EncoderConfiguration &EncoderConfiguration::get_configuration(KasmVideoEncoders::Encoder encoder) {
        return EncoderConfigurations[static_cast<uint8_t>(encoder)];
    }
} // namespace rfb
