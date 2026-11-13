// GUI simulator: LVGL 9 in an 800x480 SDL2 window, mouse = touch, the panel's src/gui/ code with
// mock data (sim/mock.cpp). Keys toggle states (h = help).
//
// --script "w1500;c400,240;k1;s/tmp/a.ppm;q" drives it without a person: wN = wait N ms,
// cX,Y = click, kC = key, sFILE = screenshot (PPM, window pixels incl. modals), q = quit.

#include <SDL2/SDL.h>
#include <lvgl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "gui/fonts.h"
#include "gui/gui.h"
#include "gui/theme.h"
#include "mock.h"

namespace {
// Keys from the SDL event watch, handled in the main loop (not inside lv_timer_handler)
char s_keys[16];
volatile int s_keyCount = 0;
volatile bool s_quit = false;

int onSdlEvent(void*, SDL_Event* e) {
    if (e->type == SDL_QUIT) s_quit = true;
    if (e->type == SDL_KEYDOWN && s_keyCount < (int)sizeof(s_keys)) {
        SDL_Keycode k = e->key.keysym.sym;
        if ((k >= '0' && k <= '9') || (k >= 'a' && k <= 'z')) s_keys[s_keyCount++] = (char)k;
    }
    return 0;
}

uint32_t tick() { return SDL_GetTicks(); }

bool saveShot(lv_display_t* disp, const char* path) {
    auto* r = static_cast<SDL_Renderer*>(lv_sdl_window_get_renderer(disp));
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(r, &w, &h);
    std::vector<uint8_t> px((size_t)w * h * 3);
    if (SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_RGB24, px.data(), w * 3) != 0) return false;
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    fwrite(px.data(), 1, px.size(), f);
    fclose(f);
    return true;
}

void pushClick(lv_display_t* disp, int x, int y, bool down) {
    auto* r = static_cast<SDL_Renderer*>(lv_sdl_window_get_renderer(disp));
    SDL_Event e = {};
    e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    e.button.windowID = SDL_GetWindowID(SDL_RenderGetWindow(r));
    e.button.button = SDL_BUTTON_LEFT;
    e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
    e.button.x = x;
    e.button.y = y;
    SDL_PushEvent(&e);
}

// Scripted run (--script): one step at a time, a click = press, 80 ms, release
struct Script {
    std::vector<std::string> steps;
    size_t i = 0;
    uint32_t waitUntil = 0;
    int releaseX = -1, releaseY = -1;

    void run(lv_display_t* disp, Mock& mock, uint32_t now) {
        if (now < waitUntil) return;
        if (releaseX >= 0) {
            pushClick(disp, releaseX, releaseY, false);
            releaseX = -1;
            waitUntil = now + 150;
            return;
        }
        if (i >= steps.size()) return;
        const std::string& s = steps[i++];
        if (s.empty()) return;
        switch (s[0]) {
            case 'w': waitUntil = now + (uint32_t)atoi(s.c_str() + 1); break;
            case 'k': mock.key(s[1]); waitUntil = now + 100; break;
            case 'c': {
                int x = 0, y = 0;
                sscanf(s.c_str() + 1, "%d,%d", &x, &y);
                pushClick(disp, x, y, true);
                releaseX = x;
                releaseY = y;
                waitUntil = now + 80;
                break;
            }
            case 's': printf("shot %s: %s\n", s.c_str() + 1, saveShot(disp, s.c_str() + 1) ? "ok" : "FAILED"); break;
            case 'q': s_quit = true; break;
            default: printf("script: unknown step '%s'\n", s.c_str());
        }
    }
};
}  // namespace

int main(int argc, char** argv) {
    Script script;
    for (int a = 1; a + 1 < argc; a++) {
        if (strcmp(argv[a], "--script") != 0) continue;
        std::string all = argv[a + 1], cur;
        for (char ch : all) {
            if (ch == ';') {
                script.steps.push_back(cur);
                cur.clear();
            } else {
                cur += ch;
            }
        }
        if (!cur.empty()) script.steps.push_back(cur);
    }

    lv_init();
    lv_tick_set_cb(tick);
    lv_display_t* disp = lv_sdl_window_create(gui::SCREEN_W, gui::SCREEN_H);
    lv_sdl_window_set_title(disp, "Panel akwarium — symulator GUI");
    lv_sdl_mouse_create();
    lv_sysmon_hide_performance(disp);  // PC frame rate says nothing about the panel
    SDL_AddEventWatch(onSdlEvent, nullptr);

    gui::initFonts();
    gui::applyTheme(disp);

    static Mock mock;
    mock.init();
    gui::begin(&mock.model, &mock);
    Mock::help();

    while (!s_quit) {
        lv_timer_handler();
        for (int i = 0; i < s_keyCount; i++) mock.key(s_keys[i]);
        s_keyCount = 0;
        script.run(disp, mock, SDL_GetTicks());
        mock.tick(SDL_GetTicks());
        gui::update();
        SDL_Delay(5);
    }
    return 0;
}
