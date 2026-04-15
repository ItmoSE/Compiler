CXX      := g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -pedantic

TARGET := app
SRCS   := main.cpp lexer.cpp analyzer.cpp interpreter.cpp 
OBJS   := $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

main.o: main.cpp lexer.hpp parser.hpp ast.hpp analyzer.hpp interpreter.hpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
