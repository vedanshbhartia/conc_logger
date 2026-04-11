CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -fopenmp -O2
INCLUDES := -I include

SRC      := src/logger.cpp
EXAMPLE  := examples/main.cpp
TARGET   := conc_logger

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC) $(EXAMPLE)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) app.log
