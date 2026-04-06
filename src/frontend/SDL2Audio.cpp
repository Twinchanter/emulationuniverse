#include "SDL2Audio.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace GB {

SDL2Audio::SDL2Audio() {
    SDL_AudioSpec want{};
    want.freq = 48000;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = &SDL2Audio::audioCallback;
    want.userdata = this;

    SDL_AudioSpec have{};
    m_device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (m_device == 0) {
        throw std::runtime_error(std::string("SDL_OpenAudioDevice failed: ") + SDL_GetError());
    }
    SDL_PauseAudioDevice(m_device, 0);
}

SDL2Audio::~SDL2Audio() {
    if (m_device != 0) {
        SDL_CloseAudioDevice(m_device);
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void SDL2Audio::pushSample(float left, float right) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push_back(std::clamp(left, -1.0f, 1.0f));
    m_queue.push_back(std::clamp(right, -1.0f, 1.0f));

    // Prevent unbounded queue growth if emulation outruns playback.
    constexpr size_t MAX_FLOATS = 48000;
    while (m_queue.size() > MAX_FLOATS) {
        m_queue.pop_front();
    }
}

void SDL2Audio::audioCallback(void* userdata, Uint8* stream, int len) {
    static_cast<SDL2Audio*>(userdata)->mix(stream, len);
}

void SDL2Audio::mix(Uint8* stream, int len) {
    std::lock_guard<std::mutex> lock(m_mutex);
    float* out = reinterpret_cast<float*>(stream);
    const int samples = len / static_cast<int>(sizeof(float));

    for (int i = 0; i < samples; ++i) {
        if (!m_queue.empty()) {
            out[i] = m_queue.front();
            m_queue.pop_front();
        } else {
            out[i] = 0.0f;
        }
    }
}

} // namespace GB
