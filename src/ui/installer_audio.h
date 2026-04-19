#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

struct SDL_AudioStream;

namespace reblue::ui {

// Plays a single looping MP3 from an in-memory buffer using SDL3 audio.
// Constructed before Runtime exists, so it manages the SDL audio subsystem
// lifecycle itself via SDL_InitSubSystem/SDL_QuitSubSystem.
//
// Safe to construct; if audio init fails, the instance is silent and owns
// nothing - any later lifetime calls are no-ops.
class InstallerAudio {
 public:
  InstallerAudio(const uint8_t* mp3_data, size_t mp3_size, float gain);
  ~InstallerAudio();

  InstallerAudio(const InstallerAudio&) = delete;
  InstallerAudio& operator=(const InstallerAudio&) = delete;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace reblue::ui
