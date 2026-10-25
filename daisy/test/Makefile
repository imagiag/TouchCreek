# Host tools for the portable engine (Mac/Linux compiler, no Daisy needed).
#   make            build + run host_test, then compare C++ vs JS (compare.js)
#   make renders    render every scenario in ../../scenarios to ../../renders/
CXX ?= c++
VERSION := $(shell git describe --always --dirty 2>/dev/null || echo dev)
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -DCREEK_VERSION=\"$(VERSION)\"
ENGINE = $(wildcard ../creek/*.cpp)
HEADERS = $(wildcard ../creek/*.h) scenario.h

all: run

host_test: host_test.cpp $(ENGINE) $(HEADERS)
	$(CXX) $(CXXFLAGS) host_test.cpp $(ENGINE) -o host_test

creek_render: creek_render.cpp $(ENGINE) $(HEADERS)
	$(CXX) $(CXXFLAGS) creek_render.cpp $(ENGINE) -o creek_render

run: host_test creek_render
	mkdir -p out
	./host_test
	node compare.js

renders: creek_render
	mkdir -p ../../renders
	for f in ../../scenarios/*.txt; do ./creek_render $$f -o ../../renders/$$(basename $$f .txt).wav || exit 1; done

clean:
	rm -rf host_test creek_render out

.PHONY: all run renders clean
