#pragma once
#include <iostream>
#include <string>
#include <stdexcept>
#include <vector>
#include <atomic>
#include <csignal>
#include <mutex>
#include <CLI/CLI.hpp>

// FFmpeg headers
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

// SDL2 headers
#include <SDL2/SDL.h>

namespace fplay {

struct AudioState {
    SwrContext* swr = nullptr;
    AVSampleFormat dst_fmt = AV_SAMPLE_FMT_S16;
    int dst_rate = 48000;
    int dst_channels = 2;
    uint64_t dst_layout = AV_CH_LAYOUT_STEREO;
    std::vector<uint8_t> buffer;
    std::mutex mtx;
};

struct FPlayArgs {
    std::string ip;
    std::string port = "554";
    std::string stream;
};

struct VideoContext {
    AVCodecContext* dec_ctx = nullptr;
    SwsContext* sws_ctx = nullptr;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    int width = 0;
    int height = 0;
};

struct AudioContext {
    AVCodecContext* dec_ctx = nullptr;
    SwrContext* swr_ctx = nullptr;
    AudioState state;
};

const int MAJOR_VER = MAJOR_VERSION;
const int MINOR_VER = MINOR_VERSION;
const int PATCH_VER = PATCH_VERSION;

// Global state
extern std::atomic<bool> g_running;

// Signal handler
void handle_sigint(int sig);

// Error handlers
void sdl_panic(const std::string& msg);
void ff_panic(const std::string& msg, int err);

// Audio callback
void sdl_audio_callback(void* userdata, Uint8* stream, int len);

// Argument parsing
bool parse_args(int argc, char** argv, FPlayArgs& args);

// Initialization functions
AVFormatContext* init_ffmpeg_and_open_stream(const std::string& url);
void find_stream_indices(AVFormatContext* fmt, int& video_idx, int& audio_idx);
bool init_video_decoder(AVFormatContext* fmt, int video_idx, VideoContext& vctx);
bool init_audio_decoder(AVFormatContext* fmt, int audio_idx, AudioContext& actx);
bool init_sdl_video(const VideoContext& vctx);
bool init_sdl_audio(AudioContext& actx);

// Processing functions
void process_video_packet(AVPacket* pkt, AVCodecContext* dec_ctx, SwsContext* sws,
                         AVFrame* vframe, AVFrame* yuv, SDL_Texture* tex,
                         SDL_Renderer* ren, SDL_Window* win);
void process_audio_packet(AVPacket* pkt, AVCodecContext* dec_ctx, SwrContext* swr,
                         AVFrame* aframe, AudioState& audio);

// Main loop
int run_playback_loop(AVFormatContext* fmt, int video_idx, int audio_idx,
                     VideoContext& vctx, AudioContext& actx);

// Cleanup
void cleanup_video(VideoContext& vctx);
void cleanup_audio(AudioContext& actx);

} // namespace fplay