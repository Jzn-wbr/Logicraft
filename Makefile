APP := sigmacraft
SRC := main.cpp world.cpp render.cpp
OBJ := $(SRC:.cpp=.o)

CXX := g++
# CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter
# # Adjust include/lib paths if SDL2/GL/GLEW are not in the default search paths.
# LDFLAGS := -lSDL2 -lglew32 -lopengl32 -lglu32

# Point these to your SDKs if needed (examples):
#   SDL2_INC = -I"C:/msys64/mingw64/include"
#   SDL2_LIB = -L"C:/msys64/mingw64/lib"
#   GLEW_INC = -I"C:/libs/glew/include"
#   GLEW_LIB = -L"C:/libs/glew/lib"
SDL2_INC ?=
SDL2_LIB ?=
GLEW_INC ?=
GLEW_LIB ?=

CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter $(SDL2_INC) $(GLEW_INC)
LDFLAGS := $(SDL2_LIB) $(GLEW_LIB) -lSDL2 -lglew32 -lopengl32 -lglu32

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

# mingw32-make SDL2_INC=-IC:/msys64/mingw64/include SDL2_LIB=-LC:/msys64/mingw64/lib GLEW_INC=-IC:/msys64/mingw64/include GLEW_LIB=-LC:/msys64/mingw64/lib