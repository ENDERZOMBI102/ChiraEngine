#include "Engine.h"

#include <backends/imgui_impl_sdl.h>
#include <SDL.h>

#include <config/ConEntry.h>
#include <entity/light/LightManager.h>
#include <i18n/TranslationManager.h>
#include <input/InputManager.h>
#include <loader/mesh/OBJMeshLoader.h>
#include <loader/mesh/ChiraMeshLoader.h>
#include <resource/provider/FilesystemResourceProvider.h>
#include <script/AngelScriptVM.h>
#include <ui/debug/ConsolePanel.h>
#include <ui/debug/ResourceUsageTrackerPanel.h>
#include "CommandLine.h"
#include "Platform.h"

#ifdef DEBUG
    #include <render/backend/RenderBackend.h>
#endif
#ifdef CHIRA_USE_DISCORD
    #include <hook/DiscordRPC.h>
#endif
#ifdef CHIRA_USE_STEAMWORKS
    #include <hook/SteamAPI.h>
#endif

using namespace chira;

CHIRA_CREATE_LOG(ENGINE);

[[maybe_unused]]
ConCommand quit{"quit", "Quits the game or application.", [] { // NOLINT(cert-err58-cpp)
    Engine::getDevice()->closeAfterThisFrame(true);
}};

[[maybe_unused]]
ConCommand crash{"crash", "Force-crashes the game or application (for debugging purposes).", [] { // NOLINT(cert-err58-cpp)
    throw std::runtime_error{"Called crash command!"};
}, CON_FLAG_CHEAT};

void Engine::preInit(int argc, const char* const argv[]) {
#ifdef CHIRA_PLATFORM_WINDOWS
    // Enable colored text in Windows console by setting encoding to UTF-8
    // #define CP_UTF8 65001 in Windows.h
    system("chcp 65001 > nul");

    // Force enable DPI awareness because the manifest method didn't work
    SDL_SetHintWithPriority(SDL_HINT_WINDOWS_DPI_SCALING, "0", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2", SDL_HINT_OVERRIDE);
#endif
    CommandLine::init(argc, argv);
    Resource::addResourceProvider(new FilesystemResourceProvider{ENGINE_FILESYSTEM_PATH});
    TranslationManager::addTranslationFile("file://i18n/engine");
    LightManager::setupShaderMacros();
}

void Engine::init() {
    Engine::started = true;

    if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER)) {
        LOG_ENGINE.error("SDL2 failed to initialize: {}", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    Engine::device.reset(new Device{TR("ui.window.title")});

#ifdef DEBUG
    if (!Renderer::setupForDebugging()) {
        LOG_ENGINE.warning("Render backend \"{}\" failed to setup for debugging!", Renderer::getHumanName());
    }
#endif

    Engine::device->displaySplashScreen();

    IMeshLoader::addMeshLoader("obj", new OBJMeshLoader{});
    IMeshLoader::addMeshLoader("cmdl", new ChiraMeshLoader{});

#ifdef CHIRA_USE_STEAMWORKS
    bool steamEnable = ConVarRegistry::hasConVar("steam_enable") && ConVarRegistry::getConVar("steam_enable")->getValue<bool>();
    if (steamEnable && (!SteamAPI::Client::initialized() && !SteamAPI::Client::initSteam()))
        LOG_ENGINE.warning("Steam failed to initialize");
#endif

    // Add console UI panel
    auto consoleID = Engine::device->addPanel(new ConsolePanel{});
    Input::KeyEvent::create(Input::Key::SDLK_BACKQUOTE, Input::KeyEventType::PRESSED, [consoleID] {
        auto console = Engine::device->getPanel(consoleID);
        console->setVisible(!console->isVisible());
    });

    // Add resource usage tracker UI panel
    auto resourceUsageTrackerID = Engine::device->addPanel(new ResourceUsageTrackerPanel{});
    Input::KeyEvent::create(Input::Key::SDLK_F1, Input::KeyEventType::PRESSED, [resourceUsageTrackerID] {
        auto resourceUsageTracker = Engine::device->getPanel(resourceUsageTrackerID);
        resourceUsageTracker->setVisible(!resourceUsageTracker->isVisible());
    });

    // Start script VM
    AngelScriptVM::init();

    // Create default resources
    Events::createEvent("chira::engine::create_default_resources");
    Events::update();
}

void Engine::run() {
    ImGui::SetCurrentContext(Engine::device->imguiContext);
    ImGui::GetIO().Fonts->Build();

    do {
        Engine::lastTime = Engine::currentTime;
        Engine::currentTime = SDL_GetTicks64();

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // todo(input): check this function, if ImGui processed an event we should ignore that event
            ImGui_ImplSDL2_ProcessEvent(&event);

            // todo(input): this is O(n^2) and was written badly because i hope it will be rewritten soon please fix
            switch (event.type) {
                case SDL_QUIT:
                    Engine::device->closeAfterThisFrame();
                    break;
                case SDL_WINDOWEVENT:
                    switch (event.window.event) {
                        case SDL_WINDOWEVENT_SHOWN:
                            Engine::device->iconified = false;
                            break;
                        case SDL_WINDOWEVENT_HIDDEN:
                        case SDL_WINDOWEVENT_MINIMIZED:
                            Engine::device->iconified = true;
                            break;
                        case SDL_WINDOWEVENT_RESTORED:
                            if (auto* win_maximized = ConVarRegistry::getConVar("win_maximized")) {
                                win_maximized->setValue(false, false);
                            }
                            break;
                        case SDL_WINDOWEVENT_MAXIMIZED:
                            if (auto* win_maximized = ConVarRegistry::getConVar("win_maximized")) {
                                win_maximized->setValue(true, false);
                            }
                            break;
                        case SDL_WINDOWEVENT_SIZE_CHANGED: {
                            int w, h;
                            SDL_GetWindowSizeInPixels(Engine::device->window, &w, &h);
                            Engine::device->setSize({w, h}, false);
                            break;
                        }
                        default:
                            // There's quite a few events we don't care about or are already handled by other events
                            break;
                    }
                    break;
                case SDL_KEYDOWN:
                    for (const auto& keyEvent : Input::KeyEvent::getEvents()) {
                        if (keyEvent.getEvent() == event.key.keysym.sym && keyEvent.getEventType() == Input::KeyEventType::PRESSED) {
                            keyEvent();
                        }
                    }
                    break;
                case SDL_KEYUP:
                    for (const auto& keyEvent : Input::KeyEvent::getEvents()) {
                        if (keyEvent.getEvent() == event.key.keysym.sym && keyEvent.getEventType() == Input::KeyEventType::RELEASED) {
                            keyEvent();
                        }
                    }
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    for (const auto& mouseEvent : Input::MouseEvent::getEvents()) {
                        if (static_cast<uint8_t>(mouseEvent.getEvent()) == event.button.button && mouseEvent.getEventType() == Input::MouseEventType::CLICKED) {
                            mouseEvent(event.button.x, event.button.y, event.button.clicks);
                        }
                    }
                    break;
                case SDL_MOUSEBUTTONUP:
                    for (const auto& mouseEvent : Input::MouseEvent::getEvents()) {
                        if (static_cast<uint8_t>(mouseEvent.getEvent()) == event.button.button && mouseEvent.getEventType() == Input::MouseEventType::RELEASED) {
                            mouseEvent(event.button.x, event.button.y, event.button.clicks);
                        }
                    }
                    break;
                case SDL_MOUSEMOTION:
                    for (const auto& mouseMotionEvent : Input::MouseMotionEvent::getEvents()) {
                        if (mouseMotionEvent.getEvent() == Input::MouseMotion::MOVEMENT) {
                            mouseMotionEvent(event.motion.x, event.motion.y, event.motion.xrel, event.motion.yrel);
                        }
                    }
                    break;
                case SDL_MOUSEWHEEL:
                    for (const auto& mouseMotionEvent : Input::MouseMotionEvent::getEvents()) {
                        if (mouseMotionEvent.getEvent() == Input::MouseMotion::SCROLL) {
                            mouseMotionEvent(event.wheel.x, event.wheel.y, event.wheel.x, event.wheel.y);
                        }
                    }
                    break;
                default:
                    // todo(input): handle joystick / game controller inputs!
                    break;
            }
        }

        // Handle repeating events
        // This is a pointer to a static variable in SDL so this is safe
        static const auto* keyStates = SDL_GetKeyboardState(nullptr);
        for (const auto& keyEvent : Input::KeyEvent::getEvents()) {
            if (keyStates[SDL_GetScancodeFromKey(keyEvent.getEvent())] && keyEvent.getEventType() == Input::KeyEventType::REPEATED) {
                keyEvent();
            }
        }

        Engine::device->refresh();

#ifdef CHIRA_USE_DISCORD
        if (DiscordRPC::initialized()) {
            DiscordRPC::updatePresence();
        }
#endif
#ifdef CHIRA_USE_STEAMWORKS
        if (SteamAPI::Client::initialized()) {
            SteamAPI::Client::runCallbacks();
        }
#endif
        Events::update();
    } while (!Engine::device->shouldCloseAfterThisFrame());

    LOG_ENGINE.info("Exiting...");

#ifdef CHIRA_USE_DISCORD
    if (DiscordRPC::initialized()) {
        DiscordRPC::shutdown();
    }
#endif
#ifdef CHIRA_USE_STEAMWORKS
    if (SteamAPI::Client::initialized()) {
        SteamAPI::Client::shutdown();
    }
#endif

    Engine::device.reset();

    Resource::discardAll();

    SDL_Quit();
    exit(EXIT_SUCCESS);
}

bool Engine::isStarted() {
    return Engine::started;
}

uint64_t Engine::getDeltaTicks() {
    return Engine::currentTime - Engine::lastTime;
}
