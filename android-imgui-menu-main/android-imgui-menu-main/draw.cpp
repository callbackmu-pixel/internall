#include "font.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_android.h"
#include "imgui/imgui_impl_opengl3.h"
#include "xhook/xhook.h"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/asset_manager.h>
#include <dlfcn.h>
#include <jni.h>
#include <pthread.h>

typedef EGLBoolean (*eglSwapBuffersFunc)(EGLDisplay display, EGLSurface surface);

static eglSwapBuffersFunc original_eglSwapBuffers = NULL;
static bool init = false;
static ImVec4 clear_color = ImVec4(0, 0, 0, 0);
static double g_Time = 0;
int dpi_scale = 3;                    // Adjustable DPI scaling for different devices
static bool setup = false;
static bool show_demo_window = false;
int glWidth, glHeight;

// Added features: toggles for ESP Line and ESP Box
static bool esp_line = false;
static bool esp_box = false;

// Custom NewFrame (standard ImGui Android one often fails in injected context)
void Android_NewFrame() {
  ImGuiIO &io = ImGui::GetIO();
  int32_t window_width = glWidth;
  int32_t window_height = glHeight;
  int display_width = window_width;
  int display_height = window_height;
  io.DisplaySize = ImVec2((float)window_width, (float)window_height);
  if (window_width > 0 && window_height > 0)
    io.DisplayFramebufferScale = ImVec2((float)display_width / window_width,
                                        (float)display_height / window_height);
  struct timespec current_timespec;
  clock_gettime(CLOCK_MONOTONIC, &current_timespec);
  double current_time = (double)(current_timespec.tv_sec) +
                        (current_timespec.tv_nsec / 1000000000.0);
  io.DeltaTime = g_Time > 0.0 ? (float)(current_time - g_Time) : (float)(1.0f / 60.0f);
  g_Time = current_time;
}

void DrawMenu() {
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (show_demo_window) ImGui::ShowDemoWindow(&show_demo_window);
  ImGui::SetNextWindowSize(ImVec2(250 * dpi_scale, 100 * dpi_scale));
  ImGui::Begin("Discord : Claudiu_Antonio");
  // Added checkboxes for ESP features
  ImGui::Checkbox("ESP Line", &esp_line);
  ImGui::Checkbox("ESP Box", &esp_box);
  ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
              1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
  // Add your own widgets here (sliders, buttons, etc.)
  ImGui::End();
}

void DrawESP() {
  ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
  if (esp_line) {
    // Draw sample ESP lines (e.g., crosshair or lines to "entities")
    // For demo: draw a red line from top-left to bottom-right
    draw_list->AddLine(ImVec2(0, 0), ImVec2(glWidth, glHeight), IM_COL32(255, 0, 0, 255), 2.0f);
    // Add more lines as needed, in real ESP, use projected world coords
    draw_list->AddLine(ImVec2(glWidth, 0), ImVec2(0, glHeight), IM_COL32(255, 0, 0, 255), 2.0f);
  }
  if (esp_box) {
    // Draw sample ESP boxes (e.g., around imaginary players)
    // For demo: draw a green box in the center
    float box_size = 100.0f;
    ImVec2 center = ImVec2(glWidth / 2.0f, glHeight / 2.0f);
    draw_list->AddRect(ImVec2(center.x - box_size, center.y - box_size),
                       ImVec2(center.x + box_size, center.y + box_size),
                       IM_COL32(0, 255, 0, 255), 0.0f, 0, 2.0f);
    // Add more boxes as needed, in real ESP, calculate per entity
  }
}

void DrawImGuiMenu() {
  if (init) {
    ImGui_ImplOpenGL3_NewFrame();
    Android_NewFrame();
    ImGui::NewFrame();
    DrawMenu();
    // Draw ESP features if enabled
    DrawESP();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  }
}

void SetupImGui() {
  if (!init) {
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)glWidth, (float)glHeight);
    io.IniFilename = NULL;  // No config file in injected context
    ImGui::StyleColorsDark();
    ImGui_ImplAndroid_Init(nullptr);
    ImGui_ImplOpenGL3_Init("#version 300 es");

    // Embedded font (high-quality Inter font)
    ImFontConfig font_cfg;
    font_cfg.SizePixels = 22.0f;
    io.Fonts->AddFontFromMemoryTTF(inter_medium, sizeof(inter_medium),
                                   16 * dpi_scale, &font_cfg,
                                   io.Fonts->GetGlyphRangesCyrillic());
    init = true;
  }
}

// Hooked rendering entry point (called every frame by target app)
EGLBoolean my_eglSwapBuffers(EGLDisplay display, EGLSurface surface) {
  eglQuerySurface(display, surface, EGL_WIDTH, &glWidth);
  eglQuerySurface(display, surface, EGL_HEIGHT, &glHeight);
  if (!setup) SetupImGui();
  setup = true;
  DrawImGuiMenu();
  return original_eglSwapBuffers(display, surface);
}

// Input hook (passes MotionEvents to ImGui so menu is interactive)
#define HOOK(ret, func, ...) ret (*orig##func)(__VA_ARGS__); ret my##func(__VA_ARGS__)
HOOK(void, Input, void *thiz, void *ex_ab, void *ex_ac) {
  origInput(thiz, ex_ab, ex_ac);
  ImGui_ImplAndroid_HandleInputEvent((AInputEvent *)thiz);
}

void hook_init() {
  // Hook any .so that uses EGL (most games)
  xhook_register(".*\\.so$", "eglSwapBuffers", (void *)my_eglSwapBuffers,
                 (void **)&original_eglSwapBuffers);
  // Hook native input for touch/keyboard compatibility
  xhook_register(".*\\.so$",
                 "_ZN7android13InputConsumer21initializeMotionEventEPNS_"
                 "11MotionEventEPKNS_12InputMessageE",
                 (void *)myInput, (void **)&origInput);
  xhook_refresh(1);
}

void *main_thread(void *) {
  hook_init();
  pthread_exit(NULL);
}

// Constructor + JNI entry (called on injection)
__attribute__((constructor)) void _init() {}
extern "C" jint JNIEXPORT JNI_OnLoad(JavaVM *vm, void *key) {
  if (key != (void *)1337) return JNI_VERSION_1_6;  // Magic key (passed by some injectors)
  pthread_t t;
  pthread_create(&t, 0, main_thread, 0);
  return JNI_VERSION_1_6;
}