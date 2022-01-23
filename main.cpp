#include <SDL.h>
#include <SDL_image.h>
#include <SDL_mixer.h>
#include <SDL_net.h>
#include <SDL_ttf.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <random>
//#include <SDL_gpu.h>
//#include <SFML/Network.hpp>
//#include <SFML/Graphics.hpp>
#include <algorithm>
#include <atomic>
#include <codecvt>
#include <functional>
#include <locale>
#include <mutex>
#include <thread>
#ifdef __ANDROID__
#include "vendor/PUGIXML/src/pugixml.hpp"
#include <android/log.h> //__android_log_print(ANDROID_LOG_VERBOSE, "SetThree", "Example number log: %d", number);
#include <jni.h>
#include "vendor/GLM/include/glm/glm.hpp"
#include "vendor/GLM/include/glm/gtc/matrix_transform.hpp"
#include "vendor/GLM/include/glm/gtc/type_ptr.hpp"
#else
#include <filesystem>
#include <pugixml.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#ifdef __EMSCRIPTEN__
namespace fs = std::__fs::filesystem;
#else
namespace fs = std::filesystem;
#endif
using namespace std::chrono_literals;
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

// NOTE: Remember to uncomment it on every release
//#define RELEASE

#if defined _MSC_VER && defined RELEASE
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
#endif

//240 x 240 (smart watch)
//240 x 320 (QVGA)
//360 x 640 (Galaxy S5)
//640 x 480 (480i - Smallest PC monitor)

int windowWidth = 240;
int windowHeight = 320;
SDL_Point mousePos;
SDL_Point realMousePos;
bool keys[SDL_NUM_SCANCODES];
bool buttons[SDL_BUTTON_X2 + 1];
SDL_Window* window;
SDL_Renderer* renderer;

void logOutputCallback(void* userdata, int category, SDL_LogPriority priority, const char* message)
{
    std::cout << message << std::endl;
}

int random(int min, int max)
{
    return min + rand() % ((max + 1) - min);
}

int SDL_QueryTextureF(SDL_Texture* texture, Uint32* format, int* access, float* w, float* h)
{
    int wi, hi;
    int result = SDL_QueryTexture(texture, format, access, &wi, &hi);
    if (w) {
        *w = wi;
    }
    if (h) {
        *h = hi;
    }
    return result;
}

SDL_bool SDL_PointInFRect(const SDL_Point* p, const SDL_FRect* r)
{
    return ((p->x >= r->x) && (p->x < (r->x + r->w)) && (p->y >= r->y) && (p->y < (r->y + r->h))) ? SDL_TRUE : SDL_FALSE;
}

std::ostream& operator<<(std::ostream& os, SDL_FRect r)
{
    os << r.x << " " << r.y << " " << r.w << " " << r.h;
    return os;
}

std::ostream& operator<<(std::ostream& os, SDL_Rect r)
{
    SDL_FRect fR;
    fR.w = r.w;
    fR.h = r.h;
    fR.x = r.x;
    fR.y = r.y;
    os << fR;
    return os;
}

SDL_Texture* renderText(SDL_Texture* previousTexture, TTF_Font* font, SDL_Renderer* renderer, const std::string& text, SDL_Color color)
{
    if (previousTexture) {
        SDL_DestroyTexture(previousTexture);
    }
    SDL_Surface* surface;
    if (text.empty()) {
        surface = TTF_RenderUTF8_Blended(font, " ", color);
    }
    else {
        surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    }
    if (surface) {
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_FreeSurface(surface);
        return texture;
    }
    else {
        return 0;
    }
}

struct Text {
    std::string text;
    SDL_Texture* t = 0;
    SDL_FRect dstR{};
    float wMultiplier = 1;
    float hMultiplier = 1;
    std::function<void()> onClickCallback;
    std::string fontStr;

    void handleEvent(SDL_Event event)
    {
        if (event.type == SDL_MOUSEBUTTONDOWN && SDL_PointInFRect(&mousePos, &dstR)) {
            if (onClickCallback) {
                onClickCallback();
            }
        }
    }

    void setText(SDL_Renderer* renderer, TTF_Font* font, std::string text, SDL_Color c = { 255, 255, 255 })
    {
        this->text = text;
        t = renderText(t, font, renderer, text, c);
    }

    void setText(SDL_Renderer* renderer, TTF_Font* font, int value, SDL_Color c = { 255, 255, 255 })
    {
        setText(renderer, font, std::to_string(value), c);
    }

    void adjustSize()
    {
        adjustSize(wMultiplier, hMultiplier);
    }

    void adjustSize(float wMultiplier, float hMultiplier)
    {
        float w, h;
        SDL_QueryTextureF(t, 0, 0, &w, &h);
        dstR.w = w * wMultiplier;
        dstR.h = h * hMultiplier;
    }

    void draw(SDL_Renderer* renderer)
    {
        SDL_RenderCopyF(renderer, t, 0, &dstR);
    }
};

void clearTexture(SDL_Renderer* renderer, SDL_Texture* texture, SDL_Color color)
{
    SDL_Texture* currT = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, texture);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderClear(renderer);
    //SDL_RenderPresent(renderer); // TOOD: Does it causes flicker on android?
    SDL_SetRenderTarget(renderer, currT);
}

SDL_Texture* createClearedTexture(SDL_Renderer* renderer, Uint32 format, int access, int width, int height, SDL_Color color)
{
    SDL_Texture* texture = SDL_CreateTexture(renderer, format, access, width, height);
    clearTexture(renderer, texture, color);
    return texture;
}

int SDL_RenderDrawCircle(SDL_Renderer* renderer, int x, int y, int radius)
{
    int offsetx, offsety, d;
    int status;

    offsetx = 0;
    offsety = radius;
    d = radius - 1;
    status = 0;

    while (offsety >= offsetx) {
        status += SDL_RenderDrawPoint(renderer, x + offsetx, y + offsety);
        status += SDL_RenderDrawPoint(renderer, x + offsety, y + offsetx);
        status += SDL_RenderDrawPoint(renderer, x - offsetx, y + offsety);
        status += SDL_RenderDrawPoint(renderer, x - offsety, y + offsetx);
        status += SDL_RenderDrawPoint(renderer, x + offsetx, y - offsety);
        status += SDL_RenderDrawPoint(renderer, x + offsety, y - offsetx);
        status += SDL_RenderDrawPoint(renderer, x - offsetx, y - offsety);
        status += SDL_RenderDrawPoint(renderer, x - offsety, y - offsetx);

        if (status < 0) {
            status = -1;
            break;
        }

        if (d >= 2 * offsetx) {
            d -= 2 * offsetx + 1;
            offsetx += 1;
        }
        else if (d < 2 * (radius - offsety)) {
            d += 2 * offsety - 1;
            offsety -= 1;
        }
        else {
            d += 2 * (offsety - offsetx - 1);
            offsety -= 1;
            offsetx += 1;
        }
    }

    return status;
}

int SDL_RenderFillCircle(SDL_Renderer* renderer, int x, int y, int radius)
{
    int offsetx, offsety, d;
    int status;

    offsetx = 0;
    offsety = radius;
    d = radius - 1;
    status = 0;

    while (offsety >= offsetx) {

        status += SDL_RenderDrawLine(renderer, x - offsety, y + offsetx,
            x + offsety, y + offsetx);
        status += SDL_RenderDrawLine(renderer, x - offsetx, y + offsety,
            x + offsetx, y + offsety);
        status += SDL_RenderDrawLine(renderer, x - offsetx, y - offsety,
            x + offsetx, y - offsety);
        status += SDL_RenderDrawLine(renderer, x - offsety, y - offsetx,
            x + offsety, y - offsetx);

        if (status < 0) {
            status = -1;
            break;
        }

        if (d >= 2 * offsetx) {
            d -= 2 * offsetx + 1;
            offsetx += 1;
        }
        else if (d < 2 * (radius - offsety)) {
            d += 2 * offsety - 1;
            offsety -= 1;
        }
        else {
            d += 2 * (offsety - offsetx - 1);
            offsety -= 1;
            offsetx += 1;
        }
    }

    return status;
}

struct Clock {
    Uint64 start = SDL_GetPerformanceCounter();

    float getElapsedTime()
    {
        Uint64 stop = SDL_GetPerformanceCounter();
        float secondsElapsed = (stop - start) / (float)SDL_GetPerformanceFrequency();
        return secondsElapsed * 1000;
    }

    float restart()
    {
        Uint64 stop = SDL_GetPerformanceCounter();
        float secondsElapsed = (stop - start) / (float)SDL_GetPerformanceFrequency();
        start = SDL_GetPerformanceCounter();
        return secondsElapsed * 1000;
    }
};

SDL_bool SDL_FRectEmpty(const SDL_FRect* r)
{
    return ((!r) || (r->w <= 0) || (r->h <= 0)) ? SDL_TRUE : SDL_FALSE;
}

SDL_bool SDL_IntersectFRect(const SDL_FRect* A, const SDL_FRect* B, SDL_FRect* result)
{
    int Amin, Amax, Bmin, Bmax;

    if (!A) {
        SDL_InvalidParamError("A");
        return SDL_FALSE;
    }

    if (!B) {
        SDL_InvalidParamError("B");
        return SDL_FALSE;
    }

    if (!result) {
        SDL_InvalidParamError("result");
        return SDL_FALSE;
    }

    /* Special cases for empty rects */
    if (SDL_FRectEmpty(A) || SDL_FRectEmpty(B)) {
        result->w = 0;
        result->h = 0;
        return SDL_FALSE;
    }

    /* Horizontal intersection */
    Amin = A->x;
    Amax = Amin + A->w;
    Bmin = B->x;
    Bmax = Bmin + B->w;
    if (Bmin > Amin)
        Amin = Bmin;
    result->x = Amin;
    if (Bmax < Amax)
        Amax = Bmax;
    result->w = Amax - Amin;

    /* Vertical intersection */
    Amin = A->y;
    Amax = Amin + A->h;
    Bmin = B->y;
    Bmax = Bmin + B->h;
    if (Bmin > Amin)
        Amin = Bmin;
    result->y = Amin;
    if (Bmax < Amax)
        Amax = Bmax;
    result->h = Amax - Amin;

    return (SDL_FRectEmpty(result) == SDL_TRUE) ? SDL_FALSE : SDL_TRUE;
}

SDL_bool SDL_HasIntersectionF(const SDL_FRect* A, const SDL_FRect* B)
{
    float Amin, Amax, Bmin, Bmax;

    if (!A) {
        SDL_InvalidParamError("A");
        return SDL_FALSE;
    }

    if (!B) {
        SDL_InvalidParamError("B");
        return SDL_FALSE;
    }

    /* Special cases for empty rects */
    if (SDL_FRectEmpty(A) || SDL_FRectEmpty(B)) {
        return SDL_FALSE;
    }

    /* Horizontal intersection */
    Amin = A->x;
    Amax = Amin + A->w;
    Bmin = B->x;
    Bmax = Bmin + B->w;
    if (Bmin > Amin)
        Amin = Bmin;
    if (Bmax < Amax)
        Amax = Bmax;
    if (Amax <= Amin)
        return SDL_FALSE;

    /* Vertical intersection */
    Amin = A->y;
    Amax = Amin + A->h;
    Bmin = B->y;
    Bmax = Bmin + B->h;
    if (Bmin > Amin)
        Amin = Bmin;
    if (Bmax < Amax)
        Amax = Bmax;
    if (Amax <= Amin)
        return SDL_FALSE;

    return SDL_TRUE;
}

int eventWatch(void* userdata, SDL_Event* event)
{
    // WARNING: Be very careful of what you do in the function, as it may run in a different thread
    if (event->type == SDL_APP_TERMINATING || event->type == SDL_APP_WILLENTERBACKGROUND) {
    }
    return 0;
}

float clamp(float n, float lower, float upper)
{
    return std::max(lower, std::min(n, upper));
}

enum class State {
    Main,
    Tutorial1,
    Tutorial2,
    Tutorial3,
    Tutorial4,
    PlantTree,
    Map,
};

struct Tutorial1 {
    Text firstText;
    Text secondText;
    Text thirdText;
    Text fourthText;
    Text fifthText;
    SDL_FRect imageR{};
    SDL_FRect arrowR{};
};

struct Tutorial2 {
    Text firstText;
    Text secondText;
    Text thirdText;
    Text fourthText;
    Text fifthText;
    SDL_FRect imageR{};
    SDL_FRect arrowR{};
};

struct Tutorial3 {
    Text firstText;
    Text secondText;
    Text thirdText;
    Text fourthText;
    Text fifthText;
    SDL_FRect imageR{};
    SDL_FRect arrowR{};
};

struct Tutorial4 {
    Text firstText;
    Text secondText;
    Text thirdText;
    Text fourthText;
    Text fifthText;
    SDL_FRect imageR{};
    SDL_FRect plantTreeBtnR{};
};

struct PlantTree {
    Text chooseTreeText;
    std::vector<SDL_FRect> treeRects;
    SDL_FRect patronsR{};
    SDL_FRect choosePlaceBtnR{};
};

struct Map {
    SDL_FRect mapR;
    SDL_FRect bannerR;
};

struct Image {
    SDL_Texture* t = 0; // WARNING: Remember to destory old one on every change if it's the only pointer to it
    SDL_FRect dstR{};
    std::string srcStr;

    void draw(SDL_Renderer* renderer)
    {
        SDL_RenderCopyF(renderer, t, 0, &dstR);
    }
};

struct Button {
    SDL_FRect dstR{};
    std::function<void()> onClickCallback;

    void handleEvent(SDL_Event event)
    {
        if (event.type == SDL_MOUSEBUTTONDOWN && SDL_PointInFRect(&mousePos, &dstR)) {
            if (onClickCallback) {
                onClickCallback();
            }
        }
    }

    void draw(SDL_Renderer* renderer)
    {
        SDL_RenderFillRectF(renderer, &dstR);
    }
};

enum class Type {
    None,
    Text,
    Image,
    Button,
};

struct HotWidget {
    Type type = Type::None;
    int selected = -1;
    SDL_Point moveHitP{};
    int lastSelected = -1;
    bool resizing = false;
    SDL_Rect lastSelectedResizeR{};
    SDL_Point resizeHitP{};
};

struct Widgets {
    std::vector<Text> texts;
    std::vector<Image> images;
    SDL_Rect r;
};

int main(int argc, char* argv[])
{
    std::srand(std::time(0));
    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_VERBOSE);
    SDL_LogSetOutputFunction(logOutputCallback, 0);
    SDL_Init(SDL_INIT_EVERYTHING);
    TTF_Init();
    SDL_GetMouseState(&mousePos.x, &mousePos.y);
    window = SDL_CreateWindow("SetTree", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, windowWidth, windowHeight, SDL_WINDOW_RESIZABLE);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    TTF_Font* robotoF = TTF_OpenFont("res/roboto.ttf", 72);
    TTF_Font* oldStandardF = TTF_OpenFont("res/OldStandardTT-Regular.ttf", 72);
    TTF_Font* heeboF = TTF_OpenFont("res/Heebo-Regular.ttf", 72);
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    SDL_RenderSetScale(renderer, w / (float)windowWidth, h / (float)windowHeight);
    SDL_AddEventWatch(eventWatch, 0);
    bool running = true;
    SDL_Texture* logoT = IMG_LoadTexture(renderer, "res/logo.png");
    SDL_Texture* hamburgerMenuT = IMG_LoadTexture(renderer, "res/hamburgerMenu.png");
    SDL_Texture* handsT = IMG_LoadTexture(renderer, "res/hands.png");
    SDL_Texture* downArrowsT = IMG_LoadTexture(renderer, "res/downArrows.png");
    SDL_Texture* tutorial1T = IMG_LoadTexture(renderer, "res/tutorial1.png");
    SDL_Texture* rightArrowT = IMG_LoadTexture(renderer, "res/rightArrow.png");
    SDL_Texture* tutorial2T = IMG_LoadTexture(renderer, "res/tutorial2.png");
    SDL_Texture* tutorial3T = IMG_LoadTexture(renderer, "res/tutorial3.png");
    SDL_Texture* tutorial4T = IMG_LoadTexture(renderer, "res/tutorial4.png");
    SDL_Texture* plantTreeButtonT = IMG_LoadTexture(renderer, "res/plantTreeButton.png");
    SDL_Texture* ordinaryOakT = IMG_LoadTexture(renderer, "res/ordinaryOak.png");
    SDL_Texture* bonsaiT = IMG_LoadTexture(renderer, "res/bonsai.png");
    SDL_Texture* patronsT = IMG_LoadTexture(renderer, "res/patrons.png");
    SDL_Texture* patrons2T = IMG_LoadTexture(renderer, "res/patrons2.png");
    SDL_Texture* choosePlaceT = IMG_LoadTexture(renderer, "res/choosePlace.png");
    SDL_Texture* mapT = IMG_LoadTexture(renderer, "res/map.png");
    SDL_FRect bannerR;
    bannerR.w = windowWidth;
    bannerR.h = 40;
    bannerR.x = 0;
    bannerR.y = 0;
    SDL_FRect logoR;
    logoR.w = 100;
    logoR.h = 30;
    logoR.x = windowWidth / 2 - logoR.w / 2;
    logoR.y = bannerR.y + bannerR.h / 2 - logoR.h / 2;
    SDL_FRect hamburgerMenuR;
    hamburgerMenuR.w = 64;
    hamburgerMenuR.h = 64;
    hamburgerMenuR.x = windowWidth - 55;
    hamburgerMenuR.y = bannerR.y + bannerR.h / 2 - hamburgerMenuR.h / 2;
    SDL_FRect handsR;
    handsR.w = 128;
    handsR.h = 128;
    handsR.x = windowWidth / 2 - handsR.w / 2;
    handsR.y = 70;
    Text plantingGreenText;
    plantingGreenText.setText(renderer, robotoF, "Sadzimy Zielone", { 0, 0, 0 });
    plantingGreenText.dstR.w = 100;
    plantingGreenText.dstR.h = 20;
    plantingGreenText.dstR.x = windowWidth / 2 - plantingGreenText.dstR.w / 2;
    plantingGreenText.dstR.y = handsR.y + handsR.h + 20;
    Text cityText;
    cityText.dstR.w = 50;
    cityText.dstR.h = 20;
    cityText.dstR.x = windowWidth / 2 - cityText.dstR.w / 2;
    cityText.dstR.y = plantingGreenText.dstR.y + plantingGreenText.dstR.h;
    cityText.setText(renderer, oldStandardF, "Miasto", { 0, 0, 0 });
    SDL_FRect downArrowsR;
    downArrowsR.w = 16;
    downArrowsR.h = 16;
    downArrowsR.x = windowWidth / 2 - downArrowsR.w / 2;
    downArrowsR.y = windowHeight - 20;
    Text tutorialText;
    tutorialText.setText(renderer, robotoF, "poradnik", { 0, 0, 0 });
    tutorialText.dstR.w = 50;
    tutorialText.dstR.h = 20;
    tutorialText.dstR.x = downArrowsR.x + downArrowsR.w / 2 - tutorialText.dstR.w / 2;
    tutorialText.dstR.y = downArrowsR.y - tutorialText.dstR.h;
    State state = State::Main;
    Tutorial1 tutorial1;
    tutorial1.firstText.dstR.w = 80;
    tutorial1.firstText.dstR.h = 15;
    tutorial1.firstText.dstR.x = 30;
    tutorial1.firstText.dstR.y = 30;
    tutorial1.firstText.setText(renderer, oldStandardF, u8"Pokażemy Ci", { 0, 0, 0 });
    tutorial1.secondText.dstR.w = 100;
    tutorial1.secondText.dstR.h = 20;
    tutorial1.secondText.dstR.x = 30;
    tutorial1.secondText.dstR.y = tutorial1.firstText.dstR.y + tutorial1.firstText.dstR.h + 10;
    tutorial1.secondText.setText(renderer, oldStandardF, u8"Jak zacząć", { 0, 0, 0 });
    tutorial1.thirdText.dstR.w = 40;
    tutorial1.thirdText.dstR.h = 12;
    tutorial1.thirdText.dstR.x = 30;
    tutorial1.thirdText.dstR.y = tutorial1.secondText.dstR.y + tutorial1.secondText.dstR.h + 20;
    tutorial1.thirdText.setText(renderer, heeboF, u8"Krok 1", { 0, 0, 0 });
    tutorial1.fourthText.dstR.w = 180;
    tutorial1.fourthText.dstR.h = 12;
    tutorial1.fourthText.dstR.x = 30;
    tutorial1.fourthText.dstR.y = tutorial1.thirdText.dstR.y + tutorial1.thirdText.dstR.h + 20;
    tutorial1.fourthText.setText(renderer, robotoF, u8"Wybierz z menu drzewko, które", { 0, 0, 0 });
    tutorial1.fifthText.dstR.w = 80;
    tutorial1.fifthText.dstR.h = 12;
    tutorial1.fifthText.dstR.x = 30;
    tutorial1.fifthText.dstR.y = tutorial1.fourthText.dstR.y + tutorial1.fourthText.dstR.h;
    tutorial1.fifthText.setText(renderer, robotoF, u8"chcesz posadzić", { 0, 0, 0 });
    tutorial1.imageR.w = 200;
    tutorial1.imageR.h = 100;
    tutorial1.imageR.x = windowWidth / 2 - tutorial1.imageR.w / 2;
    tutorial1.imageR.y = windowHeight - tutorial1.imageR.h;
    tutorial1.arrowR.w = 32;
    tutorial1.arrowR.h = 32;
    tutorial1.arrowR.x = windowWidth - tutorial1.arrowR.w;
    tutorial1.arrowR.y = windowHeight / 2 - tutorial1.arrowR.h / 2;
    Tutorial2 tutorial2;
    tutorial2.firstText.dstR.w = 80;
    tutorial2.firstText.dstR.h = 15;
    tutorial2.firstText.dstR.x = 30;
    tutorial2.firstText.dstR.y = 30;
    tutorial2.firstText.setText(renderer, oldStandardF, u8"Pokażemy Ci", { 0, 0, 0 });
    tutorial2.secondText.dstR.w = 100;
    tutorial2.secondText.dstR.h = 20;
    tutorial2.secondText.dstR.x = 30;
    tutorial2.secondText.dstR.y = tutorial2.firstText.dstR.y + tutorial2.firstText.dstR.h + 10;
    tutorial2.secondText.setText(renderer, oldStandardF, u8"Jak zacząć", { 0, 0, 0 });
    tutorial2.thirdText.dstR.w = 40;
    tutorial2.thirdText.dstR.h = 12;
    tutorial2.thirdText.dstR.x = 30;
    tutorial2.thirdText.dstR.y = tutorial2.secondText.dstR.y + tutorial2.secondText.dstR.h + 20;
    tutorial2.thirdText.setText(renderer, heeboF, u8"Krok 2", { 0, 0, 0 });
    tutorial2.fourthText.dstR.w = 180;
    tutorial2.fourthText.dstR.h = 12;
    tutorial2.fourthText.dstR.x = 30;
    tutorial2.fourthText.dstR.y = tutorial2.thirdText.dstR.y + tutorial2.thirdText.dstR.h + 20;
    tutorial2.fourthText.setText(renderer, robotoF, u8"Dowiedz się więcej na temat", { 0, 0, 0 });
    tutorial2.fifthText.dstR.w = 60;
    tutorial2.fifthText.dstR.h = 12;
    tutorial2.fifthText.dstR.x = 30;
    tutorial2.fifthText.dstR.y = tutorial2.fourthText.dstR.y + tutorial2.fourthText.dstR.h + 10;
    tutorial2.fifthText.setText(renderer, robotoF, u8"roślinki.", { 0, 0, 0 });
    tutorial2.imageR.w = 200;
    tutorial2.imageR.h = 100;
    tutorial2.imageR.x = windowWidth / 2 - tutorial2.imageR.w / 2;
    tutorial2.imageR.y = windowHeight - tutorial2.imageR.h - 50;
    tutorial2.arrowR.w = 32;
    tutorial2.arrowR.h = 32;
    tutorial2.arrowR.x = windowWidth - tutorial2.arrowR.w;
    tutorial2.arrowR.y = windowHeight / 2 - tutorial2.arrowR.h / 2;
    Tutorial3 tutorial3;
    tutorial3.firstText.dstR.w = 80;
    tutorial3.firstText.dstR.h = 15;
    tutorial3.firstText.dstR.x = 30;
    tutorial3.firstText.dstR.y = 30;
    tutorial3.firstText.setText(renderer, oldStandardF, u8"Pokażemy Ci", { 0, 0, 0 });
    tutorial3.secondText.dstR.w = 100;
    tutorial3.secondText.dstR.h = 20;
    tutorial3.secondText.dstR.x = 30;
    tutorial3.secondText.dstR.y = tutorial3.firstText.dstR.y + tutorial3.firstText.dstR.h + 10;
    tutorial3.secondText.setText(renderer, oldStandardF, u8"Jak zacząć", { 0, 0, 0 });
    tutorial3.thirdText.dstR.w = 40;
    tutorial3.thirdText.dstR.h = 12;
    tutorial3.thirdText.dstR.x = 30;
    tutorial3.thirdText.dstR.y = tutorial3.secondText.dstR.y + tutorial3.secondText.dstR.h + 20;
    tutorial3.thirdText.setText(renderer, heeboF, u8"Krok 3", { 0, 0, 0 });
    tutorial3.fourthText.dstR.w = 90;
    tutorial3.fourthText.dstR.h = 12;
    tutorial3.fourthText.dstR.x = 30;
    tutorial3.fourthText.dstR.y = tutorial3.thirdText.dstR.y + tutorial3.thirdText.dstR.h + 20;
    tutorial3.fourthText.setText(renderer, robotoF, u8"Wybierz partnera", { 0, 0, 0 });
    tutorial3.fifthText.dstR.w = 180;
    tutorial3.fifthText.dstR.h = 12;
    tutorial3.fifthText.dstR.x = 30;
    tutorial3.fifthText.dstR.y = tutorial3.fourthText.dstR.y + tutorial3.fourthText.dstR.h + 10;
    tutorial3.fifthText.setText(renderer, robotoF, u8"który posadzi drzewo w twoim imieniu.", { 0, 0, 0 });
    tutorial3.imageR.w = 200;
    tutorial3.imageR.h = 100;
    tutorial3.imageR.x = windowWidth / 2 - tutorial3.imageR.w / 2;
    tutorial3.imageR.y = windowHeight - tutorial3.imageR.h - 50;
    tutorial3.arrowR.w = 32;
    tutorial3.arrowR.h = 32;
    tutorial3.arrowR.x = windowWidth - tutorial3.arrowR.w;
    tutorial3.arrowR.y = windowHeight / 2 - tutorial3.arrowR.h / 2;
    Tutorial4 tutorial4;
    tutorial4.firstText.dstR.w = 80;
    tutorial4.firstText.dstR.h = 15;
    tutorial4.firstText.dstR.x = 30;
    tutorial4.firstText.dstR.y = 30;
    tutorial4.firstText.setText(renderer, oldStandardF, u8"Pokażemy Ci", { 0, 0, 0 });
    tutorial4.secondText.dstR.w = 100;
    tutorial4.secondText.dstR.h = 20;
    tutorial4.secondText.dstR.x = 30;
    tutorial4.secondText.dstR.y = tutorial4.firstText.dstR.y + tutorial4.firstText.dstR.h + 10;
    tutorial4.secondText.setText(renderer, oldStandardF, u8"Jak zacząć", { 0, 0, 0 });
    tutorial4.thirdText.dstR.w = 40;
    tutorial4.thirdText.dstR.h = 12;
    tutorial4.thirdText.dstR.x = 30;
    tutorial4.thirdText.dstR.y = tutorial4.secondText.dstR.y + tutorial4.secondText.dstR.h + 20;
    tutorial4.thirdText.setText(renderer, heeboF, u8"Krok 4", { 0, 0, 0 });
    tutorial4.fourthText.dstR.w = 120;
    tutorial4.fourthText.dstR.h = 12;
    tutorial4.fourthText.dstR.x = 30;
    tutorial4.fourthText.dstR.y = tutorial4.thirdText.dstR.y + tutorial4.thirdText.dstR.h + 20;
    tutorial4.fourthText.setText(renderer, robotoF, u8"Wybierz miejsce gdzie chcesz", { 0, 0, 0 });
    tutorial4.fifthText.dstR.w = 100;
    tutorial4.fifthText.dstR.h = 12;
    tutorial4.fifthText.dstR.x = 30;
    tutorial4.fifthText.dstR.y = tutorial4.fourthText.dstR.y + tutorial4.fourthText.dstR.h;
    tutorial4.fifthText.setText(renderer, robotoF, u8"zasadzić swoje drzewo", { 0, 0, 0 });
    tutorial4.imageR.w = 200;
    tutorial4.imageR.h = 100;
    tutorial4.imageR.x = windowWidth / 2 - tutorial4.imageR.w / 2;
    tutorial4.imageR.y = windowHeight - tutorial4.imageR.h - 50;
    tutorial4.plantTreeBtnR.w = 120;
    tutorial4.plantTreeBtnR.h = 30;
    tutorial4.plantTreeBtnR.x = windowWidth / 2 - tutorial4.plantTreeBtnR.w / 2;
    tutorial4.plantTreeBtnR.y = windowHeight - 40;
    PlantTree plantTree;
    plantTree.chooseTreeText.dstR.w = 100;
    plantTree.chooseTreeText.dstR.h = 12;
    plantTree.chooseTreeText.dstR.x = 30;
    plantTree.chooseTreeText.dstR.y = plantTree.chooseTreeText.dstR.y + plantTree.chooseTreeText.dstR.h;
    plantTree.chooseTreeText.setText(renderer, robotoF, u8"Wybierz drzewko", { 0, 0, 0 });
    plantTree.treeRects.push_back(SDL_FRect({ 5, 40, 160, 160 }));
    plantTree.patronsR.w = windowWidth;
    plantTree.patronsR.h = 100;
    plantTree.patronsR.x = 0;
    plantTree.patronsR.y = windowHeight - plantTree.patronsR.h - 40;
    plantTree.choosePlaceBtnR.w = 100;
    plantTree.choosePlaceBtnR.h = 30;
    plantTree.choosePlaceBtnR.x = windowWidth / 2 - plantTree.choosePlaceBtnR.w / 2;
    plantTree.choosePlaceBtnR.y = windowHeight - plantTree.choosePlaceBtnR.h - 2;
    Map map;
    map.mapR.w = windowWidth;
    map.mapR.h = 200;
    map.mapR.x = 0;
    map.mapR.y = bannerR.y + bannerR.h;
    while (running) {
        if (state == State::Main) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT || event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    running = false;
                    // TODO: On mobile remember to use eventWatch function (it doesn't reach this code when terminating)
                }
                if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    SDL_RenderSetScale(renderer, event.window.data1 / (float)windowWidth, event.window.data2 / (float)windowHeight);
                }
                if (event.type == SDL_KEYDOWN) {
                    keys[event.key.keysym.scancode] = true;
                }
                if (event.type == SDL_KEYUP) {
                    keys[event.key.keysym.scancode] = false;
                }
                if (event.type == SDL_MOUSEBUTTONDOWN) {
                    buttons[event.button.button] = true;
                    if (SDL_PointInFRect(&mousePos, &downArrowsR)) {
                        state = State::Tutorial1;
                    }
                }
                if (event.type == SDL_MOUSEBUTTONUP) {
                    buttons[event.button.button] = false;
                }
                if (event.type == SDL_MOUSEMOTION) {
                    float scaleX, scaleY;
                    SDL_RenderGetScale(renderer, &scaleX, &scaleY);
                    mousePos.x = event.motion.x / scaleX;
                    mousePos.y = event.motion.y / scaleY;
                    realMousePos.x = event.motion.x;
                    realMousePos.y = event.motion.y;
                }
            }
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 0);
            SDL_RenderClear(renderer);
            SDL_SetRenderDrawColor(renderer, 254, 250, 241, 0);
            SDL_RenderFillRectF(renderer, &bannerR);
            SDL_RenderCopyF(renderer, logoT, 0, &logoR);
            SDL_RenderCopyF(renderer, hamburgerMenuT, 0, &hamburgerMenuR);
            SDL_RenderCopyF(renderer, handsT, 0, &handsR);
            plantingGreenText.draw(renderer);
            cityText.draw(renderer);
            SDL_RenderCopyF(renderer, downArrowsT, 0, &downArrowsR);
            tutorialText.draw(renderer);
            SDL_RenderPresent(renderer);
        }
        else if (state == State::Tutorial1) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT || event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    running = false;
                    // TODO: On mobile remember to use eventWatch function (it doesn't reach this code when terminating)
                }
                if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    SDL_RenderSetScale(renderer, event.window.data1 / (float)windowWidth, event.window.data2 / (float)windowHeight);
                }
                if (event.type == SDL_KEYDOWN) {
                    keys[event.key.keysym.scancode] = true;
                }
                if (event.type == SDL_KEYUP) {
                    keys[event.key.keysym.scancode] = false;
                }
                if (event.type == SDL_MOUSEBUTTONDOWN) {
                    buttons[event.button.button] = true;
                    if (SDL_PointInFRect(&mousePos, &tutorial1.arrowR)) {
                        state = State::Tutorial2;
                    }
                }
                if (event.type == SDL_MOUSEBUTTONUP) {
                    buttons[event.button.button] = false;
                }
                if (event.type == SDL_MOUSEMOTION) {
                    float scaleX, scaleY;
                    SDL_RenderGetScale(renderer, &scaleX, &scaleY);
                    mousePos.x = event.motion.x / scaleX;
                    mousePos.y = event.motion.y / scaleY;
                    realMousePos.x = event.motion.x;
                    realMousePos.y = event.motion.y;
                }
            }
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 0);
            SDL_RenderClear(renderer);
            tutorial1.firstText.draw(renderer);
            tutorial1.secondText.draw(renderer);
            tutorial1.thirdText.draw(renderer);
            tutorial1.fourthText.draw(renderer);
            tutorial1.fifthText.draw(renderer);
            SDL_RenderCopyF(renderer, tutorial1T, 0, &tutorial1.imageR);
            SDL_RenderCopyF(renderer, rightArrowT, 0, &tutorial1.arrowR);
            SDL_RenderPresent(renderer);
        }
        else if (state == State::Tutorial2) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT || event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    running = false;
                    // TODO: On mobile remember to use eventWatch function (it doesn't reach this code when terminating)
                }
                if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    SDL_RenderSetScale(renderer, event.window.data1 / (float)windowWidth, event.window.data2 / (float)windowHeight);
                }
                if (event.type == SDL_KEYDOWN) {
                    keys[event.key.keysym.scancode] = true;
                }
                if (event.type == SDL_KEYUP) {
                    keys[event.key.keysym.scancode] = false;
                }
                if (event.type == SDL_MOUSEBUTTONDOWN) {
                    buttons[event.button.button] = true;
                    if (SDL_PointInFRect(&mousePos, &tutorial2.arrowR)) {
                        state = State::Tutorial3;
                    }
                }
                if (event.type == SDL_MOUSEBUTTONUP) {
                    buttons[event.button.button] = false;
                }
                if (event.type == SDL_MOUSEMOTION) {
                    float scaleX, scaleY;
                    SDL_RenderGetScale(renderer, &scaleX, &scaleY);
                    mousePos.x = event.motion.x / scaleX;
                    mousePos.y = event.motion.y / scaleY;
                    realMousePos.x = event.motion.x;
                    realMousePos.y = event.motion.y;
                }
            }
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 0);
            SDL_RenderClear(renderer);
            tutorial2.firstText.draw(renderer);
            tutorial2.secondText.draw(renderer);
            tutorial2.thirdText.draw(renderer);
            tutorial2.fourthText.draw(renderer);
            tutorial2.fifthText.draw(renderer);
            SDL_RenderCopyF(renderer, tutorial2T, 0, &tutorial2.imageR);
            SDL_RenderCopyF(renderer, rightArrowT, 0, &tutorial2.arrowR);
            SDL_RenderPresent(renderer);
        }
        else if (state == State::Tutorial3) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT || event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    running = false;
                    // TODO: On mobile remember to use eventWatch function (it doesn't reach this code when terminating)
                }
                if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    SDL_RenderSetScale(renderer, event.window.data1 / (float)windowWidth, event.window.data2 / (float)windowHeight);
                }
                if (event.type == SDL_KEYDOWN) {
                    keys[event.key.keysym.scancode] = true;
                }
                if (event.type == SDL_KEYUP) {
                    keys[event.key.keysym.scancode] = false;
                }
                if (event.type == SDL_MOUSEBUTTONDOWN) {
                    buttons[event.button.button] = true;
                    if (SDL_PointInFRect(&mousePos, &tutorial3.arrowR)) {
                        state = State::Tutorial4;
                    }
                }
                if (event.type == SDL_MOUSEBUTTONUP) {
                    buttons[event.button.button] = false;
                }
                if (event.type == SDL_MOUSEMOTION) {
                    float scaleX, scaleY;
                    SDL_RenderGetScale(renderer, &scaleX, &scaleY);
                    mousePos.x = event.motion.x / scaleX;
                    mousePos.y = event.motion.y / scaleY;
                    realMousePos.x = event.motion.x;
                    realMousePos.y = event.motion.y;
                }
            }
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 0);
            SDL_RenderClear(renderer);
            tutorial3.firstText.draw(renderer);
            tutorial3.secondText.draw(renderer);
            tutorial3.thirdText.draw(renderer);
            tutorial3.fourthText.draw(renderer);
            tutorial3.fifthText.draw(renderer);
            SDL_RenderCopyF(renderer, tutorial3T, 0, &tutorial3.imageR);
            SDL_RenderCopyF(renderer, rightArrowT, 0, &tutorial3.arrowR);
            SDL_RenderPresent(renderer);
        }
        else if (state == State::Tutorial4) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT || event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    running = false;
                    // TODO: On mobile remember to use eventWatch function (it doesn't reach this code when terminating)
                }
                if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    SDL_RenderSetScale(renderer, event.window.data1 / (float)windowWidth, event.window.data2 / (float)windowHeight);
                }
                if (event.type == SDL_KEYDOWN) {
                    keys[event.key.keysym.scancode] = true;
                }
                if (event.type == SDL_KEYUP) {
                    keys[event.key.keysym.scancode] = false;
                }
                if (event.type == SDL_MOUSEBUTTONDOWN) {
                    buttons[event.button.button] = true;
                    if (SDL_PointInFRect(&mousePos, &tutorial4.plantTreeBtnR)) {
                        state = State::PlantTree;
                    }
                }
                if (event.type == SDL_MOUSEBUTTONUP) {
                    buttons[event.button.button] = false;
                }
                if (event.type == SDL_MOUSEMOTION) {
                    float scaleX, scaleY;
                    SDL_RenderGetScale(renderer, &scaleX, &scaleY);
                    mousePos.x = event.motion.x / scaleX;
                    mousePos.y = event.motion.y / scaleY;
                    realMousePos.x = event.motion.x;
                    realMousePos.y = event.motion.y;
                }
            }
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 0);
            SDL_RenderClear(renderer);
            tutorial4.firstText.draw(renderer);
            tutorial4.secondText.draw(renderer);
            tutorial4.thirdText.draw(renderer);
            tutorial4.fourthText.draw(renderer);
            tutorial4.fifthText.draw(renderer);
            SDL_RenderCopyF(renderer, tutorial4T, 0, &tutorial4.imageR);
            SDL_RenderCopyF(renderer, plantTreeButtonT, 0, &tutorial4.plantTreeBtnR);
            SDL_RenderPresent(renderer);
        }
        else if (state == State::PlantTree) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT || event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    running = false;
                    // TODO: On mobile remember to use eventWatch function (it doesn't reach this code when terminating)
                }
                if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    SDL_RenderSetScale(renderer, event.window.data1 / (float)windowWidth, event.window.data2 / (float)windowHeight);
                }
                if (event.type == SDL_KEYDOWN) {
                    keys[event.key.keysym.scancode] = true;
                }
                if (event.type == SDL_KEYUP) {
                    keys[event.key.keysym.scancode] = false;
                }
                if (event.type == SDL_MOUSEBUTTONDOWN) {
                    buttons[event.button.button] = true;
                    if (SDL_PointInFRect(&mousePos, &plantTree.choosePlaceBtnR)) {
                        state = State::Map;
                    }
                }
                if (event.type == SDL_MOUSEBUTTONUP) {
                    buttons[event.button.button] = false;
                }
                if (event.type == SDL_MOUSEMOTION) {
                    float scaleX, scaleY;
                    SDL_RenderGetScale(renderer, &scaleX, &scaleY);
                    mousePos.x = event.motion.x / scaleX;
                    mousePos.y = event.motion.y / scaleY;
                    realMousePos.x = event.motion.x;
                    realMousePos.y = event.motion.y;
                }
            }
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 0);
            SDL_RenderClear(renderer);
            plantTree.chooseTreeText.draw(renderer);
            for (int i = 0; i < plantTree.treeRects.size(); ++i) {
                if (i == 0) {
                    SDL_RenderCopyF(renderer, ordinaryOakT, 0, &plantTree.treeRects[i]);
                }
                else if (i == 1) {
                    SDL_RenderCopyF(renderer, bonsaiT, 0, &plantTree.treeRects[i]);
                }
            }
            SDL_RenderCopyF(renderer, patrons2T, 0, &plantTree.patronsR);
            SDL_RenderCopyF(renderer, choosePlaceT, 0, &plantTree.choosePlaceBtnR);
            SDL_RenderPresent(renderer);
        }
        else if (state == State::Map) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT || event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    running = false;
                    // TODO: On mobile remember to use eventWatch function (it doesn't reach this code when terminating)
                }
                if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    SDL_RenderSetScale(renderer, event.window.data1 / (float)windowWidth, event.window.data2 / (float)windowHeight);
                }
                if (event.type == SDL_KEYDOWN) {
                    keys[event.key.keysym.scancode] = true;
                }
                if (event.type == SDL_KEYUP) {
                    keys[event.key.keysym.scancode] = false;
                }
                if (event.type == SDL_MOUSEBUTTONDOWN) {
                    buttons[event.button.button] = true;
                }
                if (event.type == SDL_MOUSEBUTTONUP) {
                    buttons[event.button.button] = false;
                }
                if (event.type == SDL_MOUSEMOTION) {
                    float scaleX, scaleY;
                    SDL_RenderGetScale(renderer, &scaleX, &scaleY);
                    mousePos.x = event.motion.x / scaleX;
                    mousePos.y = event.motion.y / scaleY;
                    realMousePos.x = event.motion.x;
                    realMousePos.y = event.motion.y;
                }
            }
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 0);
            SDL_RenderClear(renderer);
            SDL_RenderCopyF(renderer, logoT, 0, &logoR);
            SDL_RenderCopyF(renderer, hamburgerMenuT, 0, &hamburgerMenuR);
            SDL_RenderCopyF(renderer, mapT, 0, &map.mapR);
            SDL_RenderPresent(renderer);
        }
    }
    // TODO: On mobile remember to use eventWatch function (it doesn't reach this code when terminating)
    return 0;
}