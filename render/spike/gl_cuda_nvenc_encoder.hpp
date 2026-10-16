#pragma once

#include <cuda_gl_interop.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_cuda.h>
#include <libavutil/pixfmt.h>
}

#include "glad/glad.h"

#include <cstdio>
#include <stdexcept>
#include <string>

namespace chrononmotion::certification {

class GlCudaNvencEncoder {
public:
    GlCudaNvencEncoder(int width, int height, int fps, const std::string& path)
        : width_(width), height_(height), fps_(fps) {
        cuda_check(cudaGLGetDevices(&device_count_, devices_, 1, cudaGLDeviceListCurrentFrame), "cudaGLGetDevices");
        if (device_count_ != 1) throw std::runtime_error("GL context is not associated with exactly one CUDA device");
        cuda_check(cudaSetDevice(devices_[0]), "cudaSetDevice(GL device)");

        glGenBuffers(1, &pbo_);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_);
        glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(width_) * height_ * 4, nullptr, GL_STREAM_READ);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        cuda_check(cudaGraphicsGLRegisterBuffer(&graphics_resource_, pbo_, cudaGraphicsRegisterFlagsReadOnly),
                   "cudaGraphicsGLRegisterBuffer");

        const std::string device_id = std::to_string(devices_[0]);
        av_check(av_hwdevice_ctx_create(&cuda_device_, AV_HWDEVICE_TYPE_CUDA, device_id.c_str(), nullptr,
                                        AV_CUDA_USE_PRIMARY_CONTEXT), "av_hwdevice_ctx_create(CUDA primary)");
        frames_ = av_hwframe_ctx_alloc(cuda_device_);
        if (!frames_) throw std::runtime_error("av_hwframe_ctx_alloc failed");
        auto* frame_context = reinterpret_cast<AVHWFramesContext*>(frames_->data);
        frame_context->format = AV_PIX_FMT_CUDA;
        frame_context->sw_format = AV_PIX_FMT_RGB0;
        frame_context->width = width_;
        frame_context->height = height_;
        frame_context->initial_pool_size = 4;
        av_check(av_hwframe_ctx_init(frames_), "av_hwframe_ctx_init(CUDA RGB0)");

        const AVCodec* codec = avcodec_find_encoder_by_name("h264_nvenc");
        if (!codec) throw std::runtime_error("FFmpeg h264_nvenc encoder is unavailable");
        codec_ = avcodec_alloc_context3(codec);
        if (!codec_) throw std::runtime_error("avcodec_alloc_context3 failed");
        codec_->width = width_;
        codec_->height = height_;
        codec_->time_base = AVRational{1, fps_};
        codec_->framerate = AVRational{fps_, 1};
        codec_->pix_fmt = AV_PIX_FMT_CUDA;
        codec_->bit_rate = 12'000'000;
        codec_->gop_size = fps_ * 2;
        codec_->max_b_frames = 0;
        codec_->hw_frames_ctx = av_buffer_ref(frames_);
        av_check(avcodec_open2(codec_, codec, nullptr), "avcodec_open2(h264_nvenc CUDA RGB0)");

        av_check(avformat_alloc_output_context2(&format_, nullptr, nullptr, path.c_str()),
                 "avformat_alloc_output_context2");
        if (!format_) throw std::runtime_error("could not allocate MP4 output context");
        stream_ = avformat_new_stream(format_, nullptr);
        if (!stream_) throw std::runtime_error("avformat_new_stream failed");
        stream_->time_base = codec_->time_base;
        av_check(avcodec_parameters_from_context(stream_->codecpar, codec_), "avcodec_parameters_from_context");
        if (!(format_->oformat->flags & AVFMT_NOFILE))
            av_check(avio_open(&format_->pb, path.c_str(), AVIO_FLAG_WRITE), "avio_open");
        av_check(avformat_write_header(format_, nullptr), "avformat_write_header");
        header_written_ = true;
        packet_ = av_packet_alloc();
        frame_ = av_frame_alloc();
        if (!packet_ || !frame_) throw std::runtime_error("FFmpeg packet/frame allocation failed");
    }

    GlCudaNvencEncoder(const GlCudaNvencEncoder&) = delete;
    GlCudaNvencEncoder& operator=(const GlCudaNvencEncoder&) = delete;

    ~GlCudaNvencEncoder() { close_noexcept(); }

    // Consume the current single-sample GL render target through a pixel-pack
    // buffer. GL writes the PBO, CUDA maps it on the same physical device, and
    // a device-to-device copy fills an FFmpeg CUDA frame for NVENC. No host pixel
    // pointer is created in this production encode path.
    void encode_current_target(int64_t pts) {
        GLint previous_read_fbo = 0;
        GLint previous_pack_buffer = 0;
        GLint previous_pack_alignment = 4;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read_fbo);
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previous_pack_buffer);
        glGetIntegerv(GL_PACK_ALIGNMENT, &previous_pack_alignment);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);

        // ReadPixels into PBO is GPU-resident. The vertical blit reverses the
        // framebuffer's bottom-up row order for normal top-down video output.
        GLint previous_draw_fbo = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous_draw_fbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, previous_read_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, width_, height_, 0, height_, width_, 0,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glReadBuffer(GL_BACK);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_);
        glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(previous_pack_buffer));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previous_read_fbo));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previous_draw_fbo));
        glPixelStorei(GL_PACK_ALIGNMENT, previous_pack_alignment);
        gl_check("GL PBO read");

        cuda_check(cudaGraphicsMapResources(1, &graphics_resource_, 0), "cudaGraphicsMapResources(GL PBO)");
        void* device_pixels = nullptr;
        size_t mapped_bytes = 0;
        cuda_check(cudaGraphicsResourceGetMappedPointer(&device_pixels, &mapped_bytes, graphics_resource_),
                   "cudaGraphicsResourceGetMappedPointer");
        if (mapped_bytes < static_cast<size_t>(width_) * height_ * 4) {
            cudaGraphicsUnmapResources(1, &graphics_resource_, 0);
            throw std::runtime_error("mapped GL PBO is smaller than one RGBA frame");
        }

        av_frame_unref(frame_);
        frame_->format = AV_PIX_FMT_CUDA;
        frame_->width = width_;
        frame_->height = height_;
        frame_->hw_frames_ctx = av_buffer_ref(frames_);
        av_check(av_hwframe_get_buffer(frames_, frame_, 0), "av_hwframe_get_buffer(CUDA RGB0)");
        cuda_check(cudaMemcpy2D(frame_->data[0], frame_->linesize[0], device_pixels,
                                static_cast<size_t>(width_) * 4, static_cast<size_t>(width_) * 4,
                                height_, cudaMemcpyDeviceToDevice), "cudaMemcpy2D(GL PBO -> NVENC frame)");
        cuda_check(cudaGraphicsUnmapResources(1, &graphics_resource_, 0), "cudaGraphicsUnmapResources(GL PBO)");

        frame_->pts = pts;
        av_check(avcodec_send_frame(codec_, frame_), "avcodec_send_frame(NVENC)");
        drain_packets(false);
        ++frames_submitted_;
    }

    void finish() {
        if (finished_) return;
        av_check(avcodec_send_frame(codec_, nullptr), "avcodec_send_frame(flush)");
        drain_packets(true);
        av_check(av_write_trailer(format_), "av_write_trailer");
        header_written_ = false;
        finished_ = true;
    }

    [[nodiscard]] int frames_submitted() const { return frames_submitted_; }

private:
    static void cuda_check(cudaError_t result, const char* operation) {
        if (result != cudaSuccess)
            throw std::runtime_error(std::string(operation) + ": " + cudaGetErrorString(result));
    }
    static void av_check(int result, const char* operation) {
        if (result < 0) {
            char error[AV_ERROR_MAX_STRING_SIZE]{};
            av_strerror(result, error, sizeof(error));
            throw std::runtime_error(std::string(operation) + ": " + error);
        }
    }
    static void gl_check(const char* operation) {
        const GLenum error = glGetError();
        if (error != GL_NO_ERROR)
            throw std::runtime_error(std::string(operation) + ": GL error " + std::to_string(error));
    }
    void drain_packets(bool flushing) {
        while (true) {
            const int result = avcodec_receive_packet(codec_, packet_);
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) break;
            av_check(result, "avcodec_receive_packet(NVENC)");
            av_packet_rescale_ts(packet_, codec_->time_base, stream_->time_base);
            packet_->stream_index = stream_->index;
            av_check(av_interleaved_write_frame(format_, packet_), "av_interleaved_write_frame");
            av_packet_unref(packet_);
        }
        if (flushing && !format_) throw std::runtime_error("missing MP4 muxer during flush");
    }
    void close_noexcept() noexcept {
        if (header_written_ && format_) av_write_trailer(format_);
        if (format_ && !(format_->oformat->flags & AVFMT_NOFILE) && format_->pb) avio_closep(&format_->pb);
        if (packet_) av_packet_free(&packet_);
        if (frame_) av_frame_free(&frame_);
        if (codec_) avcodec_free_context(&codec_);
        if (format_) avformat_free_context(format_);
        if (frames_) av_buffer_unref(&frames_);
        if (cuda_device_) av_buffer_unref(&cuda_device_);
        if (graphics_resource_) cudaGraphicsUnregisterResource(graphics_resource_);
        if (pbo_) glDeleteBuffers(1, &pbo_);
    }

    int width_{};
    int height_{};
    int fps_{};
    int frames_submitted_{};
    unsigned int device_count_{};
    int devices_[1]{};
    GLuint pbo_{};
    cudaGraphicsResource* graphics_resource_{};
    AVBufferRef* cuda_device_{};
    AVBufferRef* frames_{};
    AVCodecContext* codec_{};
    AVFormatContext* format_{};
    AVStream* stream_{};
    AVPacket* packet_{};
    AVFrame* frame_{};
    bool header_written_{};
    bool finished_{};
};

} // namespace chrononmotion::certification
