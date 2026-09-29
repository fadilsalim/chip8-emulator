#include "chip8.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_audio.h>
#include <SDL2/SDL_error.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_rect.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_stdinc.h>
#include <SDL2/SDL_timer.h>
#include <SDL2/SDL_video.h>
#include <cstdint>
#include <iostream>

const int SCALE = 10; // Each pixel is 10x10 screen pixels
const int WIDTH = 64 * SCALE;
const int HEIGHT = 32 * SCALE;

// Keyboard mapping — SDL_Keycode is int32_t; uint8_t would silently truncate
// values > 0xFF (e.g. arrow keys), and causes type-mismatch in comparisons.
SDL_Keycode keymap[16] = {
    SDLK_x, // 0
    SDLK_1, // 1
    SDLK_2, // 2
    SDLK_3, // 3
    SDLK_q, // 4
    SDLK_w, // 5
    SDLK_e, // 6
    SDLK_a, // 7
    SDLK_s, // 8
    SDLK_d, // 9
    SDLK_z, // A
    SDLK_c, // B
    SDLK_4, // C
    SDLK_r, // D
    SDLK_f, // E
    SDLK_v  // F
};

void audio_callback(void *userdata, uint8_t *stream, int len) {
  static uint32_t sample_index = 0;
  int16_t *audio_buffer = (int16_t *)stream;
  int samples = len / 2;

  bool *beeping = (bool *)userdata;
  for (int i = 0; i < samples; i++) {
    if (*beeping) {
      // Generating 440Hz sqaure wave
      int16_t value = ((sample_index++ / 100) % 2) ? 3000 : -3000;
      audio_buffer[i] = value;
    } else {
      audio_buffer[i] = 0; // Silence
      sample_index = 0;
    }
  }
}

void draw_graphics(SDL_Renderer *renderer, Chip8 &chip8) {
  // Clear screen
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);
  // Drawing white pixels
  SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
  for (int y = 0; y < 32; y++) {
    for (int x = 0; x < 64; x++) {
      if (chip8.display[x + (y * 64)] == 1) {
        SDL_Rect rect = {x * SCALE, y * SCALE, SCALE,
                         SCALE}; // y=0 is top in both CHIP-8 and SDL
        SDL_RenderFillRect(renderer, &rect);
      }
    }
  }
  SDL_RenderPresent(renderer);
}

void handle_input(Chip8 &chip8, bool &running, int &cycles_per_frame) {
  SDL_Event event;

  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_QUIT)
      running = false;
    if (event.type == SDL_KEYDOWN) {
      if (event.key.keysym.sym == SDLK_ESCAPE)
        running = false;
      if (event.key.keysym.sym == SDLK_MINUS) {
        if (cycles_per_frame > 1)
          cycles_per_frame--;
        std::cout << "Emulation speed: " << cycles_per_frame
                  << " cycles/frame\n";
      }
      if (event.key.keysym.sym == SDLK_EQUALS ||
          event.key.keysym.sym == SDLK_PLUS) {
        if (cycles_per_frame < 100)
          cycles_per_frame++;
        std::cout << "Emulation speed: " << cycles_per_frame
                  << " cycles/frame\n";
      }
      // Check which Chip-8 key was pressed
      for (int i = 0; i < 16; i++) {
        if (event.key.keysym.sym == keymap[i])
          chip8.key[i] = 1;
      }
    }
    if (event.type == SDL_KEYUP) {
      for (int i = 0; i < 16; i++) {
        if (event.key.keysym.sym == keymap[i])
          chip8.key[i] = 0;
      }
    }
  }
}

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <ROM file>" << std::endl;
    return 1;
  }
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
    std::cerr << "SDL Error: " << SDL_GetError() << std::endl;
    return 1;
  }
  // Audio setup
  bool beeping = false;
  SDL_AudioSpec want, have;
  SDL_zero(want);
  want.freq = 44100;
  want.format = AUDIO_S16SYS;
  want.channels = 1;
  want.samples = 2048;
  want.callback = audio_callback;
  want.userdata = &beeping;

  SDL_AudioDeviceID audio_device =
      SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if (audio_device == 0)
    std::cerr << "Failed to open audio: " << SDL_GetError() << std::endl;
  else
    SDL_PauseAudioDevice(audio_device, 0);

  SDL_Window *window =
      SDL_CreateWindow("Chip-8 Emulator", SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT, SDL_WINDOW_SHOWN);
  if (!window) {
    std::cerr << "Window error: " << SDL_GetError() << std::endl;
    SDL_Quit();
    return 1;
  }
  SDL_Renderer *renderer =
      SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer) {
    std::cerr << "Renderer error: " << SDL_GetError() << std::endl;
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  // Disable the OS text-input / IME pipeline so that holding letter or digit
  // keys does not trigger accent chooser menus (KDE/GNOME on Linux/macOS).
  SDL_StopTextInput();

  Chip8 chip8;
  chip8.load_rom(argv[1]);

  bool running = true;
  int cycles_per_frame = 10;
  while (running) {
    handle_input(chip8, running, cycles_per_frame);
    for (int i = 0; i < cycles_per_frame; i++) {
      chip8.emulate_cycle(); // CPU speed depends on cycles_per_frame
    }
    chip8.tick_timers(); // timers at correct 60Hz, decoupled from CPU rate
    beeping = (chip8.get_sound_timer() > 0);
    draw_graphics(renderer, chip8);
    SDL_Delay(16); // ~60 FPS
  }
  if (audio_device != 0)
    SDL_CloseAudioDevice(audio_device);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}