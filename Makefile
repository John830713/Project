CXX = g++
CXXFLAGS = -O2 -Wall -std=c++17 -MMD -MP -static-libgcc -static-libstdc++ -municode
LIBS = -lgdiplus -lgdi32 -luser32 -lkernel32 -lcomctl32 -lshell32 -mwindows

TARGET = Project.exe
CPP_OBJS = \
	main.o \
	HostApp.o \
	Core/ConfigManager.o \
	Core/InputManager.o \
	Core/Logger.o \
	Core/ModuleManager.o \
	Services/ChecksumService.o \
	Services/ClipboardService.o \
	Services/FileService.o \
	Services/TranslationService.o

all: $(TARGET)

$(TARGET): $(CPP_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(CPP_OBJS:.o=.d)

clean:
	-@del /f $(TARGET) 2>nul
	-@for %%f in ($(CPP_OBJS)) do del /f "%%f" 2>nul

.PHONY: all clean
