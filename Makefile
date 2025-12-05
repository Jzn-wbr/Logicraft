APP := messercraft
SRC := main.cpp world.cpp render.cpp
OBJ := $(SRC:.cpp=.o)

CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter
# Adjust include/lib paths if SDL2/GL/GLEW are not in the default search paths.
LDFLAGS := -lSDL2 -lglew32 -lopengl32 -lglu32

all: $(APP)

$(APP): $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

.PHONY: clean run

run: $(APP)
	./$(APP)

clean:
	$(RM) $(OBJ) $(APP).exe $(APP)
