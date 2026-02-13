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
#include "FFMPEGHWEncoder.h"

#include <fmt/format.h>
#include <rfb/ServerCore.h>

#include "EncoderProbe.h"
#include "rfb/LogWriter.h"

#include "EncoderConfiguration.h"
#include "KasmVideoConstants.h"
#include "rfb/encodings.h"
#include <rfb/encoders/utils.h>
#if defined LIBYUV_CONVERSION
#include <libyuv.h>
#endif

static rfb::LogWriter vlog("FFMPEGHWEncoder");

namespace rfb {
    template<AVHWDeviceType HWDeviceType, AVPixelFormat AVPixFmt>
    FFMPEGHWEncoder<HWDeviceType, AVPixFmt>::FFMPEGHWEncoder(Screen layout_, const FFmpeg &ffmpeg_, SConnection *conn, KasmVideoEncoders::Encoder encoder_,
        const char *dri_node_, VideoEncoderParams params) :
        VideoEncoder(layout_.id, conn), layout(layout_),
        ffmpeg(ffmpeg_), encoder(encoder_), current_params(params), msg_codec_id(KasmVideoEncoders::to_msg_id(encoder)),
        dri_node(dri_node_) {
        AVBufferRef *hw_device_ctx{};
        int err{};

        vlog.debug("Constructor: HWDeviceType=%d, AVPixFmt=%d, encoder=%s, dri_node=%s",
                   HWDeviceType, AVPixFmt, KasmVideoEncoders::to_string(encoder),
                   dri_node_ ? dri_node_ : "null");

        if (err = ffmpeg.av_hwdevice_ctx_create(&hw_device_ctx, HWDeviceType, dri_node_, nullptr, 0); err < 0) {
            throw std::runtime_error(fmt::format("Failed to create hw device context {}", ffmpeg.get_error_description(err)));
        }

        hw_device_ctx_guard.reset(hw_device_ctx);
        const auto *enc_name = KasmVideoEncoders::to_string(encoder);
        vlog.debug("Looking for encoder: %s", enc_name);
        codec = ffmpeg.avcodec_find_encoder_by_name(enc_name);
        if (!codec)
            throw std::runtime_error(fmt::format("Could not find {} encoder", enc_name));

        auto *frame = ffmpeg.av_frame_alloc();
        if (!frame)
            throw std::runtime_error("Cannot allocate AVFrame");

        sw_frame_guard.reset(frame);

        auto *pkt = ffmpeg.av_packet_alloc();
        if (!pkt) {
            throw std::runtime_error("Could not allocate packet");
        }
        pkt_guard.reset(pkt);
    }

    template<AVHWDeviceType HWDeviceType, AVPixelFormat AVPixFmt>
    bool FFMPEGHWEncoder<HWDeviceType, AVPixFmt>::init(int width, int height, VideoEncoderParams params) {
        current_params = params;
        AVHWFramesContext *frames_ctx{};
        int err{};

        vlog.debug("FRAME RESIZE (%d, %d): RATE: %d, GOP: %d, QUALITY: %d", width, height, current_params.frame_rate, current_params.group_of_picture, current_params.quality);

        auto *ctx = ffmpeg.avcodec_alloc_context3(codec);
        if (!ctx) {
            vlog.error("Cannot allocate AVCodecContext");
            return false;
        }
        ctx_guard.reset(ctx);

        ctx->time_base = {1, current_params.frame_rate};
        ctx->framerate = {current_params.frame_rate, 1};
        ctx->gop_size = current_params.group_of_picture; // interval between I-frames
        ctx->max_b_frames = 0; // No B-frames for immediate output
        ctx->pix_fmt = AVPixFmt;
        ctx_guard->width = current_params.width;
        ctx_guard->height = current_params.height;
        ctx_guard->coded_width = current_params.width;
        ctx_guard->coded_height = current_params.height;
        ctx->delay = 0;
        ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;

        vlog.debug("Encoder context config: pix_fmt=%d, width=%d, height=%d, coded_width=%d, coded_height=%d, framerate=%d, gop=%d, quality=%d",
                   ctx->pix_fmt, ctx->width, ctx->height, ctx->coded_width, ctx->coded_height,
                   current_params.frame_rate, current_params.group_of_picture, current_params.quality);

        if constexpr (HWDeviceType == AV_HWDEVICE_TYPE_CUDA && AVPixFmt == AV_PIX_FMT_CUDA) {
            // NVENC low-latency settings
            if (ffmpeg.av_opt_set(ctx->priv_data, "preset", "p1", 0) < 0) {
                vlog.info("Cannot set preset");
            }

            if (ffmpeg.av_opt_set(ctx->priv_data, "tune", "ull", 0) < 0) {
                vlog.info("Cannot set tune");
            }

            if (ffmpeg.av_opt_set(ctx->priv_data, "rc", "vbr_hq", 0) < 0) {
                vlog.info("Cannot set rc");
            }

            if (ffmpeg.av_opt_set(ctx->priv_data, "zerolatency", "1", 0) < 0) {
                vlog.info("Cannot set zerolatency");
            }

            if (ffmpeg.av_opt_set(ctx->priv_data, "delay", "0", 0) < 0) {
                vlog.info("Cannot set delay");
            }

            if (ffmpeg.av_opt_set(ctx->priv_data, "rc-lookahead", "0", 0) < 0) {
                vlog.info("Cannot set rc-lookahead");
            }
        } else {
            if (ffmpeg.av_opt_set(ctx->priv_data, "async_depth", "1", 0) < 0) {
                vlog.info("Cannot set async_depth");
            }

            if (ffmpeg.av_opt_set(ctx->priv_data, "rc_mode", "CQP", 0) < 0) {
                vlog.info("Cannot set rc_mode");
            }
        }

        if (ffmpeg.av_opt_set_int(ctx->priv_data, "qp", current_params.quality, 0) < 0) {
            vlog.info("Cannot set qp");
        }

        auto *hw_frames_ctx = ffmpeg.av_hwframe_ctx_alloc(hw_device_ctx_guard.get());
        if (!hw_frames_ctx) {
            vlog.error("Failed to create HW frame context");
            return false;
        }

        hw_frames_ref_guard.reset(hw_frames_ctx);

        frames_ctx = reinterpret_cast<AVHWFramesContext *>(hw_frames_ctx->data);
        frames_ctx->format = AVPixFmt;
        frames_ctx->sw_format = AV_PIX_FMT_NV12;
        frames_ctx->width = current_params.width;
        frames_ctx->height = current_params.height;
        frames_ctx->initial_pool_size = 20;

        vlog.debug("HW frame context config: format=%d (AVPixFmt template), sw_format=%d (NV12), width=%d, height=%d, pool_size=20",
                   frames_ctx->format, frames_ctx->sw_format, frames_ctx->width, frames_ctx->height);

        if (err = ffmpeg.av_hwframe_ctx_init(hw_frames_ctx); err < 0) {
            vlog.error("Failed to initialize HW frame context (%s). Error code: %d", ffmpeg.get_error_description(err).c_str(), err);
            return false;
        }
        vlog.debug("HW frame context initialized successfully");

        FFmpeg::av_buffer_unref(&ctx_guard->hw_frames_ctx);

        ctx_guard->hw_frames_ctx = ffmpeg.av_buffer_ref(hw_frames_ctx);
        if (!ctx_guard->hw_frames_ctx) {
            vlog.error("Failed to create buffer reference");
            return false;
        }

        auto *frame = ffmpeg.av_frame_alloc();
        if (!frame) {
            vlog.error("Cannot allocate AVFrame");
            return false;
        }
        sw_frame_guard.reset(frame);

        frame->format = AV_PIX_FMT_NV12;
        frame->width = params.width;
        frame->height = params.height;
        frame->pict_type = AV_PICTURE_TYPE_I;

        vlog.debug("SW frame config: format=%d (NV12), width=%d, height=%d", frame->format, frame->width, frame->height);

        if (ffmpeg.av_frame_get_buffer(frame, 0) < 0) {
            vlog.error("Could not allocate sw-frame data");
            return false;
        }

        vlog.debug("SW frame allocated: linesize[0]=%d, linesize[1]=%d", frame->linesize[0], frame->linesize[1]);

        auto *hw_frame = ffmpeg.av_frame_alloc();
        if (!hw_frame) {
            vlog.error("Cannot allocate hw AVFrame");
            return false;
        }
        hw_frame_guard.reset(hw_frame);

        if (err = ffmpeg.av_hwframe_get_buffer(hw_frames_ctx, hw_frame, 0); err < 0) {
            vlog.error("Could not allocate hw-frame data (%s). Error code: %d", ffmpeg.get_error_description(err).c_str(), err);
            return false;
        }

        vlog.debug("Opening codec: %s", codec->name);
        if (err = ffmpeg.avcodec_open2(ctx_guard.get(), codec, nullptr); err < 0) {
            vlog.error("Failed to open codec (%s). Error code: %d", ffmpeg.get_error_description(err).c_str(), err);
            return false;
        }
        vlog.debug("Codec opened successfully");
#if defined(FFMPEG_FILTER)
        const char* filters = "format=nv12,hwupload";  // Or "scale_vaapi=format=nv12" for explicit VAAPI scaler
        ffmpeg.avfilter_graph_parse_ptr(filter_graph, filters, &inputs, &outputs, nullptr);
        ffmpeg.av_buffersrc_add_frame_flags(buffersrc_ctx, rgb_frame, AV_BUFFERSRC_FLAG_PUSH);
        AVFrame* hw_frame = av_frame_alloc();
        ffmpeg.av_buffersink_get_frame_flags(buffersink_ctx, hw_frame, AV_BUFFERSINK_FLAG_NO_REQUEST);
        // ffmpeg.avcodec_send_frame(enc_ctx, hw_frame);
#endif

        return true;
    }

    template<AVHWDeviceType HWDeviceType, AVPixelFormat AVPixFmt>
    bool FFMPEGHWEncoder<HWDeviceType, AVPixFmt>::isSupported() const {
        return conn->cp.supportsEncoding(encodingKasmVideo);
    }

    template<AVHWDeviceType HWDeviceType, AVPixelFormat AVPixFmt>
    bool FFMPEGHWEncoder<HWDeviceType, AVPixFmt>::render(const PixelBuffer *pb) {
        // compress
        int stride;
        const auto rect = layout.dimensions;
        const auto *buffer = pb->getBuffer(rect, &stride);

        const int width = rect.width();
        const int height = rect.height();
        auto *frame = sw_frame_guard.get();

        int dst_width = width;
        int dst_height = height;

        if (width % 2 != 0)
            dst_width = width & ~1;

        if (height % 2 != 0)
            dst_height = height & ~1;

        VideoEncoderParams params{dst_width,
            dst_height,
            static_cast<uint8_t>(Server::frameRate),
            static_cast<uint8_t>(Server::groupOfPicture),
            static_cast<uint8_t>(Server::videoQualityCRFCQP)};

        if (current_params != params) {
            bpp = pb->getPF().bpp >> 3;
            if (!init(width, height, params)) {
                vlog.error("Failed to initialize encoder");
                return false;
            }

            frame = sw_frame_guard.get();
        } else {
            frame->pict_type = AV_PICTURE_TYPE_NONE;
        }

        const int src_stride_bytes = stride * bpp;

        vlog.debug("render(): width=%d, height=%d, dst_width=%d, dst_height=%d, stride=%d pixels, stride_bytes=%d, bpp=%d",
                   width, height, dst_width, dst_height, stride, src_stride_bytes, bpp);

        int err{};

#if defined(LIBYUV_CONVERSION)
        vlog.debug("Converting ARGB to NV12: src_stride=%d, dst_linesize[0]=%d, dst_linesize[1]=%d, dst_width=%d, dst_height=%d",
                   src_stride_bytes, frame->linesize[0], frame->linesize[1], dst_width, dst_height);

        if (err = libyuv::ARGBToNV12(buffer, src_stride_bytes, frame->data[0], frame->linesize[0],
            frame->data[1], frame->linesize[1], dst_width, dst_height); err != 0) {
            vlog.error("libyuv::ARGBToNV12 failed with code: %d", err);
            return false;
        }
        vlog.debug("ARGB to NV12 conversion successful");
#endif


        frame->pts = pts++;

        vlog.debug("SW frame before transfer: format=%d, width=%d, height=%d, linesize[0]=%d, linesize[1]=%d, pts=%ld",
                   frame->format, frame->width, frame->height, frame->linesize[0], frame->linesize[1], frame->pts);

        if (err = ffmpeg.av_hwframe_transfer_data(hw_frame_guard.get(), frame, 0); err < 0) {
            vlog.error(
                "Error while transferring frame data to surface (%s). Error code: %d", ffmpeg.get_error_description(err).c_str(), err);
            return false;
        }
        vlog.debug("Frame transfer successful");

        auto *hw_frame = hw_frame_guard.get();
        vlog.debug("HW frame before send: format=%d, width=%d, height=%d, linesize[0]=%d, linesize[1]=%d, pts=%ld",
                   hw_frame->format, hw_frame->width, hw_frame->height, hw_frame->linesize[0], hw_frame->linesize[1], hw_frame->pts);

        if (err = ffmpeg.avcodec_send_frame(ctx_guard.get(), hw_frame_guard.get()); err < 0) {
            vlog.error("Error sending frame to codec (%s). Error code: %d", ffmpeg.get_error_description(err).c_str(), err);
            return false;
        }
        vlog.debug("Frame sent to codec successfully");

        auto *pkt = pkt_guard.get();

        err = ffmpeg.avcodec_receive_packet(ctx_guard.get(), pkt);
        if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) {
            // Trying again
            ffmpeg.avcodec_send_frame(ctx_guard.get(), hw_frame_guard.get());
            err = ffmpeg.avcodec_receive_packet(ctx_guard.get(), pkt);
        }

        if (err < 0) {
            vlog.error("Error receiving packet from codec");
            return false;
        }

        if (pkt->flags & AV_PKT_FLAG_KEY)
            vlog.debug("Key frame %ld", frame->pts);

        return true;
    }

    template<AVHWDeviceType HWDeviceType, AVPixelFormat AVPixFmt>
    void FFMPEGHWEncoder<HWDeviceType, AVPixFmt>::writeRect(const PixelBuffer *pb, const Palette &palette) {
        auto *pkt = pkt_guard.get();
        auto *os = conn->getOutStream(conn->cp.supportsUdp);
        os->writeU8(layout.id);
        os->writeU8(msg_codec_id);
        os->writeU8(pkt->flags & AV_PKT_FLAG_KEY);
        encoders::write_compact(os, pkt->size);
        os->writeBytes(&pkt->data[0], pkt->size);
        vlog.debug("Screen id %d, codec %d, frame size:  %d", layout.id, msg_codec_id, pkt->size);

        ffmpeg.av_packet_unref(pkt);
    }

    template<AVHWDeviceType HWDeviceType, AVPixelFormat AVPixFmt>
    void FFMPEGHWEncoder<HWDeviceType, AVPixFmt>::writeSolidRect(int width, int height, const PixelFormat &pf, const rdr::U8 *colour) {}

    template<AVHWDeviceType HWDeviceType, AVPixelFormat AVPixFmt>
    void FFMPEGHWEncoder<HWDeviceType, AVPixFmt>::writeSkipRect() {
        auto *os = conn->getOutStream(conn->cp.supportsUdp);
        os->writeU8(layout.id);
        os->writeU8(kasmVideoSkip);
    }

    template class FFMPEGHWEncoder<AV_HWDEVICE_TYPE_VAAPI, AV_PIX_FMT_VAAPI>;
    template class FFMPEGHWEncoder<AV_HWDEVICE_TYPE_CUDA, AV_PIX_FMT_CUDA>;
} // namespace rfb
