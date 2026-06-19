CXX = g++
RC = windres
CXXFLAGS = -O2 -Wall -std=c++17 -MMD -MP -static-libgcc -static-libstdc++ -municode
LIBS = -lgdiplus -lgdi32 -luser32 -lkernel32 -lcomctl32 -lshell32 -mwindows

# Auto-regenerate build/translation files if missing
ifeq ($(wildcard GeneratedBuild.mk),)
$(info GeneratedBuild.mk missing, running Build.py...)
DUMMY := $(shell py Build.py)
endif

-include GeneratedBuild.mk

ifndef TARGET
TARGET = Project.exe
endif

ifndef CPP_OBJS
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
	Services/TranslationService.o \
	Pet/MainWindow.o \
	Pet/PetConfig.o \
	UI/SettingsDialog.o
endif

ifndef RC_OBJS
RC_OBJS =
endif

ifndef GENERATED_ICON_OBJ
GENERATED_ICON_OBJ =
endif

ifndef LIBS_EXTRA
LIBS_EXTRA =
endif

ALL_OBJS = $(CPP_OBJS) $(RC_OBJS)
ifneq ($(strip $(GENERATED_ICON_OBJ)),)
ALL_OBJS += $(GENERATED_ICON_OBJ)
endif

DEPS = $(CPP_OBJS:.o=.d)

.PHONY: all clean rebuild

all: $(TARGET)

$(TARGET): $(ALL_OBJS)
	$(CXX) $(CXXFLAGS) $(ALL_OBJS) -o $(TARGET) $(LIBS) $(LIBS_EXTRA)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.rc
	$(RC) $< -O coff -o $@

-include $(DEPS)

clean:
	-@del /f "$(TARGET)" 2>nul
	-@for %%f in ($(ALL_OBJS)) do del /f "%%f" 2>nul
	-@for %%f in ($(DEPS)) do del /f "%%f" 2>nul

rebuild: clean all
