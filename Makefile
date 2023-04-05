# Compiler and linker flags
CXX = g++                              # Use g++ as the compiler
CXXFLAGS = -std=c++17 -Wall -Wextra    # Enable C++17 standard, and enable some warning flags
LDFLAGS =                              # Linker flags (empty for this program)

# Name of the target executable
TARGET = main

# Source files
SOURCES = main.cpp

# Object files (one for each source file)
OBJECTS = $(SOURCES:.cpp=.o)

# Default target: build the program
all: $(TARGET)

# Rule to build the target executable
$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $^

# Rule to build object files from source files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Clean up the build artifacts
clean:
	rm -f $(OBJECTS) $(TARGET)

# Run the program
run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
