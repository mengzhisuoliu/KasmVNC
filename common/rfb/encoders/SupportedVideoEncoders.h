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
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>
#include "KasmVideoEncoders.h"

namespace rfb {
    struct SupportedVideoEncoders {
        enum class Codecs : uint8_t
        {
            h264,
            h264_vaapi,
            h264_nvenc,
            avc,
            avc_vaapi,
            avc_nvenc,

            h265,
            h265_vaapi,
            h265_nvenc,
            hevc,
            hevc_vaapi,
            hevc_nvenc,

            av1,
            av1_vaapi,
            av1_nvenc,
            auto_detect,
            unavailable // Keep this as the last entry - used for compile-time size checks
        };

        static constexpr auto MappedCodecs = std::to_array<KasmVideoEncoders::Encoder>({KasmVideoEncoders::Encoder::h264_software,
            KasmVideoEncoders::Encoder::h264_ffmpeg_vaapi,
            KasmVideoEncoders::Encoder::h264_nvenc,
            KasmVideoEncoders::Encoder::h264_software,
            KasmVideoEncoders::Encoder::h264_ffmpeg_vaapi,
            KasmVideoEncoders::Encoder::h264_nvenc,

            KasmVideoEncoders::Encoder::h265_software,
            KasmVideoEncoders::Encoder::h265_ffmpeg_vaapi,
            KasmVideoEncoders::Encoder::h265_nvenc,
            KasmVideoEncoders::Encoder::h265_software,
            KasmVideoEncoders::Encoder::h265_ffmpeg_vaapi,
            KasmVideoEncoders::Encoder::h265_nvenc,

            KasmVideoEncoders::Encoder::av1_software,
            KasmVideoEncoders::Encoder::av1_ffmpeg_vaapi,
            KasmVideoEncoders::Encoder::av1_nvenc,
            KasmVideoEncoders::Encoder::h264_software,

            KasmVideoEncoders::Encoder::unavailable});

        static_assert(
            MappedCodecs.size() == static_cast<size_t>(Codecs::unavailable) + 1, "MappedCodecs array size must match Codecs enum count");

        // Compile-time check: Encoder must match Codec enum count (excluding unavailable)
        static_assert(static_cast<size_t>(KasmVideoEncoders::Encoder::unavailable) + 4 == static_cast<size_t>(SupportedVideoEncoders::Codecs::unavailable),
            "Encoder enum count must match SupportedVideoEncoders::Codecs enum count - 4.");


        static inline auto CodecNames = std::to_array<std::string_view>({"h264",
            "h264_vaapi",
            "h264_nvenc",
            "avc",
            "avc_vaapi",
            "avc_nvenc",

            "h265",
            "h265_vaapi",
            "h265_nvenc",
            "hevc",
            "hevc_vaapi",
            "hevc_nvenc",

            "av1",
            "av1_vaapi",
            "av1_nvenc",

            "auto"});

        static_assert(CodecNames.size() == static_cast<size_t>(Codecs::unavailable), "CodecNames array size must match Codecs enum count");

        static std::string_view to_string(Codecs codec) {
            return CodecNames[static_cast<uint8_t>(codec)];
        }

        static bool is_supported(std::string_view codec) {
            if (codec.empty())
                return false;

            for (const auto supported_codec: CodecNames)
                if (supported_codec == codec)
                    return true;

            return false;
        }

        static auto get_codec(std::string_view codec) {
            for (auto codec_impl: enum_range(Codecs::h264, Codecs::auto_detect)) {
                if (to_string(codec_impl) == codec)
                    return codec_impl;
            }

            return Codecs::unavailable;
        }

        static constexpr auto map_encoder(Codecs impl) {
            return MappedCodecs[static_cast<uint8_t>(impl)];
        }

        static std::vector<std::string_view> parse(const std::string_view codecs) {
            std::vector<std::string_view> result;

            if (codecs.empty())
                return {};

            size_t pos{};
            size_t start{};

            while (pos < codecs.size()) {
                pos = codecs.find_first_of(',', pos);
                if (pos == std::string_view::npos)
                    pos = codecs.size();

                result.push_back(codecs.substr(start, pos - start));

                start = ++pos;
            }

            return result;
        }

        static KasmVideoEncoders::Encoders map_encoders(const std::vector<std::string_view> &codecs) {
            KasmVideoEncoders::Encoders result;

            if (codecs.empty())
                return {};

            for (const auto codec_name: codecs) {
                const auto codec = get_codec(codec_name);

                switch (codec) {
                    case Codecs::auto_detect:
                        if (!result.empty())
                            result.clear();

                        result.push_back(map_encoder(Codecs::av1_nvenc));
                        result.push_back(map_encoder(Codecs::av1_vaapi));
                        result.push_back(map_encoder(Codecs::av1));
                        result.push_back(map_encoder(Codecs::h265_nvenc));
                        result.push_back(map_encoder(Codecs::h265_vaapi));
                        result.push_back(map_encoder(Codecs::h265));
                        result.push_back(map_encoder(Codecs::h264_nvenc));
                        result.push_back(map_encoder(Codecs::h264_vaapi));
                        result.push_back(map_encoder(Codecs::h264));

                        return result;
                    default:
                    {
                        const auto encoder = map_encoder(codec);
                        if (std::find(result.begin(), result.end(), encoder) == result.end())
                            result.push_back(encoder);
                    }
                }
            }

            return result;
        }

        static KasmVideoEncoders::EncoderConfigs filter_available_encoders(
            const KasmVideoEncoders::Encoders &encoders, const KasmVideoEncoders::EncoderConfigs &available) {
            KasmVideoEncoders::EncoderConfigs result;

            for (const auto &encoder_config: available) {
                if (std::ranges::find(encoders.begin(), encoders.end(), encoder_config.encoder) != encoders.end())
                    result.push_back(encoder_config);
            }

            return result;
        }
    };
} // namespace rfb
