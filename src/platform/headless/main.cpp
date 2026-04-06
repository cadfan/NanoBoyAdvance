// SPDX-License-Identifier: GPL-3.0-or-later
// Headless capture binary for gba-accuracy-tests.
// Loads a ROM, runs N frames with optional input schedule,
// writes the final framebuffer as raw BGR555 LE (76800 bytes).
//
// Links against nba static lib only — no SDL, no Qt, no OpenGL.
// ROM is constructed inline (no save / GPIO support); accuracy tests
// don't need persistent backups.

#include <nba/core.hpp>
#include <nba/config.hpp>
#include <nba/device/video_device.hpp>
#include <nba/device/audio_device.hpp>
#include <nba/rom/rom.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int kWidth = 240;
constexpr int kHeight = 160;
constexpr int kPixels = kWidth * kHeight;

class CaptureVideoDevice : public nba::VideoDevice {
 public:
  std::array<std::uint32_t, kPixels> last_buffer{};
  bool any_frame = false;
  void Draw(std::uint32_t* buffer) final {
    std::memcpy(last_buffer.data(), buffer, kPixels * sizeof(std::uint32_t));
    any_frame = true;
  }
};

std::vector<std::uint8_t> read_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) {
    std::fprintf(stderr, "cannot open %s\n", path.c_str());
    std::exit(2);
  }
  auto size = f.tellg();
  f.seekg(0);
  std::vector<std::uint8_t> buf(static_cast<size_t>(size));
  f.read(reinterpret_cast<char*>(buf.data()), size);
  return buf;
}

// XRGB8888 native -> BGR555 LE (240*160*2 = 76800 bytes).
std::vector<std::uint8_t> to_bgr555(const std::array<std::uint32_t, kPixels>& src) {
  std::vector<std::uint8_t> out(kPixels * 2);
  for (int i = 0; i < kPixels; ++i) {
    std::uint32_t px = src[i];
    std::uint8_t r = (px >> 16) & 0xFF;
    std::uint8_t g = (px >> 8) & 0xFF;
    std::uint8_t b = px & 0xFF;
    std::uint16_t bgr =
        static_cast<std::uint16_t>(((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3));
    out[i * 2 + 0] = bgr & 0xFF;
    out[i * 2 + 1] = (bgr >> 8) & 0xFF;
  }
  return out;
}

// "frame:mask,frame:mask,..." → sorted vector of (frame, mask).
std::vector<std::pair<int, int>> parse_keys(const std::string& spec) {
  std::vector<std::pair<int, int>> out;
  size_t i = 0;
  while (i < spec.size()) {
    size_t comma = spec.find(',', i);
    std::string token =
        spec.substr(i, comma == std::string::npos ? std::string::npos : comma - i);
    size_t colon = token.find(':');
    if (colon == std::string::npos) break;
    out.emplace_back(std::stoi(token.substr(0, colon)),
                     std::stoi(token.substr(colon + 1)));
    if (comma == std::string::npos) break;
    i = comma + 1;
  }
  std::sort(out.begin(), out.end());
  return out;
}

void apply_mask(nba::CoreBase& core, int mask) {
  for (int bit = 0; bit < 10; ++bit) {
    core.SetKeyStatus(static_cast<nba::Key>(bit), (mask & (1 << bit)) != 0);
  }
}

// Round ROM size up to next power of two for rom_mask computation.
std::uint32_t round_pow2_mask(size_t size) {
  // Default GBA cart mask is 0x01FFFFFF (32 MB). Use it unless ROM is smaller.
  std::uint32_t mask = 0x01FF'FFFF;
  if (size == 0) return mask;
  size_t p = 1;
  while (p < size) p <<= 1;
  return static_cast<std::uint32_t>(p - 1);
}

}  // namespace

int main(int argc, char** argv) {
  std::string rom_path, bios_path, output_path, key_spec;
  int target_frames = 60;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
    if (a == "--rom") rom_path = next();
    else if (a == "--bios") bios_path = next();
    else if (a == "--frames") target_frames = std::stoi(next());
    else if (a == "--output") output_path = next();
    else if (a == "--keys") key_spec = next();
    else {
      std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
      return 2;
    }
  }
  if (rom_path.empty() || output_path.empty() || bios_path.empty()) {
    std::fprintf(stderr,
                 "usage: nba-headless --rom R --bios B --output O "
                 "[--frames N] [--keys frame:mask,...]\n");
    return 2;
  }

  auto config = std::make_shared<nba::Config>();
  auto capture = std::make_shared<CaptureVideoDevice>();
  config->video_dev = capture;
  config->audio_dev = std::make_shared<nba::NullAudioDevice>();

  auto core = nba::CreateCore(config);

  auto bios = read_file(bios_path);
  auto rom_bytes = read_file(rom_path);

  core->Attach(bios);

  // Construct ROM inline — no backup, no GPIO. Accuracy tests don't need
  // save persistence. See src/nba/include/nba/rom/rom.hpp for the ctor.
  std::uint32_t rom_mask = round_pow2_mask(rom_bytes.size());
  nba::ROM rom(std::move(rom_bytes), nullptr, nullptr, rom_mask);
  core->Attach(std::move(rom));

  auto schedule = parse_keys(key_spec);
  size_t sched_idx = 0;
  int current_mask = 0;

  core->Reset();
  for (int frame = 0; frame < target_frames; ++frame) {
    while (sched_idx < schedule.size() && schedule[sched_idx].first <= frame) {
      current_mask = schedule[sched_idx].second;
      ++sched_idx;
    }
    apply_mask(*core, current_mask);
    core->RunForOneFrame();
  }

  if (!capture->any_frame) {
    std::fprintf(stderr, "core emitted no frames\n");
    return 3;
  }
  auto bgr = to_bgr555(capture->last_buffer);
  std::ofstream out(output_path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bgr.data()), bgr.size());
  if (!out) {
    std::fprintf(stderr, "write failed\n");
    return 4;
  }
  return 0;
}
