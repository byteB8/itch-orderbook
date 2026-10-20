# make        builds everything into build/
# make test   builds and runs the tests
# make sanitize  runs the tests under AddressSanitizer and UBSan, and the queue test under ThreadSanitizer
CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic
LDLIBS   := -pthread
BUILD    := build

SRC     := $(wildcard src/*.cpp)
OBJ     := $(SRC:src/%.cpp=$(BUILD)/%.o)
TESTS   := $(patsubst tests/%.cpp,$(BUILD)/%,$(wildcard tests/test_*.cpp))
TOOLS   := $(patsubst tools/%.cpp,$(BUILD)/%,$(wildcard tools/*.cpp))
BENCHES := $(patsubst bench/%.cpp,$(BUILD)/%,$(wildcard bench/*.cpp))

.PHONY: all test sanitize clean
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

SANFLAGS := -std=c++20 -O1 -g -fno-omit-frame-pointer -Wall -Wextra

sanitize:
	$(MAKE) --no-print-directory BUILD=build/asan CXXFLAGS="$(SANFLAGS) -fsanitize=address,undefined" test
	$(MAKE) --no-print-directory BUILD=build/tsan CXXFLAGS="$(SANFLAGS) -fsanitize=thread" build/tsan/test_spsc_queue
	setarch $$(uname -m) -R ./build/tsan/test_spsc_queue short  # ASLR off: TSan cannot map its shadow memory on recent kernels

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)
