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
#include <filesystem>
#include <iostream>
// Dear ImGui — core + SDL2 / SDLRenderer2 backends
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <cmath>

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
  static uint32_t global_sample = 0;
  int16_t *audio_buffer = (int16_t *)stream;
  int samples = len / 2;
  const int AMPLITUDE = 1200; // Softer volume for background music
  const double SAMPLE_RATE = 44100.0;

  // A soothing, upbeat 16-note retro 8-bit arpeggio background music loop
  const double melody[] = {
      523.25, 659.25, 783.99, 880.00,  // C5, E5, G5, A5
      783.99, 659.25, 523.25, 392.00,  // G5, E5, C5, G4
      440.00, 523.25, 659.25, 783.99,  // A4, C5, E5, G5
      659.25, 523.25, 440.00, 392.00   // E5, C5, A4, G4
  };
  const int num_notes = 16;
  const uint32_t samples_per_note = 11025; // ~0.25 seconds per note at 44.1kHz
  const uint32_t total_loop_samples = num_notes * samples_per_note;

  bool *beeping = (bool *)userdata;

  for (int i = 0; i < samples; i++) {
    // 1. Generate Background Music (Smooth Triangle Wave)
    uint32_t loop_pos = global_sample % total_loop_samples;
    int note_index = loop_pos / samples_per_note;
    double freq = melody[note_index];

    uint32_t note_sample = loop_pos % samples_per_note;
    double period = SAMPLE_RATE / freq;
    double phase = std::fmod((double)note_sample, period) / period;
    
    int16_t bgm_value = 0;
    if (phase < 0.5) {
      bgm_value = AMPLITUDE * (4.0 * phase - 1.0);
    } else {
      bgm_value = AMPLITUDE * (3.0 - 4.0 * phase);
    }

    // 2. Layer the crisp 8-bit single beep sound effect when active
    if (beeping && *beeping) {
      const double sfx_frequency = 880.0; // Crisp arcade beep pitch
      double sfx_period = SAMPLE_RATE / sfx_frequency;
      bool sfx_high = std::fmod((double)global_sample, sfx_period) < (sfx_period / 2.0);
      
      // Snappy volume envelope to keep the beep clean
      double sfx_progress = (double)(global_sample % (int)SAMPLE_RATE) / SAMPLE_RATE;
      double sfx_envelope = 1.0 - (sfx_progress * 2.0);
      if (sfx_envelope < 0.2) sfx_envelope = 0.2;

      int16_t sfx_value = sfx_high ? (2000 * sfx_envelope) : (-2000 * sfx_envelope);
      
      // Blend BGM and SFX together so they don't clip
      audio_buffer[i] = (bgm_value / 2) + (sfx_value / 2);
    } else {
      audio_buffer[i] = bgm_value;
    }

    global_sample++;
  }
}

void draw_graphics(SDL_Renderer *renderer, Chip8 &chip8,
                   DisplayPalette palette, bool rom_loaded) {
  const Palette &current = kPalettes[static_cast<int>(palette)];

  SDL_SetRenderDrawColor(renderer, current.background.r, current.background.g,
                         current.background.b, current.background.a);
  SDL_RenderClear(renderer);

  if (!rom_loaded)
    return; // Just leave the clear background

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
    std::cerr << "No ROM provided — use the in-game browser to load one.\n";
    // Don't exit: the ROM browser lets the user pick one at runtime.
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
  bool rom_loaded = false;
  if (argc > 1) {
    chip8.load_rom(argv[1]); // optional: may also be loaded via the ROM browser
    rom_loaded = true;
  }

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

    if (ImGui::Button("Save State")) {
      chip8.saveState("savestate.dat");
    }

    if (ImGui::Button("Load State")) {
      chip8.loadState("savestate.dat");
    }

    if (ImGui::Button("Quit Emulator")) {
      running = false;
    }

    // --- ROM Browser ---
    ImGui::Separator();
    ImGui::Text("Available ROMs:");
    try {
      for (const auto &entry :
           std::filesystem::directory_iterator("roms")) {
        if (!entry.is_regular_file())
          continue;
        std::string filename = entry.path().filename().string();
        if (ImGui::Button(filename.c_str())) {
          chip8.load_rom(entry.path().string().c_str());
          rom_loaded = true;
        }
      }
    } catch (const std::filesystem::filesystem_error &) {
      ImGui::TextDisabled("(roms/ folder not found)");
    }

    ImGui::End();

    // 2. Run the CHIP-8 CPU only when a ROM is loaded and not paused.
    if (!is_paused && rom_loaded) {
      for (int i = 0; i < cycles_per_frame; i++) {
        chip8.emulate_cycle(); // CPU speed depends on cycles_per_frame
      }
      chip8.tick_timers(); // timers at correct 60Hz, decoupled from CPU rate
      beeping = (chip8.get_sound_timer() > 0);
    } else {
      beeping = false; // silence audio when paused or no ROM loaded
    }

    // 3. Draw CHIP-8 pixels onto the SDL render target.
    draw_graphics(renderer, chip8, palette, rom_loaded);

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