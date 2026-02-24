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
#include "EncoderProbe.h"
#include <fcntl.h>
#include <rfb/LogWriter.h>
#include <string>
#include <unistd.h>
#include <vector>
#include "KasmVideoConstants.h"
#include "KasmVideoEncoders.h"
#include <fstream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
}
#include "rfb/ffmpeg.h"

namespace rfb::video_encoders {
    static LogWriter vlog("EncoderProbe");

    EncoderProbe::EncoderProbe(FFmpeg &ffmpeg_, const std::vector<std::string_view> &parsed_encoders, const char *dri_node) :
        ffmpeg(ffmpeg_) {
        if (!ffmpeg.is_available()) {
            available_encoder_configs.emplace_back(KasmVideoEncoders::Encoder::unavailable);
        } else {
            auto debug_encoders = []<typename T, typename Alloc = std::allocator<T>>(const char *msg, const std::vector<T, Alloc> &encoders) {
                std::string encoder_names;

                for (const auto &encoder: encoders) {
                    std::string encoder_name;

                    if constexpr (std::is_same_v<T, KasmVideoEncoders::Encoder>)
                        encoder_name = KasmVideoEncoders::to_string(encoder);
                    else
                        encoder_name = KasmVideoEncoders::to_string(encoder.encoder);

                    encoder_names.append(encoder_name).append(" ");
                }

                if (!encoder_names.empty())
                    vlog.debug("%s: %s", msg, encoder_names.c_str());
            };

            const auto encoders = SupportedVideoEncoders::map_encoders(parsed_encoders);

            if (dri_node)
                vlog.debug("Dri node: %s", dri_node);

            debug_encoders("CLI-specified video codecs", encoders);

            static std::array<EncoderCandidate, 7> candidates = {
                {
                    EncoderCandidate{KasmVideoEncoders::Encoder::h264_nvenc, AV_CODEC_ID_H264, AV_HWDEVICE_TYPE_CUDA},
                    EncoderCandidate{KasmVideoEncoders::Encoder::h265_nvenc, AV_CODEC_ID_HEVC, AV_HWDEVICE_TYPE_CUDA},
                    EncoderCandidate{KasmVideoEncoders::Encoder::av1_nvenc, AV_CODEC_ID_AV1, AV_HWDEVICE_TYPE_CUDA},
                    EncoderCandidate{KasmVideoEncoders::Encoder::h264_ffmpeg_vaapi, AV_CODEC_ID_H264, AV_HWDEVICE_TYPE_VAAPI},
                    EncoderCandidate{KasmVideoEncoders::Encoder::h265_ffmpeg_vaapi, AV_CODEC_ID_HEVC, AV_HWDEVICE_TYPE_VAAPI},
                    // EncoderCandidate{KasmVideoEncoders::Encoder::av1_ffmpeg_vaapi, AV_CODEC_ID_AV1, AV_HWDEVICE_TYPE_VAAPI},
                    EncoderCandidate{KasmVideoEncoders::Encoder::h264_software, AV_CODEC_ID_H264, AV_HWDEVICE_TYPE_NONE},
                    EncoderCandidate{KasmVideoEncoders::Encoder::h265_software, AV_CODEC_ID_HEVC, AV_HWDEVICE_TYPE_NONE},
                    // EncoderCandidate{KasmVideoEncoders::Encoder::av1_software, AV_CODEC_ID_AV1, AV_HWDEVICE_TYPE_NONE},
                }
            };

            available_encoder_configs = probe(dri_node, candidates);

            debug_encoders("Available encoders", available_encoder_configs);

            available_encoder_configs = SupportedVideoEncoders::filter_available_encoders(encoders, available_encoder_configs);
            debug_encoders("Using CLI-specified video codecs (supported subset)", available_encoder_configs);
        }

        available_encoder_configs.shrink_to_fit();
        if (available_encoder_configs.empty())
            best_encoder = {KasmVideoEncoders::Encoder::unavailable};
        else
            best_encoder = available_encoder_configs.front();
    }

    bool EncoderProbe::dri_node_supports_hwdevice_type(const char *dri_node, int hw_type) {
        const auto path = std::format("/sys/class/drm/{}/device/vendor", dri_node);

        std::ifstream file{path};
        if (!file.is_open())
            return false;

        std::string s;
        file >> s;
        uint32_t vendor_id = std::stoul(s, nullptr, 0);

        switch (hw_type) {
            case AV_HWDEVICE_TYPE_CUDA:
                return vendor_id == 0x10DE;
            case AV_HWDEVICE_TYPE_VAAPI:
                return vendor_id == 0x8086 || vendor_id == 0x1002;
            default:
                return false;
        }
    }

    bool EncoderProbe::try_open_codec(const char *dri_node, const AVCodec *codec, const EncoderCandidate &candidate) {
        FFmpeg::BufferGuard hw_ctx_guard;
        AVBufferRef *hw_ctx{};

        auto err = ffmpeg.av_hwdevice_ctx_create(&hw_ctx, candidate.hw_type, dri_node, nullptr, 0);
        if (err != 0) {
            vlog.debug("Failed to create hw device context (%s). Error code: %d",
                ffmpeg.get_error_description(err).c_str(),
                err);
            return false;
        }

        hw_ctx_guard.reset(hw_ctx);

        FFmpeg::ContextGuard ctx_guard;

        auto *ctx = ffmpeg.avcodec_alloc_context3(codec);
        if (!ctx)
            return false;

        auto get_pix_fmt = [](int hw_type) {
            switch (hw_type) {
                case AV_HWDEVICE_TYPE_CUDA:
                    return AV_PIX_FMT_CUDA;
                case AV_HWDEVICE_TYPE_VAAPI:
                    return AV_PIX_FMT_VAAPI;
                default:
                    return AV_PIX_FMT_VAAPI;
            }
        };

        ctx_guard.reset(ctx);
        ctx->width = 1920;
        ctx->height = 1080;
        ctx->time_base = {1, 60};
        ctx->framerate = {60, 1};
        ctx->pix_fmt = get_pix_fmt(candidate.hw_type);
        ctx->hw_device_ctx = ffmpeg.av_buffer_ref(hw_ctx);

        FFmpeg::BufferGuard hw_frames_ref_guard;

        auto *hw_frames_ctx = ffmpeg.av_hwframe_ctx_alloc(hw_ctx_guard.get());
        if (!hw_frames_ctx) {
            vlog.debug("Failed to create HW frame context");
            return false;
        }

        auto *frames_ctx = reinterpret_cast<AVHWFramesContext *>(hw_frames_ctx->data);
        frames_ctx->format = ctx->pix_fmt;
        frames_ctx->sw_format = AV_PIX_FMT_NV12;
        frames_ctx->width = ctx->width;
        frames_ctx->height = ctx->height;

        if (err = ffmpeg.av_hwframe_ctx_init(hw_frames_ctx); err < 0) {
            vlog.debug("Failed to initialize HW frame context (%s). Error code: %d",
                ffmpeg.get_error_description(err).c_str(),
                err);
            return false;
        }

        hw_frames_ref_guard.reset(hw_frames_ctx);

        FFmpeg::av_buffer_unref(&ctx_guard->hw_frames_ctx);

        ctx_guard->hw_frames_ctx = ffmpeg.av_buffer_ref(hw_frames_ctx);
        if (!ctx_guard->hw_frames_ctx) {
            vlog.debug("Failed to create buffer reference");
            return false;
        }

        if (err = ffmpeg.avcodec_open2(ctx_guard.get(), codec, nullptr); err < 0) {
            vlog.debug("Failed to open codec (%s). Error code: %d", ffmpeg.get_error_description(err).c_str(), err);
            return false;
        }

        if (candidate.hw_type == AV_HWDEVICE_TYPE_VAAPI) {
            vlog.debug("DEBUG: Codec: %s\n", codec->name);

            const AVOption *opt{};
            while (opt = ffmpeg.av_opt_next(ctx_guard->priv_data, opt), opt) {
                vlog.debug("DEBUG: Option: %s.%s (help: %s)\n",
                    codec->name,
                    opt->name,
                    opt->help ? opt->help : "n/a");
            }
        }

        return err == 0;
    }

    KasmVideoEncoders::EncoderConfigs EncoderProbe::probe(
        const char *dri_node, std::span<EncoderCandidate> candidates) {
        KasmVideoEncoders::EncoderConfigs result{};
        for (const auto &encoder_candidate: candidates) {
            const auto *name = KasmVideoEncoders::to_string(encoder_candidate.encoder);
            vlog.debug("Probing %s...", name);
            const AVCodec *codec = ffmpeg.avcodec_find_encoder_by_name(name);
            if (!codec || codec->type != AVMEDIA_TYPE_VIDEO)
                continue;

            if (encoder_candidate.hw_type == AV_HWDEVICE_TYPE_NONE || !ffmpeg.av_codec_is_encoder(codec))
                continue;

            if (dri_node) {
                if (try_open_codec(dri_node, codec, encoder_candidate)) {
                    result.emplace_back(encoder_candidate.encoder, dri_node);
                }
            } else {
                vlog.debug("Trying to probe all available dri nodes");
                for (const auto *dri_node_path: dri_node_paths) {
                    vlog.debug("Probing dri node %s", dri_node_path);
                    if (!dri_node_supports_hwdevice_type(dri_node_path, encoder_candidate.hw_type)) {
                        vlog.debug("Skipping dri node %s", dri_node_path);
                        continue;
                    }

                    const auto dri_device = std::format("/dev/dri/{}", dri_node_path);
                    vlog.debug("Trying dri node %s", dri_device.c_str());
                    if (!try_open_codec(dri_device.c_str(), codec, encoder_candidate))
                        continue;

                    result.emplace_back(encoder_candidate.encoder, dri_device);
                }
            }
        }

        result.push_back({KasmVideoEncoders::Encoder::h264_software});
        result.push_back({KasmVideoEncoders::Encoder::h265_software});
        // result.push_back(KasmVideoEncoders::Encoder::av1_software);

        return result;
    }

    /*bool EncoderProbe::is_acceleration_available() {
        if (access(render_path, R_OK | W_OK) != 0)
            return false;

        const int fd = open(render_path, O_RDWR);
        if (fd < 0)
            return false;

        close(fd);

        return true;
    }*/
} // namespace rfb::video_encoders
