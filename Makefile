CXX = g++
RC = windres
CXXFLAGS = -O2 -Wall -std=c++17 -MMD -MP -static-libgcc -static-libstdc++ -static -municode $(CXXFLAGS_EXTRA)
LIBS = -lgdiplus -lgdi32 -luser32 -lkernel32 -lcomctl32 -lshell32 -lws2_32 -lole32 -luuid -lwinmm -liphlpapi -mwindows

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
	Core/DebugConsole.o \
	Core/InputManager.o \
	Core/LoaderConfig.o \
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
RC_OBJS = TranslationRes.o
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

TRANSLATION_OUT = Translation/zh-TW.ini
ifneq ($(wildcard Translation/_src/zh-TW.ini),)
ifneq ($(wildcard $(TRANSLATION_OUT)),)
TRANSLATION_DEPS = Translation/_src/zh-TW.ini $(wildcard Pet/lang/zh-TW.ini) $(wildcard Modules/*/lang/zh-TW.ini)
else
$(info $(TRANSLATION_OUT) missing, running Build.py...)
DUMMY2 := $(shell py Build.py)
endif
endif

DEPS = $(CPP_OBJS:.o=.d)

.PHONY: all test clean debug rebuild distclean

all: $(TARGET)

$(TARGET): $(ALL_OBJS)
	$(CXX) $(CXXFLAGS) $(ALL_OBJS) -o $(TARGET) $(LIBS) $(LIBS_EXTRA)

ifneq ($(TRANSLATION_DEPS),)
$(TRANSLATION_OUT): $(TRANSLATION_DEPS)
	py Build.py
endif

TranslationRes.o: $(TRANSLATION_OUT)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.rc
	$(RC) $< -O coff -o $@

TEST_CXXFLAGS = -O0 -g -Wall -std=c++17 -DENABLE_DEBUG_STATE=1 -static-libgcc -static-libstdc++ -static
TEST_LIBS = -luser32 -lkernel32

TEST_TARGETS = Debug/AutoKey/AutoKeyTest.exe Debug/UI/TooltipTest.exe Debug/Core/DebugStateTest.exe

test: $(TEST_TARGETS)
	@for %%t in ($(TEST_TARGETS)) do (echo === %%t === && "%%t")

Debug/UI/TooltipTest.exe: Debug/UI/TooltipTest.cpp
	$(CXX) $(TEST_CXXFLAGS) -I. $^ -o $@ $(TEST_LIBS) -lcomctl32

Debug/AutoKey/AutoKeyTest.exe: Debug/AutoKey/AutoKeyTest.cpp Modules/AutoKey/AutoKeyParser.cpp
	$(CXX) $(TEST_CXXFLAGS) -I. $^ -o $@ $(TEST_LIBS)

Debug/Core/DebugStateTest.exe: Debug/Core/DebugStateTest.cpp Core/ModuleManager.cpp
	$(CXX) $(TEST_CXXFLAGS) -I. $^ -o $@ $(TEST_LIBS)

-include $(DEPS)

clean:
	-@del /f "$(TARGET)" 2>nul
	-@del /f /q $(subst /,\,$(ALL_OBJS)) 2>nul
	-@del /f /q $(subst /,\,$(DEPS)) 2>nul

debug: clean
	$(MAKE) CXXFLAGS_EXTRA=-DDEBUG_CONSOLE=1

rebuild: clean all

distclean: clean
	-@del /f "GeneratedBuild.mk" 2>nul
	-@del /f "GeneratedIcon.ico" 2>nul
	-@del /f "GeneratedIcon.rc" 2>nul
	-@del /f "GeneratedIcon.o" 2>nul
	-@del /f "Translation\zh-TW.ini" 2>nul
	-@del /f "Build.log" 2>nul

testshell:
	@echo SHELL='\'
