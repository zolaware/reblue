#include "ui/installer_audio.h"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <dr_mp3.h>

#include <cstring>
#include <mutex>
#include <vector>

#include "bdengine/common/logging.h"

namespace reblue::ui {

struct InstallerAudio::Impl {
  drmp3 decoder{};
  bool decoder_ready = false;

  SDL_AudioStream* stream = nullptr;
  bool audio_subsystem_inited = false;

  // Guards decoder access so that SDL's pull callback and the destructor cannot
  // race. SDL_DestroyAudioStream waits for any in-flight callback, but we still
  // lock here so the EOF seek is atomic w.r.t. future callbacks.
  std::mutex mutex;
  std::vector<int16_t> scratch;

  static void SDLCALL FeedCallback(void* userdata, SDL_AudioStream* stream,
                                   int additional_amount, int /*total_amount*/) {
    auto* self = static_cast<Impl*>(userdata);
    if (additional_amount <= 0) return;

    std::lock_guard<std::mutex> lock(self->mutex);
    if (!self->decoder_ready) return;

    const int channels = static_cast<int>(self->decoder.channels);
    const int frame_bytes = channels * static_cast<int>(sizeof(int16_t));
    if (frame_bytes <= 0) return;

    int frames_needed = (additional_amount + frame_bytes - 1) / frame_bytes;
    if (static_cast<int>(self->scratch.size()) < frames_needed * channels) {
      self->scratch.resize(static_cast<size_t>(frames_needed) * channels);
    }

    int frames_filled = 0;
    while (frames_filled < frames_needed) {
      const drmp3_uint64 want = static_cast<drmp3_uint64>(frames_needed - frames_filled);
      const drmp3_uint64 got = drmp3_read_pcm_frames_s16(
          &self->decoder, want,
          self->scratch.data() + static_cast<size_t>(frames_filled) * channels);
      if (got == 0) {
        // EOF - seek back to the start and keep filling so the loop boundary
        // is seamless within a single callback.
        if (!drmp3_seek_to_pcm_frame(&self->decoder, 0)) break;
        continue;
      }
      frames_filled += static_cast<int>(got);
    }

    if (frames_filled > 0) {
      SDL_PutAudioStreamData(stream, self->scratch.data(), frames_filled * frame_bytes);
    }
  }
};

InstallerAudio::InstallerAudio(const uint8_t* mp3_data, size_t mp3_size, float gain)
    : impl_(std::make_unique<Impl>()) {
  if (!mp3_data || mp3_size == 0) {
    BD_WARN("InstallerAudio: empty MP3 blob");
    return;
  }

  if (!drmp3_init_memory(&impl_->decoder, mp3_data, mp3_size, nullptr)) {
    BD_WARN("InstallerAudio: drmp3_init_memory failed");
    return;
  }
  impl_->decoder_ready = true;

  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    BD_WARN("InstallerAudio: SDL_InitSubSystem(AUDIO) failed: {}", SDL_GetError());
    drmp3_uninit(&impl_->decoder);
    impl_->decoder_ready = false;
    return;
  }
  impl_->audio_subsystem_inited = true;

  SDL_AudioSpec spec{};
  spec.format = SDL_AUDIO_S16;
  spec.channels = static_cast<int>(impl_->decoder.channels);
  spec.freq = static_cast<int>(impl_->decoder.sampleRate);

  impl_->stream = SDL_OpenAudioDeviceStream(
      SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &Impl::FeedCallback, impl_.get());
  if (!impl_->stream) {
    BD_WARN("InstallerAudio: SDL_OpenAudioDeviceStream failed: {}", SDL_GetError());
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    impl_->audio_subsystem_inited = false;
    drmp3_uninit(&impl_->decoder);
    impl_->decoder_ready = false;
    return;
  }

  SDL_SetAudioStreamGain(impl_->stream, gain);
  if (!SDL_ResumeAudioStreamDevice(impl_->stream)) {
    BD_WARN("InstallerAudio: SDL_ResumeAudioStreamDevice failed: {}", SDL_GetError());
  }
  BD_INFO("InstallerAudio: playing {} Hz, {} ch", spec.freq, spec.channels);
}

InstallerAudio::~InstallerAudio() {
  if (impl_->stream) {
    // Destroying the stream unbinds it from the device and waits for any
    // in-flight callback to finish before returning.
    SDL_DestroyAudioStream(impl_->stream);
    impl_->stream = nullptr;
  }
  if (impl_->decoder_ready) {
    drmp3_uninit(&impl_->decoder);
    impl_->decoder_ready = false;
  }
  if (impl_->audio_subsystem_inited) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    impl_->audio_subsystem_inited = false;
  }
}

}  // namespace reblue::ui
