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

#include <array>
#include <cstdint>
#include <rfb/encodings.h>
#include <string>
#include <type_traits>
#include <vector>
#include "KasmVideoConstants.h"

template<typename E>
class EnumRange {
public:
    using val_t = std::underlying_type_t<E>;

    class EnumIterator {
        val_t value;

    public:
        explicit EnumIterator(E v) :
            value(static_cast<std::underlying_type_t<E>>(v)) {}
        E operator*() const {
            return static_cast<E>(value);
        }
        EnumIterator &operator++() {
            ++value;
            return *this;
        }
        bool operator!=(const EnumIterator &other) const {
            return value != other.value;
        }
    };

    EnumRange(E begin, E end) :
        begin_iter(EnumIterator(begin)),
        end_iter(++EnumIterator(end)) {}
    [[nodiscard]] EnumIterator begin() const {
        return begin_iter;
    }
    [[nodiscard]] EnumIterator end() const {
        return end_iter;
    }
    EnumIterator begin() {
        return begin_iter;
    }
    EnumIterator end() {
        return end_iter;
    }

private:
    EnumIterator begin_iter;
    EnumIterator end_iter;
};

template<typename T>
auto enum_range(T begin, T end) {
    return EnumRange<T>(begin, end);
}

namespace rfb {
    struct KasmVideoEncoders {
        // Codecs are ordered by preferred usage quality
        enum class Encoder : uint8_t
        {
            av1_vaapi,
            av1_ffmpeg_vaapi,
            av1_nvenc,
            av1_software,

            h265_vaapi, // h265
            h265_ffmpeg_vaapi,
            h265_nvenc,
            h265_software,

            h264_vaapi,
            h264_ffmpeg_vaapi,
            h264_nvenc,
            h264_software,

            unavailable // Keep this as the last entry - used for compile-time size checks
        };

        struct EncoderConfig {
            Encoder encoder{};
            std::string dri_path{};
        };

        using Encoders = std::vector<Encoder>;
        using EncoderConfigs = std::vector<EncoderConfig>;

        static inline auto EncoderNames = std::to_array<const char *>({"av1_vaapi",
            "av1_vaapi",
            "av1_nvenc",
            "libsvtav1",

            "hevc_vaapi",
            "hevc_vaapi",
            "hevc_nvenc",
            "libx265",

            "h264_vaapi",
            "h264_vaapi",
            "h264_nvenc",
            "libx264",
            "unavailable"});

        static_assert(EncoderNames.size() == static_cast<size_t>(Encoder::unavailable) + 1, "EncoderNames array size must match Encoder enum count.");

        static inline auto Encodings = std::to_array<int>({pseudoEncodingStreamingModeAV1VAAPI,
            pseudoEncodingStreamingModeAV1VAAPI,
            pseudoEncodingStreamingModeAV1NVENC,
            pseudoEncodingStreamingModeAV1SW,

            pseudoEncodingStreamingModeHEVCVAAPI,
            pseudoEncodingStreamingModeHEVCVAAPI,
            pseudoEncodingStreamingModeHEVCNVENC,
            pseudoEncodingStreamingModeHEVCSW,

            pseudoEncodingStreamingModeAVCVAAPI,
            pseudoEncodingStreamingModeAVCVAAPI,
            pseudoEncodingStreamingModeAVCNVENC,
            pseudoEncodingStreamingModeAVCSW,

            pseudoEncodingStreamingModeJpegWebp});

        static_assert(Encodings.size() == static_cast<size_t>(Encoder::unavailable) + 1, "Encodings array size must match Encoder enum count. ");

        static bool is_accelerated(Encoder encoder) {
            return encoder != Encoder::h264_software && encoder != Encoder::h265_software && encoder != Encoder::av1_software;
        }

        static auto to_string(Encoder encoder) {
            return EncoderNames[static_cast<uint8_t>(encoder)];
        }

        static int to_encoding(Encoder encoder) {
            return Encodings[static_cast<uint8_t>(encoder)];
        }

        static Encoder from_encoding(int encoding) {
            for (auto encoder: enum_range(Encoder::av1_vaapi, Encoder::unavailable)) {
                if (to_encoding(encoder) == encoding) {
                    switch (encoder) {
                        case Encoder::av1_vaapi:
                            return Encoder::av1_ffmpeg_vaapi;
                        case Encoder::h265_vaapi:
                            return Encoder::h265_ffmpeg_vaapi;
                        case Encoder::h264_vaapi:
                            return Encoder::h264_ffmpeg_vaapi;
                        default:
                            return encoder;
                    }
                }
            }

            return Encoder::unavailable;
        }

        static unsigned int to_msg_id(Encoder encoder) {
            static_assert(11 == static_cast<size_t>(Encoder::unavailable), "The switch case needs to be exte");
            switch (encoder) {
                case Encoder::av1_vaapi:
                case Encoder::av1_ffmpeg_vaapi:
                case Encoder::av1_nvenc:
                case Encoder::av1_software:
                    return kasmVideoAV1;
                case Encoder::h265_vaapi: // h265
                case Encoder::h265_ffmpeg_vaapi:
                case Encoder::h265_nvenc:
                case Encoder::h265_software:
                    return kasmVideoH265;
                case Encoder::h264_vaapi:
                case Encoder::h264_ffmpeg_vaapi:
                case Encoder::h264_nvenc:
                case Encoder::h264_software:
                    return kasmVideoH264;
                default:
                    assert(false);
            }
        }

        static int32_t to_streaming_mode(Encoder encoder) {
            switch (encoder) {
                case Encoder::av1_vaapi:
                case Encoder::av1_ffmpeg_vaapi:
                case Encoder::av1_nvenc:
                case Encoder::av1_software:
                    return pseudoEncodingStreamingModeAV1;
                case Encoder::h265_vaapi: // h265
                case Encoder::h265_ffmpeg_vaapi:
                case Encoder::h265_nvenc:
                case Encoder::h265_software:
                    return pseudoEncodingStreamingModeHEVC;
                case Encoder::h264_vaapi:
                case Encoder::h264_ffmpeg_vaapi:
                case Encoder::h264_nvenc:
                case Encoder::h264_software:
                    return pseudoEncodingStreamingModeAVC;
                default:
                    return pseudoEncodingStreamingModeJpegWebp;
            }
        }
    };
} // namespace rfb
