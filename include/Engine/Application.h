#ifndef ENGINE_APPLICATION_H
#define ENGINE_APPLICATION_H

#define PUBLIC
#define PRIVATE
#define PROTECTED
#define STATIC
#define VIRTUAL
#define EXPOSED


#include <Engine/Includes/Standard.h>
#include <Engine/InputManager.h>
#include <Engine/Audio/AudioManager.h>
#include <Engine/Scene.h>
#include <Engine/Math/Math.h>
#include <Engine/TextFormats/INI.h>

class Application {
public:
    static INI*        Settings;
    static float       FPS;
    static bool        Running;
    static bool        GameStart;
    static SDL_Window* Window;
    static char        WindowTitle[256];
    static int         WindowWidth;
    static int         WindowHeight;
    static Platforms   Platform;
    static int         UpdatesPerFrame;
    static bool        Stepper;
    static bool        Step;

    static void Init(int argc, char* args[]);
    static void GetPerformanceSnapshot();
    static void UpdateWindowTitle();
    static void PollEvents();
    static void RunFrame(void* p);
    static void DelayFrame();
    static void Run(int argc, char* args[]);
    static void Cleanup();
    static void LoadGameConfig();
    static void LoadSettings();
    static int  HandleAppEvents(void* data, SDL_Event* event);
};

#endif /* ENGINE_APPLICATION_H */
