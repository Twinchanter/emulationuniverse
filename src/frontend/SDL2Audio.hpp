#pragma once

#include <SDL.h>

#include <deque>
#include <mutex>

namespace GB {

class SDL2Audio {
public:
    SDL2Audio();
    ~SDL2Audio();

    SDL2Audio(const SDL2Audio&) = delete;
    SDL2Audio& operator=(const SDL2Audio&) = delete;

    void pushSample(float left, float right);

private:
    static void audioCallback(void* userdata, Uint8* stream, int len);
    void mix(Uint8* stream, int len);

    SDL_AudioDeviceID     m_device = 0;
    std::mutex            m_mutex;
    std::deque<float>     m_queue; // interleaved stereo floats
};

} // namespace GB
