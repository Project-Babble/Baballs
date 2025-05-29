MAKEFLAGS += -j --no-print-directory
.SILENT .SUFFIXES .SECONDARY:
OBJDIR ::= .obj

sinclude makefile.user

BUILD_TYPE ?= Release

COMMON_SOURCES ::= math_utils.cpp capture_reader.cpp numpy_io.cpp
OVERLAY_SOURCES ::= dashboard_ui.cpp frame_buffer.cpp jpeg_stream.c main.cpp opengl_context.cpp \
	overlay_manager.cpp rest_server.cpp routine.cpp stb_truetype.c subprocess.cpp trainer_progress.cpp trainer_wrapper.cpp
TRAINER_SOURCES ::= trainer.cpp

CSTDFLAGS ::= -fvisibility=hidden -ffunction-sections -fdata-sections -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable
CXXFLAGS ::= -std=c++14
CFLAGS ::= -std=gnu11
LDFLAGS ::= -Wl,--gc-sections,--fatal-warnings -static-libgcc -static-libstdc++ $(shell pkg-config --libs libturbojpeg libonnxruntime)
ifeq ($(BUILD_TYPE),Debug)
	CSTDFLAGS ::= $(CSTDFLAGS) -Og -DDEBUG -Werror
	LDFLAGS ::= $(LDFLAGS) -g
else
	CSTDFLAGS ::= $(CSTDFLAGS) -O3 -DNDEBUG
	LDFLAGS ::= $(LDFLAGS) -s
endif

all: gaze_overlay trainer

trainer: $(TRAINER_SOURCES:%=$(OBJDIR)/%.o)
gaze_overlay: $(OVERLAY_SOURCES:%=$(OBJDIR)/%.o)
gaze_overlay: LDFLAGS ::= $(LDFLAGS) $(shell pkg-config --libs openvr egl gl)
gaze_overlay trainer: $(COMMON_SOURCES:%=$(OBJDIR)/%.o)
	@echo '[ld $(notdir $@)]'
	$(CXX) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/%.c.o: %.c
	@echo '[cc $(notdir $@)]'
	@mkdir -p '$(@D)'
	$(CC) -c $< -o $@ $(CFLAGS) $(CSTDFLAGS) -MMD -MP

$(OBJDIR)/%.cpp.o: %.cpp
	@echo '[cxx $(notdir $@)]'
	@mkdir -p '$(@D)'
	$(CXX) -c $< -o $@ $(CXXFLAGS) $(CSTDFLAGS) -MMD -MP

clean:
	@echo '[rm .obj/ gaze_overlay trainer]'
	$(RM) -r .obj/ gaze_overlay trainer

.PHONY: all clean

sinclude $(TRAINER_SOURCES:%=$(OBJDIR)/%.d) $(OVERLAY_SOURCES:%=$(OBJDIR)/%.d) $(COMMON_SOURCES:%=$(OBJDIR)/%.d)
