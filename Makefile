CXX      = g++
SDL_CFLAGS  := $(shell sdl2-config --cflags)   # gives -I/usr/include/SDL2 etc.
SDL_LIBS    := $(shell sdl2-config --libs)      # gives -lSDL2
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -Isrc/imgui $(SDL_CFLAGS)
TARGET = chip8
SOURCES = src/main.cpp src/chip8.cpp \
          src/imgui/imgui.cpp \
          src/imgui/imgui_draw.cpp \
          src/imgui/imgui_tables.cpp \
          src/imgui/imgui_widgets.cpp \
          src/imgui/imgui_demo.cpp \
          src/imgui/imgui_impl_sdl2.cpp \
          src/imgui/imgui_impl_sdlrenderer2.cpp
OBJECTS = $(SOURCES:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJECTS) $(SDL_LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean