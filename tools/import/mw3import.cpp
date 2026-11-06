// mw3import: the first-run importer's movies (docs/fmv.md, The imported
// movies). Our addition, not original code. For each video\*.AVI of an MW3
// 1.2 install (Indeo 5 at 15 fps, PCM sound) it writes, in the write dir's
// video folder:
//   <name>.m1v  the picture as an MPEG-1 video stream at 30 fps, every frame
//               twice (MPEG-1 has no 15 fps), exactly 2 x dwLength frames,
//               for pl_mpeg;
//   <name>.wav  the AVI's sound stream byte for byte, its WAVEFORMAT the fmt;
//   <name>.fmv  the AVI's stream headers and the video's key frames, which
//               the movie player's arithmetic reads (fmv_stream.cpp).
//
//   mw3import [--install DIR] [--write-dir DIR] [--only NAME] [--missing]
//             [--cue DISC.cue [--music-only]] [--shapes]
//
// --missing: only the movies without a .fmv yet (mw3's first run asks for
// these, following the "progress K N" lines). --cue: the disc image's CD
// audio as music\trackNN.wav too (platform/cue_audio.h). --shapes: then
// every movie made measured as the program would (movie_shape.h), a
// "shape <line>" each, for tools/analysis/movie_shapes.py's table.
//
// Needs FFmpeg's libavformat, libavcodec, libswscale and libavutil (Indeo 5
// decoding and the MPEG-1 encoder); built only when they are found.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "mech3/app_movie.h"
#include "platform/cue_audio.h"
#include "gamez/zutil/zutl_path.h"
#include "frontend/sdl/data_folder_start.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace fs = std::filesystem;

namespace {

uint32_t u32(const std::vector<uint8_t>& d, size_t p) {
  return p + 4 <= d.size() ? static_cast<uint32_t>(d[p] | d[p + 1] << 8 | d[p + 2] << 16) | uint32_t{d[p + 3]} << 24
                           : 0;
}
uint16_t u16(const std::vector<uint8_t>& d, size_t p) {
  return p + 2 <= d.size() ? static_cast<uint16_t>(d[p] | d[p + 1] << 8) : 0;
}

// An AVI stream header (strh), as AVIStreamInfoA gives it.
struct StreamHeader {
  std::string type, handler;
  uint32_t flags = 0, priority = 0, language = 0, initial = 0, scale = 0, rate = 0, start = 0, length = 0,
           suggested = 0, quality = 0, sample_size = 0;
  int16_t frame[4] = {};
  std::vector<uint8_t> format;  // strf
};

struct Avi {
  uint32_t us_per_frame = 0, frames = 0, suggested = 0, width = 0, height = 0;
  StreamHeader video, audio;
  bool has_audio = false;
  std::vector<uint32_t> keys;    // video frames flagged AVIIF_KEYFRAME in idx1
  std::vector<uint8_t> sound;    // the audio chunks' bytes, in order
  uint32_t video_chunks = 0;
};

bool parse_avi(const std::string& path, Avi* out, std::string* err) {
  std::ifstream f(path, std::ios::binary);
  std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(d.data() + 8, "AVI ", 4)) {
    *err = "not an AVI";
    return false;
  }
  size_t movi = 0;
  StreamHeader* cur = nullptr;
  // The chunks of a list, recursing into hdrl and strl.
  std::vector<std::pair<size_t, size_t>> todo = {{12, d.size()}};
  while (!todo.empty()) {
    auto [p, end] = todo.back();
    todo.pop_back();
    std::vector<std::pair<size_t, size_t>> lists;
    while (p + 8 <= end) {
      const std::string id(reinterpret_cast<const char*>(&d[p]), 4);
      const uint32_t n = u32(d, p + 4);
      const size_t q = p + 8;
      if (id == "LIST" && q + 4 <= d.size()) {
        const std::string t(reinterpret_cast<const char*>(&d[q]), 4);
        if (t == "hdrl" || t == "strl") lists.push_back({q + 4, std::min(q + n, d.size())});
        if (t == "movi") movi = q;
      } else if (id == "avih") {
        out->us_per_frame = u32(d, q), out->frames = u32(d, q + 16), out->suggested = u32(d, q + 28);
        out->width = u32(d, q + 32), out->height = u32(d, q + 36);
      } else if (id == "strh") {
        const std::string type(reinterpret_cast<const char*>(&d[q]), 4);
        cur = type == "vids" ? &out->video : type == "auds" ? &out->audio : nullptr;
        if (type == "auds") out->has_audio = true;
        if (cur) {
          cur->type = type;
          cur->handler.assign(reinterpret_cast<const char*>(&d[q + 4]), 4);
          cur->flags = u32(d, q + 8), cur->priority = u16(d, q + 12), cur->language = u16(d, q + 14);
          cur->initial = u32(d, q + 16), cur->scale = u32(d, q + 20), cur->rate = u32(d, q + 24);
          cur->start = u32(d, q + 28), cur->length = u32(d, q + 32), cur->suggested = u32(d, q + 36);
          cur->quality = u32(d, q + 40), cur->sample_size = u32(d, q + 44);
          for (int i = 0; i < 4; ++i) cur->frame[i] = static_cast<int16_t>(u16(d, q + 48 + 2 * i));
        }
      } else if (id == "strf" && cur) {
        cur->format.assign(d.begin() + static_cast<std::ptrdiff_t>(q),
                           d.begin() + static_cast<std::ptrdiff_t>(std::min(q + n, d.size())));
      } else if (id == "idx1" && movi) {
        // Offsets are from the movi list's type word, or absolute in some
        // writers: the first entry's chunk id says which.
        const uint32_t first = n >= 16 ? u32(d, q + 8) : 0;
        const size_t base = first + 4 <= d.size() && std::memcmp(&d[first], &d[q], 4) == 0 ? 0 : movi;
        for (size_t e = q; e + 16 <= q + n; e += 16) {
          const std::string cid(reinterpret_cast<const char*>(&d[e]), 4);
          const uint32_t fl = u32(d, e + 4), off = u32(d, e + 8), size = u32(d, e + 12);
          if (cid.compare(2, 2, "dc") == 0 || cid.compare(2, 2, "db") == 0) {
            if (fl & 0x10) out->keys.push_back(out->video_chunks);
            ++out->video_chunks;
          } else if (cid.compare(2, 2, "wb") == 0) {
            const size_t at = base + off + 8;
            if (at + size <= d.size()) out->sound.insert(out->sound.end(), d.begin() + static_cast<std::ptrdiff_t>(at),
                                                         d.begin() + static_cast<std::ptrdiff_t>(at + size));
          }
        }
      }
      p = q + n + (n & 1);
    }
    for (auto it = lists.rbegin(); it != lists.rend(); ++it) todo.push_back(*it);
  }
  if (out->video.type.empty()) {
    *err = "no video stream";
    return false;
  }
  return true;
}

void write_sidecar(const std::string& path, const Avi& a) {
  std::FILE* f = std::fopen(path.c_str(), "wb");  // "\n" lines on every system: the sets match
  if (!f) return;
  std::fprintf(f, "; mw3import: the AVI's stream headers (docs/fmv.md, The imported movies)\n");
  std::fprintf(f, "fmv 1\n");
  std::fprintf(f, "avih %u %u %u %u %u\n", a.us_per_frame, a.frames, a.suggested, a.width, a.height);
  auto word = [](std::string w) {  // a FOURCC as one token
    if (w.empty() || w[0] == 0) return std::string("-");
    for (char& c : w)
      if (c == ' ' || c == 0) c = '_';
    return w;
  };
  auto strh = [&](const char* tag, const StreamHeader& s) {
    std::fprintf(f, "%s %s %s %u %u %u %u %u %u %u %u %u %u %u %d %d %d %d\n", tag, s.type.c_str(),
                 word(s.handler).c_str(), s.flags, s.priority, s.language, s.initial, s.scale, s.rate,
                 s.start, s.length, s.suggested, s.quality, s.sample_size, s.frame[0], s.frame[1], s.frame[2],
                 s.frame[3]);
    std::fprintf(f, "%sf", tag);
    for (uint8_t b : s.format) std::fprintf(f, " %02x", b);
    std::fprintf(f, "\n");
  };
  strh("video", a.video);
  if (a.has_audio) strh("audio", a.audio);
  std::fprintf(f, "keys %zu", a.keys.size());
  for (uint32_t k : a.keys) std::fprintf(f, " %u", k);
  std::fprintf(f, "\n");
  std::fclose(f);
}

void write_wav(const std::string& path, const Avi& a) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return;
  auto le32 = [&](uint32_t v) {
    uint8_t b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
    std::fwrite(b, 1, 4, f);
  };
  const uint32_t fmt = static_cast<uint32_t>(a.audio.format.size());
  const uint32_t data = static_cast<uint32_t>(a.sound.size());
  std::fwrite("RIFF", 1, 4, f);
  le32(4 + 8 + fmt + (fmt & 1) + 8 + data + (data & 1));
  std::fwrite("WAVEfmt ", 1, 8, f);
  le32(fmt);
  std::fwrite(a.audio.format.data(), 1, fmt, f);
  if (fmt & 1) std::fputc(0, f);
  std::fwrite("data", 1, 4, f);
  le32(data);
  std::fwrite(a.sound.data(), 1, data, f);
  if (data & 1) std::fputc(0, f);
  std::fclose(f);
}

// The picture: each source frame (the last the decoder gave, for packets it
// gives none for) encoded twice at 30 fps.
bool write_m1v(const std::string& avi, const std::string& out_path, uint32_t frames, std::string* err) {
  AVFormatContext* in = nullptr;
  if (avformat_open_input(&in, avi.c_str(), nullptr, nullptr) < 0) return *err = "cannot open", false;
  avformat_find_stream_info(in, nullptr);
  const int vs = av_find_best_stream(in, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (vs < 0) return avformat_close_input(&in), *err = "no video", false;
  const AVCodec* dec = avcodec_find_decoder(in->streams[vs]->codecpar->codec_id);
  AVCodecContext* dc = dec ? avcodec_alloc_context3(dec) : nullptr;
  if (!dc || avcodec_parameters_to_context(dc, in->streams[vs]->codecpar) < 0 || avcodec_open2(dc, dec, nullptr) < 0)
    return avformat_close_input(&in), *err = "no Indeo 5 decoder", false;
  const int w = dc->width, h = dc->height;

  const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_MPEG1VIDEO);
  AVCodecContext* ec = avcodec_alloc_context3(enc);
  ec->width = w, ec->height = h, ec->pix_fmt = AV_PIX_FMT_YUV420P;
  ec->time_base = AVRational{1, 30}, ec->framerate = AVRational{30, 1};
  ec->gop_size = 30, ec->max_b_frames = 0;
  ec->flags |= AV_CODEC_FLAG_QSCALE;
  ec->global_quality = FF_QP2LAMBDA * 2;
  AVFormatContext* out = nullptr;
  avformat_alloc_output_context2(&out, nullptr, "mpeg1video", out_path.c_str());
  if (!out) return *err = "no MPEG-1 video muxer", false;
  if (out->oformat->flags & AVFMT_GLOBALHEADER) ec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  if (avcodec_open2(ec, enc, nullptr) < 0) return *err = "no MPEG-1 encoder", false;
  AVStream* os = avformat_new_stream(out, nullptr);
  avcodec_parameters_from_context(os->codecpar, ec);
  os->time_base = ec->time_base;
  if (avio_open(&out->pb, out_path.c_str(), AVIO_FLAG_WRITE) < 0) return *err = "cannot write", false;
  if (avformat_write_header(out, nullptr) < 0) return *err = "header", false;

  AVFrame* got = av_frame_alloc();
  AVFrame* cur = av_frame_alloc();
  cur->format = AV_PIX_FMT_YUV420P, cur->width = w, cur->height = h;
  av_frame_get_buffer(cur, 0);
  // Black until the first picture.
  for (int y = 0; y < h; ++y) std::memset(cur->data[0] + y * cur->linesize[0], 16, static_cast<size_t>(w));
  for (int p = 1; p < 3; ++p)
    for (int y = 0; y < h / 2; ++y) std::memset(cur->data[p] + y * cur->linesize[p], 128, static_cast<size_t>(w / 2));
  SwsContext* sws = nullptr;
  AVPacket* pkt = av_packet_alloc();
  AVPacket* opkt = av_packet_alloc();
  int64_t pts = 0;
  auto drain = [&](bool flush) {
    if (flush) avcodec_send_frame(ec, nullptr);
    while (avcodec_receive_packet(ec, opkt) == 0) {
      av_packet_rescale_ts(opkt, ec->time_base, os->time_base);
      opkt->stream_index = os->index;
      av_interleaved_write_frame(out, opkt);
    }
  };
  auto emit = [&]() {
    for (int i = 0; i < 2; ++i) {
      av_frame_make_writable(cur);
      cur->pts = pts++;
      cur->quality = ec->global_quality;
      avcodec_send_frame(ec, cur);
      drain(false);
    }
  };
  uint32_t done = 0;
  while (done < frames && av_read_frame(in, pkt) >= 0) {
    if (pkt->stream_index != vs) {
      av_packet_unref(pkt);
      continue;
    }
    avcodec_send_packet(dc, pkt);
    av_packet_unref(pkt);
    if (avcodec_receive_frame(dc, got) == 0) {
      sws = sws_getCachedContext(sws, got->width, got->height, static_cast<AVPixelFormat>(got->format), w, h,
                                 AV_PIX_FMT_YUV420P, SWS_BICUBIC, nullptr, nullptr, nullptr);
      av_frame_make_writable(cur);
      sws_scale(sws, got->data, got->linesize, 0, got->height, cur->data, cur->linesize);
      av_frame_unref(got);
    }
    emit();
    ++done;
  }
  while (done < frames) emit(), ++done;  // a short stream holds its last picture
  drain(true);
  av_write_trailer(out);
  avio_closep(&out->pb);
  sws_freeContext(sws);
  av_packet_free(&pkt);
  av_packet_free(&opkt);
  av_frame_free(&got);
  av_frame_free(&cur);
  avcodec_free_context(&dc);
  avcodec_free_context(&ec);
  avformat_free_context(out);
  avformat_close_input(&in);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef MW3_GAME_DIR
  std::string install = MW3_GAME_DIR, write_dir, only, cue;  // a development build's copy
#else
  std::string install, write_dir, only, cue;  // a player's build: --install, else mw3.cfg's
#endif
#ifndef MW3_VERSION
#define MW3_VERSION "unknown"
#endif
  bool missing = false, music_only = false, shapes = false;
  for (int i = 1; i < argc; ++i) {
    const std::string s = argv[i];
    if (s == "--version") {  // the build and the FFmpeg it runs on (its configuration: the libraries in it)
      std::printf("mw3import (MechWarrior 3 rebuild) %s\nFFmpeg libavcodec %s, %s\nconfiguration: %s\n", MW3_VERSION,
                  av_version_info(), avcodec_license(), avcodec_configuration());
      return 0;
    }
    if (s == "--install" && i + 1 < argc) install = argv[++i];
    else if (s == "--write-dir" && i + 1 < argc) write_dir = argv[++i];
    else if (s == "--only" && i + 1 < argc) only = argv[++i];
    else if (s == "--cue" && i + 1 < argc) cue = argv[++i];
    else if (s == "--missing") missing = true;
    else if (s == "--music-only") music_only = true;
    else if (s == "--shapes") shapes = true;
    else {
      std::fprintf(stderr,
                   "usage: mw3import [--install DIR] [--write-dir DIR] [--only NAME] [--missing]\n"
                   "                 [--cue DISC.cue [--music-only]] [--shapes] [--version]\n");
      return 2;
    }
  }
  if (write_dir.empty() && !sdlfe::data_folder_at_start(&write_dir, false)) return 1;
  if (write_dir.empty()) return std::fprintf(stderr, "mw3import: no write dir\n"), 1;
  std::setvbuf(stdout, nullptr, _IONBF, 0);  // mw3 follows the progress lines as they come
  int failed = 0;
  // The disc's CD audio as music\trackNN.wav (docs/fmv.md, Music).
  if (!cue.empty()) {
    std::string err;
    const int32_t n = platform::write_cue_audio(cue, (fs::path(write_dir) / "music").string(), [](int32_t t, uint64_t bytes) {
      std::printf("mw3import: music track%02d.wav, %.1f s\n", t, static_cast<double>(bytes) / (44100.0 * 4));
    }, &err);
    if (n < 0 || !err.empty()) std::fprintf(stderr, "mw3import: %s: %s\n", cue.c_str(), err.c_str()), ++failed;
    std::printf("mw3import: %d music tracks into %s\n", n < 0 ? 0 : n, (fs::path(write_dir) / "music").string().c_str());
    if (music_only) return failed ? 1 : 0;
  }
  if (install.empty()) {  // the one mw3 found and saved (mw3.cfg's install=)
    std::ifstream cfg(fs::path(write_dir) / "mw3.cfg");
    std::string line;
    while (std::getline(cfg, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.rfind("install=", 0) == 0) install = line.substr(8);
    }
  }
  if (install.empty())
    return std::fprintf(stderr, "mw3import: no install: give --install DIR, or start mw3 once so it finds it\n"), 1;
  const std::string any_video = zutl::any_case(install, "video");
  const fs::path src = any_video.empty() ? fs::path(install) / "video" : fs::path(any_video), dst = fs::path(write_dir) / "video";
  std::error_code ec;
  fs::create_directories(dst, ec);
  av_log_set_level(AV_LOG_ERROR);
  // The movies to make: every video\*.AVI (lower-cased), or with --missing
  // the ones without a .fmv yet.
  std::vector<std::pair<fs::path, std::string>> todo;
  for (const auto& e : fs::directory_iterator(src, ec)) {
    std::string ext = e.path().extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext != ".avi") continue;
    std::string name = e.path().stem().string();
    for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (!only.empty() && name != only) continue;
    if (missing && fs::exists(dst / (name + ".fmv"), ec)) continue;
    todo.emplace_back(e.path(), name);
  }
  std::sort(todo.begin(), todo.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
  std::printf("progress 0 %zu\n", todo.size());
  int made = 0;
  for (size_t k = 0; k < todo.size(); ++k) {
    const auto& [path, name] = todo[k];
    Avi a;
    std::string err;
    const fs::path base = dst / name;
    if (!parse_avi(path.string(), &a, &err) || !write_m1v(path.string(), base.string() + ".m1v", a.video.length, &err)) {
      std::fprintf(stderr, "mw3import: %s: %s\n", path.string().c_str(), err.c_str());
      ++failed;
    } else {
      if (a.has_audio) write_wav(base.string() + ".wav", a);
      write_sidecar(base.string() + ".fmv", a);  // last: its presence marks the movie done
      std::printf("mw3import: %s: %u frames (%zu keys), %zu sound bytes\n", name.c_str(), a.video.length,
                  a.keys.size(), a.sound.size());
      ++made;
    }
    std::printf("progress %zu %zu %s\n", k + 1, todo.size(), name.c_str());
  }
  std::printf("mw3import: %d movies into %s%s\n", made, dst.string().c_str(), failed ? ", some failed" : "");
  for (size_t k = 0; shapes && k < todo.size(); ++k) {
    const std::string line = mech3::measure_imported_movie((dst / todo[k].second).string());
    if (line.empty()) std::fprintf(stderr, "mw3import: %s: not measured\n", todo[k].second.c_str()), ++failed;
    else std::printf("shape %s\n", line.c_str());
  }
  return failed ? 1 : 0;
}
