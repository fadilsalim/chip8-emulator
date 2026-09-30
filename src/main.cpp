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
// Dear ImGui — core + SDL2 / SDLRenderer2 backends
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

const int SCALE = 10; // Each pixel is 10x10 screen pixels
const int WIDTH = 64 * SCALE;
const int HEIGHT = 32 * SCALE;

enum class DisplayPalette {
  OriginalBw,
  ClassicGreen,
  AmberCrt,
  NeonHighContrast
};

struct Palette {
  const char *name;
  SDL_Color background;
  SDL_Color foreground;
};

const Palette kPalettes[] = {
    {"Original Black & White", {0, 0, 0, 255}, {255, 255, 255, 255}},
    {"Classic Green", {0, 30, 0, 255}, {0, 255, 0, 255}},
    {"Amber CRT", {25, 12, 0, 255}, {255, 180, 60, 255}},
    {"Neon High Contrast", {10, 10, 18, 255}, {255, 0, 255, 255}},
};

const int kPaletteCount = sizeof(kPalettes) / sizeof(kPalettes[0]);

DisplayPalette next_palette(DisplayPalette palette) {
  int index = static_cast<int>(palette);
  index = (index + 1) % kPaletteCount;
  return static_cast<DisplayPalette>(index);
}

void apply_palette(SDL_Renderer *renderer, DisplayPalette palette) {
  const Palette &current = kPalettes[static_cast<int>(palette)];
  SDL_SetRenderDrawColor(renderer, current.background.r, current.background.g,
                         current.background.b, current.background.a);
  std::cout << "Display palette: " << current.name << std::endl;
}

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

void draw_graphics(SDL_Renderer *renderer, Chip8 &chip8,
                   DisplayPalette palette) {
  const Palette &current = kPalettes[static_cast<int>(palette)];

  SDL_SetRenderDrawColor(renderer, current.background.r, current.background.g,
                         current.background.b, current.background.a);
  SDL_RenderClear(renderer);

  SDL_SetRenderDrawColor(renderer, current.foreground.r, current.foreground.g,
                         current.foreground.b, current.foreground.a);
  for (int y = 0; y < 32; y++) {
    for (int x = 0; x < 64; x++) {
      if (chip8.display[x + (y * 64)] == 1) {
        SDL_Rect rect = {x * SCALE, y * SCALE, SCALE,
                         SCALE}; // y=0 is top in both CHIP-8 and SDL
        SDL_RenderFillRect(renderer, &rect);
      }
    }
  }
  // NOTE: SDL_RenderPresent is intentionally NOT called here.
  // ImGui renders on top of the CHIP-8 pixels, so present happens after
  // ImGui_ImplSDLRenderer2_RenderDrawData() in the main loop.
}

void handle_input(SDL_Renderer *renderer, Chip8 &chip8, bool &running,
                  DisplayPalette &palette, int &cycles_per_frame) {
  SDL_Event event;

  while (SDL_PollEvent(&event)) {
    // Let ImGui consume the event first so its widgets stay responsive.
    ImGui_ImplSDL2_ProcessEvent(&event);
    if (event.type == SDL_QUIT)
      running = false;
    if (event.type == SDL_KEYDOWN) {
      if (event.key.keysym.sym == SDLK_ESCAPE)
        running = false;
      // --- Ishwar's Save/Load Controls ---
      switch (event.key.keysym.sym) {
      case SDLK_m: // Save state
        chip8.saveState("savestate.dat");
        break;
      case SDLK_n: // Load state
        chip8.loadState("savestate.dat");
        break;
      }

      // --- Navjyoth's Palette Controls ---
      if (event.key.keysym.sym == SDLK_F1) {
        palette = DisplayPalette::OriginalBw;
        apply_palette(renderer, palette);
      } else if (event.key.keysym.sym == SDLK_F2) {
        palette = DisplayPalette::ClassicGreen;
        apply_palette(renderer, palette);
      } else if (event.key.keysym.sym == SDLK_F3) {
        palette = DisplayPalette::AmberCrt;
        apply_palette(renderer, palette);
      } else if (event.key.keysym.sym == SDLK_F4) {
        palette = DisplayPalette::NeonHighContrast;
        apply_palette(renderer, palette);
      } else if (event.key.keysym.sym == SDLK_p) {
        palette = next_palette(palette);
        apply_palette(renderer, palette);
      }
      // My speed controls
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

  // --- ImGui initialisation ---
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui::StyleColorsDark();
  ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer2_Init(renderer);

  Chip8 chip8;
  chip8.load_rom(argv[1]);

  DisplayPalette palette = DisplayPalette::OriginalBw;
  apply_palette(renderer, palette);

  bool running = true;
  int cycles_per_frame = 10;

  while (running) {
    handle_input(renderer, chip8, running, palette, cycles_per_frame);

    // 1. Start the ImGui frame FIRST so is_paused is known before CPU runs.
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // ImGui::Begin returns false (and collapses the window) when minimised.
    // We repurpose the expanded state as a pause signal: settings open = paused.
    bool is_settings_open = ImGui::Begin("Settings");
    bool is_paused = is_settings_open; // paused while the panel is expanded

    ImGui::SliderInt("Speed", &cycles_per_frame, 1, 100);

    if (ImGui::Button("Cycle Palette")) {
      palette = next_palette(palette);
      apply_palette(renderer, palette);
    }

    if (ImGui::Button("Quit Emulator")) {
      running = false;
    }

    ImGui::End();

    // 2. Run the CHIP-8 CPU only when not paused.
    if (!is_paused) {
      for (int i = 0; i < cycles_per_frame; i++) {
        chip8.emulate_cycle(); // CPU speed depends on cycles_per_frame
      }
      chip8.tick_timers(); // timers at correct 60Hz, decoupled from CPU rate
      beeping = (chip8.get_sound_timer() > 0);
    } else {
      beeping = false; // silence audio so the tone doesn't loop while paused
    }

    // 3. Draw CHIP-8 pixels onto the SDL render target.
    draw_graphics(renderer, chip8, palette);

    // 4. Overlay ImGui and present both in one flip.
    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);

    SDL_Delay(16); // ~60 FPS
  }

  // --- ImGui shutdown ---
  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  if (audio_device != 0)
    SDL_CloseAudioDevice(audio_device);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}