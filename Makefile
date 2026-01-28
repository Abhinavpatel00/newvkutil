TARGET := test

BUILD_DIR   := build
RELEASE_DIR := release

# =========================
# Sources
# =========================
SRC_C := test.c vk_cmd.c helpers.c vk_startup.c vk_sync.c vk_queue.c \
         vk_descriptor.c vk_descriptor_freq.c vk_descriptor_bindless.c \
         vk_pipeline_layout.c vk_pipelines.c vk_shader_reflect.c render_object.c \
         vk_swapchain.c volk.c vk_resources.c desc_write.c vk_debug_text.c gpu_timer.c \
         camera.c scene.c bindlesstextures.c proceduraltextures.c vk_gui.c offset_allocator.c

SRC_CPP := tracy.cpp vma.cpp $(wildcard external/meshoptimizer/src/*.cpp) \
           external/cimgui/cimgui.cpp external/cimgui/cimgui_impl.cpp \
           external/cimgui/imgui/imgui.cpp external/cimgui/imgui/imgui_draw.cpp \
           external/cimgui/imgui/imgui_demo.cpp external/cimgui/imgui/imgui_tables.cpp \
           external/cimgui/imgui/imgui_widgets.cpp \
           external/cimgui/imgui/backends/imgui_impl_glfw.cpp \
           external/cimgui/imgui/backends/imgui_impl_vulkan.cpp

# =========================
# Common flags
# =========================
COMMON_CFLAGS   := -std=gnu99 -fno-common
COMMON_CXXFLAGS := -std=c++17 -w -fno-common \
                   -DIMGUI_IMPL_VULKAN_NO_PROTOTYPES \
                   -DIMGUI_IMPL_API='extern "C"' \
                   -Iexternal/cimgui -Iexternal/cimgui/imgui \
                   -Iexternal/cimgui/imgui/backends

LIBS := -lvulkan -lm -lglfw -lX11 -lXi -lXrandr -lXcursor -lXinerama -ldl -lpthread

# =========================
# Debug flags (default)
# =========================
CFLAGS   := $(COMMON_CFLAGS) -O0 -g -DDEBUG
CXXFLAGS := $(COMMON_CXXFLAGS) -O0 -g -DDEBUG
LDFLAGS  :=

# =========================
# Release flags (EXTREME)
# =========================
RELEASE_CFLAGS := $(COMMON_CFLAGS) \
    -O3 -DNDEBUG -march=native -mtune=native \
    -flto -funroll-loops -fomit-frame-pointer \
    -ffast-math -fno-math-errno

RELEASE_CXXFLAGS := $(COMMON_CXXFLAGS) \
    -O3 -DNDEBUG -march=native -mtune=native \
    -flto -funroll-loops -fomit-frame-pointer \
    -ffast-math -fno-math-errno

RELEASE_LDFLAGS := -flto -Wl,--as-needed

# =========================
# Objects
# =========================
OBJ := $(addprefix $(BUILD_DIR)/, $(SRC_C:.c=.o) $(SRC_CPP:.cpp=.o))
RELEASE_OBJ := $(addprefix $(RELEASE_DIR)/, $(SRC_C:.c=.o) $(SRC_CPP:.cpp=.o))

# =========================
# Targets
# =========================
all: debug

debug: $(TARGET)

release: CFLAGS=$(RELEASE_CFLAGS)
release: CXXFLAGS=$(RELEASE_CXXFLAGS)
release: LDFLAGS=$(RELEASE_LDFLAGS)
release: $(RELEASE_DIR)/$(TARGET)

# =========================
# Linking
# =========================
$(TARGET): $(OBJ)
	@echo Linking DEBUG $@
	$(CXX) $^ $(LDFLAGS) -o $@ $(LIBS)

$(RELEASE_DIR)/$(TARGET): $(RELEASE_OBJ)
	@echo Linking RELEASE $@
	$(CXX) $^ $(LDFLAGS) -o $@ $(LIBS)

# =========================
# Compilation rules
# =========================
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(RELEASE_DIR)/%.o: %.c | $(RELEASE_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(RELEASE_DIR)/%.o: %.cpp | $(RELEASE_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# =========================
# Dirs
# =========================
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(RELEASE_DIR):
	mkdir -p $(RELEASE_DIR)

clean:
	rm -rf $(BUILD_DIR) $(RELEASE_DIR) $(TARGET)

.PHONY: all debug release clean





