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
#include "ScreenEncoderManager.h"
#include <cassert>
#include <rfb/LogWriter.h>
#include <rfb/Region.h>
#include <rfb/SMsgWriter.h>
#include <rfb/encodings.h>
#include <sys/stat.h>
#include <tbb/parallel_for_each.h>
#include "VideoEncoder.h"
#include "VideoEncoderFactory.h"

namespace rfb {
    static LogWriter vlog("ScreenEncoderManager");

    template<uint8_t T>
    ScreenEncoderManager<T>::ScreenEncoderManager(const FFmpeg &ffmpeg_, const KasmVideoEncoders::EncoderConfig &encoder,
        const KasmVideoEncoders::EncoderConfigs &encoders, SConnection *conn, VideoEncoderParams params) :
        Encoder(conn, encodingKasmVideo, static_cast<EncoderFlags>(EncoderUseNativePF | EncoderLossy), -1),
        ffmpeg(ffmpeg_),
        current_params(params),
        base_video_encoder(encoder),
        available_encoders(encoders) {
        screens_to_refresh.reserve(T);
    }

    template<uint8_t T>
    ScreenEncoderManager<T>::~ScreenEncoderManager() {
        clear_screens();
    }

    template<uint8_t T>
    void ScreenEncoderManager<T>::clear() {
        current_params = {};
        base_video_encoder = KasmVideoEncoders::EncoderConfig{KasmVideoEncoders::Encoder::unavailable};
        available_encoders.clear();
        screens_to_refresh.clear();
        clear_screens();
    }

    template<uint8_t T>
    void ScreenEncoderManager<T>::set_params(const KasmVideoEncoders::EncoderConfig &encoder,
        const KasmVideoEncoders::EncoderConfigs &encoders, VideoEncoderParams params) {
        base_video_encoder = encoder;
        available_encoders = encoders;
        current_params = params;
    }


    template<uint8_t T>
    VideoEncoder *ScreenEncoderManager<T>::add_encoder(const Screen &layout) const {
        VideoEncoder *encoder{};
        try {
            encoder = create_encoder(layout, &ffmpeg, conn, base_video_encoder.encoder, base_video_encoder.dri_path.c_str(), current_params);
        } catch (const std::exception &e) {
            if (base_video_encoder.encoder != KasmVideoEncoders::Encoder::h264_software) {
                vlog.error("Attempting fallback to software encoder due to error: %s", e.what());
                try {
                    encoder = create_encoder(layout, &ffmpeg, conn, KasmVideoEncoders::Encoder::h264_software, nullptr, current_params);
                } catch (const std::exception &exception) {
                    vlog.error("Failed to create software encoder: %s", exception.what());
                }
            } else
                vlog.error("Failed to create software encoder: %s", e.what());
        }

        return encoder;
    }

    template<uint8_t T>
    bool ScreenEncoderManager<T>::add_screen(uint8_t index, const Screen &layout) {
        auto *encoder = add_encoder(layout);
        if (!encoder)
            return false;

        mask |= 1 << index;

        screens[index] = {layout, encoder, true};
        head = std::min(head, index);
        ++count;

        return true;
    }

    template<uint8_t T>
    size_t ScreenEncoderManager<T>::get_screen_count() const {
        return count;
    }

    template<uint8_t T>
    void ScreenEncoderManager<T>::remove_screen(uint8_t index) {
        if (screens[index].encoder) {
            delete screens[index].encoder;
            screens[index].encoder = nullptr;

            --count;
        }
        mask &= ~(1 << index);
        screens[index] = {};
    }

    template<uint8_t T>
    void ScreenEncoderManager<T>::rebuild_screens_to_refresh() {
        screens_to_refresh.clear();

        uint64_t remaining_mask = mask;
        while (remaining_mask) {
            const auto pos = __builtin_ctzll(remaining_mask);
            if (screens[pos].dirty)
                screens_to_refresh.push_back(pos);

            remaining_mask &= remaining_mask - 1;
        }
    }

    template<uint8_t T>
    void ScreenEncoderManager<T>::clear_screens() {
        uint64_t remaining_mask = mask;
        while (remaining_mask) {
            const auto pos = __builtin_ctzll(remaining_mask);
            remove_screen(pos);
            remaining_mask &= remaining_mask - 1;
        }
    }

    template<uint8_t T>
    ScreenEncoderManager<T>::stats_t ScreenEncoderManager<T>::get_stats() const {
        return stats;
    }

    template<uint8_t T>
    bool ScreenEncoderManager<T>::sync_layout(const ScreenSet &layout, const Region &region) {
        const auto bounds = region.get_bounding_rect();

        const auto old_mask = mask;

        for (uint8_t i = 0; i < static_cast<uint8_t>(layout.num_screens()); ++i) {
            const auto &screen = layout.screens[i];
            auto id = screen.id;
            if (id >= T) {
                vlog.error("Wrong id");
                id = 0;
            }

            if (!screens[id].layout.dimensions.equals(screen.dimensions)) {
                remove_screen(id);
                if (!add_screen(id, screen))
                    return false;
            } else if (screen.dimensions.overlaps(bounds)) {
                screens[id].dirty = true;
            }
        }

        if (old_mask != mask || (mask > 0 && screens_to_refresh.empty()))
            rebuild_screens_to_refresh();

        return true;
    }

    template<uint8_t T>
    bool ScreenEncoderManager<T>::isSupported() const {
        if (const auto *encoder = screens[head].encoder; encoder)
            return encoder->isSupported();

        return false;
    }

    template<uint8_t T>
    bool ScreenEncoderManager<T>::writeFrame(const PixelBuffer *pb, const Palette &palette) {
        if (screens_to_refresh.empty())
            return true;

        const auto bpp = conn->cp.pf().bpp >> 3;
        auto *out_conn = conn->getOutStream(conn->cp.supportsUdp);

        if (!out_conn) {
            vlog.error("writeRect: getOutStream returned NULL");
            return false;
        }

        const auto send_frame = [this, &bpp, out_conn, pb, &palette](screen_t &screen) {
            ++stats.rects;
            const auto &rect = screen.layout.dimensions;
            const auto area = rect.area();
            stats.pixels += area;
            const auto before = out_conn->length();

            const int equiv = 12 + (area * bpp);
            stats.equivalent += equiv;

            const auto &encoder = screen.encoder;

            conn->writer()->startRect(rect, encoder->encoding);
            encoder->writeRect(pb, palette);
            conn->writer()->endRect();

            screen.dirty = false;

            const auto after = out_conn->length();
            stats.bytes += after - before;
        };

        if (screens_to_refresh.size() > 1) {
            tbb::task_group_context ctx;

            tbb::parallel_for_each(screens_to_refresh.begin(), screens_to_refresh.end(), [this, pb, &ctx](uint8_t index) {
                if (ctx.is_group_execution_cancelled())
                    return;

                auto &screen = screens[index];
                if (auto *encoder = screen.encoder; encoder) {
                    screen.dirty = encoder->render(pb);
                    if (!screen.dirty)
                        ctx.cancel_group_execution();
                }
            });

            if (ctx.is_group_execution_cancelled())
                return false;

            for (auto index: screens_to_refresh) {
                auto &screen = screens[index];
                if (screen.dirty) {
                    send_frame(screen);
                }
            }
        } else {
            if (auto encoder = screens[head].encoder; encoder) {
                if (encoder->render(pb))
                    send_frame(screens[head]);
                else
                    return false;
            }
        }

        return true;
    }

    template<uint8_t T>
    void ScreenEncoderManager<T>::writeSolidRect(int width, int height, const PixelFormat &pf, const rdr::U8 *colour) {
        for (const auto index: screens_to_refresh) {
            if (auto *encoder = screens[index].encoder; encoder)
                encoder->writeSolidRect(width, height, pf, colour);
        }
    }

} // namespace rfb
