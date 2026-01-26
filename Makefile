TARGET := test
BUILD_DIR := build

# List your C and C++ source files
SRC_C   := test.c vk_cmd.c helpers.c vk_startup.c vk_sync.c vk_queue.c \
		   vk_descriptor.c vk_descriptor_freq.c vk_descriptor_bindless.c \
		   vk_pipeline_layout.c vk_pipelines.c vk_shader_reflect.c render_object.c \
		   vk_swapchain.c volk.c vk_resources.c desc_write.c  vk_debug_text.c gpu_timer.c camera.c scene.c \
		   bindlesstextures.c proceduraltextures.c vk_gui.c offset_allocator.c

SRC_CPP :=tracy.cpp vma.cpp  $(wildcard external/meshoptimizer/src/*.cpp) \
		   external/cimgui/cimgui.cpp external/cimgui/cimgui_impl.cpp \
		   external/cimgui/imgui/imgui.cpp external/cimgui/imgui/imgui_draw.cpp \
		   external/cimgui/imgui/imgui_demo.cpp external/cimgui/imgui/imgui_tables.cpp \
		   external/cimgui/imgui/imgui_widgets.cpp \
		   external/cimgui/imgui/backends/imgui_impl_glfw.cpp \
		   external/cimgui/imgui/backends/imgui_impl_vulkan.cpp

# Compiler flags
# Compiler flags
CFLAGS   := -std=gnu99 -ggdb  


CXXFLAGS := -std=c++17 -w -g -fno-common -DIMGUI_IMPL_VULKAN_NO_PROTOTYPES \
		   -DIMGUI_IMPL_API='extern "C"' \
		   -Iexternal/cimgui -Iexternal/cimgui/imgui -Iexternal/cimgui/imgui/backends
LDFLAGS  :=
LIBS := -lvulkan -lm -lglfw -lX11 -lXi -lXrandr -lXcursor -lXinerama -ldl -lpthread
# 1. Transform source names to build folder object names
# This turns 'test.c' into 'build/test.o'
OBJ := $(addprefix $(BUILD_DIR)/, $(SRC_C:.c=.o) $(SRC_CPP:.cpp=.o))

# Default rule
all: $(TARGET)

# Link the executable using the objects in the build folder
$(TARGET): $(OBJ)
	@echo Linking $@
	$(CXX) $(OBJ) $(LDFLAGS) -o $@ $(LIBS)

# 2. Rule for C files
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	@echo Compiling $<
	$(CC) $(CFLAGS) -c $< -o $@

# 3. Rule for C++ files
$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	@echo Compiling $<
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 4. Create the build directory if it doesn't exist
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	@echo Cleaning...
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all clean
.PHONY: all clean
