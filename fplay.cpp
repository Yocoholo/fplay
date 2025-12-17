/**
 fplay.cpp is the ffplay wrapper for making it easier to consume media files.
 fplay can parse ssh config for resolving remote hosts.
 open source under the MIT license.
 author: github.com/yocoholo
 */
#include "fplay.h"

using namespace fplay;

std::atomic<bool> fplay::g_running{true};

void fplay::handle_sigint(int) {
    g_running = false;
}

void fplay::sdl_panic(const std::string& msg) {
    std::cerr << "SDL error: " << msg << " | " << SDL_GetError() << std::endl;
    std::exit(1);
}

void fplay::ff_panic(const std::string& msg, int err) {
    char errbuf[256];
    av_strerror(err, errbuf, sizeof(errbuf));
    std::cerr << "FFmpeg error: " << msg << " | " << errbuf << std::endl;
    std::exit(1);
}

void fplay::sdl_audio_callback(void* userdata, Uint8* stream, int len) {
    AudioState* a = static_cast<AudioState*>(userdata);
    SDL_memset(stream, 0, len);
    std::lock_guard<std::mutex> lock(a->mtx);
    if (a->buffer.empty()) return;
    int n = std::min<int>(len, a->buffer.size());
    SDL_MixAudioFormat(stream, a->buffer.data(), AUDIO_S16, n, SDL_MIX_MAXVOLUME);
    a->buffer.erase(a->buffer.begin(), a->buffer.begin() + n);
}

bool fplay::parse_args(int argc, char** argv, FPlayArgs& args) {
    CLI::App app{"FFmpeg + SDL2 RTSP media player"};

    app.add_option("-i,--ip", args.ip, "IP address of RTSP server")->required();
    app.add_option("-p,--port", args.port, "RTSP port")->default_val("554");
    app.add_option("-s,--stream", args.stream, "Stream path")->required();

    app.set_version_flag("--version,-v",
        "fplay version " + std::to_string(MAJOR_VER) + "." +
        std::to_string(MINOR_VER) + "." + std::to_string(PATCH_VER));

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
        app.exit(e);
        return false;
    }

    return true;
}

AVFormatContext* fplay::init_ffmpeg_and_open_stream(const std::string& url) {
    av_log_set_level(AV_LOG_WARNING);
    avformat_network_init();

    AVFormatContext* fmt = nullptr;
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);
    av_dict_set(&opts, "stimeout", "5000000", 0);
    av_dict_set(&opts, "fflags", "nobuffer", 0);
    av_dict_set(&opts, "buffer_size", "102400", 0);

    int ret = avformat_open_input(&fmt, url.c_str(), nullptr, &opts);
    av_dict_free(&opts);
    if (ret < 0) ff_panic("avformat_open_input", ret);

    ret = avformat_find_stream_info(fmt, nullptr);
    if (ret < 0) ff_panic("avformat_find_stream_info", ret);

    return fmt;
}

void fplay::find_stream_indices(AVFormatContext* fmt, int& video_idx, int& audio_idx) {
    video_idx = -1;
    audio_idx = -1;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_idx < 0)
            video_idx = i;
        else if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_idx < 0)
            audio_idx = i;
    }
}

bool fplay::init_video_decoder(AVFormatContext* fmt, int video_idx, VideoContext& vctx) {
    if (video_idx < 0) return false;

    AVStream* vs = fmt->streams[video_idx];
    const AVCodec* vcodec = avcodec_find_decoder(vs->codecpar->codec_id);
    if (!vcodec) {
        std::cerr << "Video codec not found.\n";
        return false;
    }

    vctx.dec_ctx = avcodec_alloc_context3(vcodec);
    avcodec_parameters_to_context(vctx.dec_ctx, vs->codecpar);
    vctx.dec_ctx->thread_count = 2;
    vctx.dec_ctx->lowres = 0;

    int ret = avcodec_open2(vctx.dec_ctx, vcodec, nullptr);
    if (ret < 0) ff_panic("avcodec_open2(video)", ret);

    vctx.width = vctx.dec_ctx->width;
    vctx.height = vctx.dec_ctx->height;
    vctx.sws_ctx = sws_getContext(vctx.width, vctx.height, vctx.dec_ctx->pix_fmt,
                                   vctx.width, vctx.height, AV_PIX_FMT_YUV420P,
                                   SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!vctx.sws_ctx) {
        std::cerr << "Failed to create sws context.\n";
        return false;
    }

    return true;
}

bool fplay::init_audio_decoder(AVFormatContext* fmt, int audio_idx, AudioContext& actx) {
    if (audio_idx < 0) return false;

    AVStream* as = fmt->streams[audio_idx];
    const AVCodec* acodec = avcodec_find_decoder(as->codecpar->codec_id);
    if (!acodec) {
        std::cerr << "Audio codec not found.\n";
        return false;
    }

    actx.dec_ctx = avcodec_alloc_context3(acodec);
    avcodec_parameters_to_context(actx.dec_ctx, as->codecpar);
    actx.dec_ctx->request_sample_fmt = AV_SAMPLE_FMT_S16;

    int ret = avcodec_open2(actx.dec_ctx, acodec, nullptr);
    if (ret < 0) ff_panic("avcodec_open2(audio)", ret);

#if LIBAVUTIL_VERSION_MAJOR >= 57  // FFmpeg ≥ 5.0

    /* ---------- channel layouts ---------- */

    AVChannelLayout in_ch_layout;

    if (actx.dec_ctx->ch_layout.nb_channels > 0) {
        av_channel_layout_copy(&in_ch_layout, &actx.dec_ctx->ch_layout);
    } else {
        int ch = actx.dec_ctx->ch_layout.nb_channels;
        if (ch <= 0) ch = 2;
        av_channel_layout_default(&in_ch_layout, ch);
    }

    AVChannelLayout out_ch_layout;
    av_channel_layout_default(&out_ch_layout, actx.state.dst_channels);

    /* ---------- swresample ---------- */

    actx.swr_ctx = nullptr;
    ret = swr_alloc_set_opts2(
        &actx.swr_ctx,
        &out_ch_layout,
        actx.state.dst_fmt,
        actx.state.dst_rate,
        &in_ch_layout,
        actx.dec_ctx->sample_fmt,
        actx.dec_ctx->sample_rate,
        0,
        nullptr
    );

    if (ret < 0 || !actx.swr_ctx)
        ff_panic("swr_alloc_set_opts2", ret);

#else  // FFmpeg 4.x and earlier

    int in_channels = actx.dec_ctx->channels;
    uint64_t in_channel_layout = actx.dec_ctx->channel_layout;

    if (in_channel_layout == 0) {
        if (in_channels == 0) in_channels = 2;
        in_channel_layout = av_get_default_channel_layout(in_channels);
    }

    uint64_t out_ch_layout =
        av_get_default_channel_layout(actx.state.dst_channels);

    actx.swr_ctx = swr_alloc_set_opts(
        nullptr,
        out_ch_layout,
        actx.state.dst_fmt,
        actx.state.dst_rate,
        in_channel_layout,
        actx.dec_ctx->sample_fmt,
        actx.dec_ctx->sample_rate,
        0,
        nullptr
    );

    if (!actx.swr_ctx)
        ff_panic("swr_alloc_set_opts", AVERROR(ENOMEM));

#endif

    ret = swr_init(actx.swr_ctx);
    if (ret < 0)
        ff_panic("swr_init", ret);

    actx.state.swr = actx.swr_ctx;

#if LIBAVUTIL_VERSION_MAJOR >= 57
    av_channel_layout_uninit(&in_ch_layout);
    av_channel_layout_uninit(&out_ch_layout);
#endif

    return true;
}

bool fplay::init_sdl_video(const VideoContext& vctx) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS))
        sdl_panic("SDL_Init failed");
    return true;
}

bool fplay::init_sdl_audio(AudioContext& actx) {
    SDL_AudioSpec want{};
    want.freq = actx.state.dst_rate;
    want.format = AUDIO_S16;
    want.channels = actx.state.dst_channels;
    want.samples = 1024;
    want.callback = sdl_audio_callback;
    want.userdata = &actx.state;

    SDL_AudioSpec have{};
    if (SDL_OpenAudio(&want, &have) < 0) sdl_panic("OpenAudio");
    SDL_PauseAudio(0);
    return true;
}

void fplay::process_video_packet(AVPacket* pkt, AVCodecContext* dec_ctx, SwsContext* sws,
                                AVFrame* vframe, AVFrame* yuv, SDL_Texture* tex,
                                SDL_Renderer* ren, SDL_Window* win) {
    int ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret != 0) return;

    while ((ret = avcodec_receive_frame(dec_ctx, vframe)) == 0) {
        sws_scale(sws, vframe->data, vframe->linesize,
                 0, dec_ctx->height, yuv->data, yuv->linesize);

        SDL_UpdateYUVTexture(tex, nullptr,
                            yuv->data[0], yuv->linesize[0],
                            yuv->data[1], yuv->linesize[1],
                            yuv->data[2], yuv->linesize[2]);

        int w, h;
        SDL_GetWindowSize(win, &w, &h);
        SDL_Rect dst{0, 0, w, h};
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, &dst);
        SDL_RenderPresent(ren);
    }
}

void fplay::process_audio_packet(AVPacket* pkt, AVCodecContext* dec_ctx, SwrContext* swr,
                                AVFrame* aframe, AudioState& audio) {
    int ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret != 0) return;

    while ((ret = avcodec_receive_frame(dec_ctx, aframe)) == 0) {
        int max_out = swr_get_out_samples(swr, aframe->nb_samples);
        std::vector<uint8_t> outbuf(max_out * audio.dst_channels * av_get_bytes_per_sample(audio.dst_fmt));
        uint8_t* outptrs[1] = { outbuf.data() };
        int nb = swr_convert(swr, outptrs, max_out, (const uint8_t**)aframe->data, aframe->nb_samples);
        int out_bytes = nb * audio.dst_channels * av_get_bytes_per_sample(audio.dst_fmt);

        std::lock_guard<std::mutex> lock(audio.mtx);
        audio.buffer.insert(audio.buffer.end(), outbuf.begin(), outbuf.begin() + out_bytes);
    }
}

int fplay::run_playback_loop(AVFormatContext* fmt, int video_idx, int audio_idx,
                            VideoContext& vctx, AudioContext& actx) {
    AVPacket* pkt = av_packet_alloc();
    AVFrame* vframe = av_frame_alloc();
    AVFrame* aframe = av_frame_alloc();
    AVFrame* yuv = av_frame_alloc();

    if (video_idx >= 0) {
        yuv->format = AV_PIX_FMT_YUV420P;
        yuv->width = vctx.width;
        yuv->height = vctx.height;
        int ret = av_frame_get_buffer(yuv, 32);
        if (ret < 0) ff_panic("av_frame_get_buffer(yuv)", ret);
    }

    while (g_running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) g_running = false;
            else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_q) g_running = false;
        }

        int ret = av_read_frame(fmt, pkt);
        if (ret == AVERROR_EOF) break;
        if (ret < 0) {
            SDL_Delay(5);
            continue;
        }

        if (pkt->stream_index == video_idx && vctx.dec_ctx) {
            process_video_packet(pkt, vctx.dec_ctx, vctx.sws_ctx, vframe, yuv,
                               vctx.texture, vctx.renderer, vctx.window);
        } else if (pkt->stream_index == audio_idx && actx.dec_ctx) {
            process_audio_packet(pkt, actx.dec_ctx, actx.swr_ctx, aframe, actx.state);
        }

        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    av_frame_free(&vframe);
    av_frame_free(&aframe);
    av_frame_free(&yuv);

    return 0;
}

void fplay::cleanup_video(VideoContext& vctx) {
    if (vctx.texture) SDL_DestroyTexture(vctx.texture);
    if (vctx.renderer) SDL_DestroyRenderer(vctx.renderer);
    if (vctx.window) SDL_DestroyWindow(vctx.window);
    if (vctx.sws_ctx) sws_freeContext(vctx.sws_ctx);
    if (vctx.dec_ctx) avcodec_free_context(&vctx.dec_ctx);
}

void fplay::cleanup_audio(AudioContext& actx) {
    SDL_CloseAudio();
    if (actx.swr_ctx) {
        swr_free(&actx.swr_ctx);
        actx.state.swr = nullptr;
    }
    if (actx.dec_ctx) avcodec_free_context(&actx.dec_ctx);
}

int main(int argc, char** argv) {
    using namespace fplay;

    FPlayArgs args;
    if (!parse_args(argc, argv, args)) return 1;

    std::string url = "rtsp://" + args.ip + ":" + args.port + "/" + args.stream;
    std::signal(SIGINT, handle_sigint);

    AVFormatContext* fmt = init_ffmpeg_and_open_stream(url);

    int video_idx, audio_idx;
    find_stream_indices(fmt, video_idx, audio_idx);

    if (video_idx < 0 && audio_idx < 0) {
        std::cerr << "No audio or video streams found.\n";
        return 1;
    }

    VideoContext vctx;
    AudioContext actx;

    bool has_video = init_video_decoder(fmt, video_idx, vctx);
    bool has_audio = init_audio_decoder(fmt, audio_idx, actx);

    if (has_video || has_audio) init_sdl_video(vctx);

    if (has_video) {
        vctx.window = SDL_CreateWindow("RTSP Player", SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, vctx.width, vctx.height,
                                       SDL_WINDOW_RESIZABLE);
        if (!vctx.window) sdl_panic("CreateWindow");
        vctx.renderer = SDL_CreateRenderer(vctx.window, -1,
                                          SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!vctx.renderer) sdl_panic("CreateRenderer");
        vctx.texture = SDL_CreateTexture(vctx.renderer, SDL_PIXELFORMAT_IYUV,
                                        SDL_TEXTUREACCESS_STREAMING, vctx.width, vctx.height);
        if (!vctx.texture) sdl_panic("CreateTexture");
    }

    if (has_audio) init_sdl_audio(actx);

    int result = run_playback_loop(fmt, video_idx, audio_idx, vctx, actx);

    if (has_video) cleanup_video(vctx);
    if (has_audio) cleanup_audio(actx);
    if (fmt) avformat_close_input(&fmt);

    SDL_Quit();
    avformat_network_deinit();
    return result;
}

