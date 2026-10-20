# make        builds everything into build/
# make test   builds and runs the tests
CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic
LDLIBS   := -pthread
BUILD    := build

SRC     := $(wildcard src/*.cpp)
OBJ     := $(SRC:src/%.cpp=$(BUILD)/%.o)
TESTS   := $(patsubst tests/%.cpp,$(BUILD)/%,$(wildcard tests/test_*.cpp))
TOOLS   := $(patsubst tools/%.cpp,$(BUILD)/%,$(wildcard tools/*.cpp))
BENCHES := $(patsubst bench/%.cpp,$(BUILD)/%,$(wildcard bench/*.cpp))

.PHONY: all test clean
.SECONDARY:

all: $(TOOLS) $(TESTS) $(BENCHES)

$(BUILD)/%.o: src/%.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%: tools/%.cpp $(OBJ) $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc $< $(OBJ) -o $@ $(LDLIBS)

$(BUILD)/test_%: tests/test_%.cpp $(OBJ) $(wildcard src/*.h tests/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc $< $(OBJ) -o $@ $(LDLIBS)

$(BUILD)/bench_%: bench/bench_%.cpp $(OBJ) $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc $< $(OBJ) -o $@ $(LDLIBS)

test: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)
