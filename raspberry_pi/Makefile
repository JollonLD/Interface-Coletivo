# Makefile for UDP CSV telemetria (Raspberry Pi)

CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -I../hx711/include
LDFLAGS := -L../hx711

# Try to get system libs via pkg-config if available (optional)
GPIOD_CFLAGS := $(shell pkg-config --cflags libgpiod 2>/dev/null)
GPIOD_LIBS := $(shell pkg-config --libs libgpiod 2>/dev/null)
LGPIO_CFLAGS := $(shell pkg-config --cflags lgpio 2>/dev/null)
LGPIO_LIBS := $(shell pkg-config --libs lgpio 2>/dev/null)

# Base libs: hx711 local lib; other libs appended from pkg-config if found
LIBS := -lhx711 -lpthread $(GPIOD_LIBS) $(LGPIO_LIBS)

# Append pkg-config cflags to CXXFLAGS (if any)
CXXFLAGS += $(GPIOD_CFLAGS) $(LGPIO_CFLAGS)

SRC := src/main.cpp src/net/udp_comm.cpp src/gpio/gpio_input_reader.cpp src/sensor/hx711_adapter.cpp
OBJ := $(SRC:.cpp=.o)
TARGET := udp_csv

CXXFLAGS += -Iinclude

.PHONY: all clean strip install run

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)


%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# allow building object files in-place for src/ paths
src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@


clean:
	rm -f $(OBJ) $(TARGET)

strip: $(TARGET)
	strip --strip-all $(TARGET) || true

install: $(TARGET)
	sudo install -m 0755 $(TARGET) /usr/local/bin/$(TARGET)

run: $(TARGET)
	./$(TARGET) 127.0.0.1 12345
