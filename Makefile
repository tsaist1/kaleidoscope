CXX = clang++
CXXFLAGS = -g -O3 `llvm-config --cxxflags`

all: parser

parser: parser.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f parser
