/* This file is part of the Marble Marcher (https://github.com/HackerPoet/MarbleMarcher).
* Copyright(C) 2018 CodeParade
* 
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 2 of the License, or
* (at your option) any later version.
* 
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License
* along with this program.If not, see <http://www.gnu.org/licenses/>.
*/
#include "Scene.h"
#include "Level.h"
#include "Overlays.h"
#include "Res.h"
#include "SelectRes.h"
#include "Scores.h"
#include <SFML/Audio.hpp>
#include <SFML/Graphics.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/OpenGL.hpp>
#include <SFML/Window/Keyboard.hpp>
#include <sys/types.h>
#include <sys/stat.h>
#include <string>
#include <iostream>
#include <fstream>
#include <thread>
#include <mutex>

#ifdef _WIN32
#include <Windows.h>
#define ERROR_MSG(x) MessageBox(nullptr, TEXT(x), TEXT("ERROR"), MB_OK);
#else
#define ERROR_MSG(x) std::cerr << x << std::endl;
#endif

//Constants
static const float mouse_sensitivity = 0.005f;
static const float wheel_sensitivity = 0.2f;
static const float music_vol = 75.0f;
static const float target_fps = 60.0f;
bool isMulti = false; 

//Game modes
enum GameMode {
  MAIN_MENU,
  PLAYING,
  PLAYING_MULTIPLAYER,
  CHOOSE_MULTIPLAYER,
  PAUSED,
  PAUSED_MULTIPLAYER,
  SCREEN_SAVER,
  CONTROLS,
  OPTIONS, 
  LEVELS,
  CREDITS,
  MIDPOINT
};

//Global variables
static sf::Vector2i mouse_pos;
static bool all_keys[sf::Keyboard::KeyCount] = { 0 };
static bool mouse_clicked = false;
static bool show_cheats = false;
static GameMode game_mode = MAIN_MENU;
static int selected_level = 0;  //Track which level was selected for multiplayer choice

float GetVol() {
  if (game_settings.mute) {
    return 0.0f;
  } else if (game_mode == PAUSED || PAUSED_MULTIPLAYER) {
    return music_vol / 4;
  } else {
    return music_vol;
  }
}

void LockMouse(sf::RenderWindow& window) {
  window.setMouseCursorVisible(false);
  const sf::Vector2u size = window.getSize();
  mouse_pos = sf::Vector2i(size.x / 2, size.y / 2);
  sf::Mouse::setPosition(mouse_pos);
}
void UnlockMouse(sf::RenderWindow& window) {
  window.setMouseCursorVisible(true);
}

void PauseGame(sf::RenderWindow& window, Scene& scene) {
  if (game_mode == PLAYING_MULTIPLAYER) {
    game_mode = PAUSED_MULTIPLAYER;
  } else {
    game_mode = PAUSED;
  }
  scene.GetCurMusic().setVolume(GetVol());
  UnlockMouse(window);
  scene.SetExposure(0.5f);
}

int DirExists(const char *path) {
  struct stat info;
  if (stat(path, &info) != 0) {
    return 0;
  } else if (info.st_mode & S_IFDIR) {
    return 1;
  }
  return 0;
}

#if defined(_WIN32)
int WinMain(HINSTANCE hInstance, HINSTANCE, LPTSTR lpCmdLine, int nCmdShow) {
#else
int main(int argc, char *argv[]) {
#endif
  //Make sure shader is supported
  if (!sf::Shader::isAvailable()) {
    ERROR_MSG("Graphics card does not support shaders");
    return 1;
  }
  //Load the vertex shader
  sf::Shader shader;
  if (!shader.loadFromFile(vert_glsl, sf::Shader::Vertex)) {
    ERROR_MSG("Failed to compile vertex shader");
    return 1;
  }
  //Load the fragment shader
  if (!shader.loadFromFile(frag_glsl, sf::Shader::Fragment)) {
    ERROR_MSG("Failed to compile fragment shader");
    return 1;
  }

  //Load the font
  sf::Font font;
  if (!font.loadFromFile(Orbitron_Bold_ttf)) {
    ERROR_MSG("Unable to load font");
    return 1;
  }
  //Load the mono font
  sf::Font font_mono;
  if (!font_mono.loadFromFile(Inconsolata_Bold_ttf)) {
    ERROR_MSG("Unable to load mono font");
    return 1;
  }

  //Load the music
  sf::Music menu_music;
  menu_music.openFromFile(menu_ogg);
  menu_music.setLoop(true);
  sf::Music level_music[num_level_music];
  level_music[0].openFromFile(level1_ogg);
  level_music[0].setLoop(true);
  level_music[1].openFromFile(level2_ogg);
  level_music[1].setLoop(true);
  level_music[2].openFromFile(level3_ogg);
  level_music[2].setLoop(true);
  level_music[3].openFromFile(level4_ogg);
  level_music[3].setLoop(true);
  sf::Music credits_music;
  credits_music.openFromFile(credits_ogg);
  credits_music.setLoop(true);

  //Get the directory for saving and loading high scores
#ifdef _WIN32
  const std::string save_dir = std::string(std::getenv("APPDATA")) + "\\MarbleMarcher";
#else
  const std::string save_dir = std::string(std::getenv("HOME")) + "/.MarbleMarcher";
#endif
  
  if (!DirExists(save_dir.c_str())) {
#if defined(_WIN32)
    bool success = CreateDirectory(save_dir.c_str(), NULL) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
#else
    bool success = mkdir(save_dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == 0;
#endif
    if (!success) {
      ERROR_MSG("Failed to create save directory");
      return 1;
    }
  }
  const std::string save_file = save_dir + "/scores.bin";
  const std::string settings_file = save_dir + "/settings.bin";

  //Load scores if available
  high_scores.Load(save_file);
  game_settings.Load(settings_file);

  //Have user select the resolution
  SelectRes select_res(&font_mono);
  const Resolution* resolution = select_res.Run();
  bool fullscreen = select_res.FullScreen();
  if (resolution == nullptr) {
    return 0;
  }

  //GL settings
  sf::ContextSettings settings;
  settings.majorVersion = 2;
  settings.minorVersion = 0;

  //Create the window
  sf::VideoMode screen_size;
  sf::Uint32 window_style;
  const sf::Vector2i screen_center(resolution->width / 2, resolution->height / 2);
  if (fullscreen) {
    screen_size = sf::VideoMode::getDesktopMode();
    window_style = sf::Style::Fullscreen;
  } else {
    screen_size = sf::VideoMode(resolution->width, resolution->height, 24);
    window_style = sf::Style::Close;
  }
  sf::RenderWindow window(screen_size, "Marble Marcher", window_style, settings);
  window.setVerticalSyncEnabled(true);
  window.setKeyRepeatEnabled(false);
  window.requestFocus();

  //If native resolution is the same, then we don't need a render texture
  if (resolution->width == screen_size.width && resolution->height == screen_size.height) {
    fullscreen = false;
  }

  //Create the render texture if needed
  sf::RenderTexture renderTexture;
  // Main render texture used only when fullscreen to composite final image
  if (fullscreen) {
    renderTexture.create(resolution->width, resolution->height, settings);
    renderTexture.setSmooth(true);
    renderTexture.setActive(true);
    window.setActive(false);
  }

  // Create two offscreen render textures for splitscreen rendering (left/right).
  const unsigned int left_w = resolution->width / 2;
  const unsigned int right_w = resolution->width - left_w;
  const unsigned int view_h = resolution->height;
  sf::RenderTexture renderLeft;
  sf::RenderTexture renderRight;
  renderLeft.create(left_w, view_h, settings);
  renderLeft.setSmooth(true);
  renderRight.create(right_w, view_h, settings);
  renderRight.setSmooth(true);

  //Create the fractal scene
  Scene scene(level_music);
  Scene scene1(level_music);
  Scene scene2(level_music);
  const sf::Glsl::Vec2 window_res((float)resolution->width, (float)resolution->height);
  shader.setUniform("iResolution", window_res);
  scene.Write(shader);

  //Create screen rectangle (full) and view rectangles (per-viewport)
  sf::RectangleShape rect;
  rect.setSize(window_res);
  rect.setPosition(0, 0);

  const sf::Glsl::Vec2 left_res((float)left_w, (float)view_h);
  const sf::Glsl::Vec2 right_res((float)right_w, (float)view_h);

  // Rect that fills a viewport-sized render texture (origin at 0,0)
  sf::RectangleShape rect_view_left;
  rect_view_left.setSize(sf::Vector2f((float)left_w, (float)view_h));
  rect_view_left.setPosition(0, 0);

  sf::RectangleShape rect_view_right;
  rect_view_right.setSize(sf::Vector2f((float)right_w, (float)view_h));
  rect_view_right.setPosition(0, 0);

  //Create the menus
  Overlays overlays(&font, &font_mono);
  overlays.SetScale(float(screen_size.width) / 1280.0f);
  menu_music.setVolume(GetVol());
  menu_music.play();

  //Main loop
  sf::Clock clock;
  float smooth_fps = target_fps;
  float lag_ms = 0.0f;
  while (window.isOpen()) {
    sf::Event event;
    float mouse_wheel = 0.0f;
    while (window.pollEvent(event)) {
      if (event.type == sf::Event::Closed) {
        window.close();
        break;
      } else if (event.type == sf::Event::LostFocus) {
        if (game_mode == PLAYING) {
          PauseGame(window, scene);
        }
      } else if (event.type == sf::Event::KeyPressed) {
        const sf::Keyboard::Key keycode = event.key.code;
        if (event.key.code < 0 || event.key.code >= sf::Keyboard::KeyCount) { continue; }
        if (game_mode == CREDITS) {
          game_mode = MAIN_MENU;
          UnlockMouse(window);
          scene.SetMode(Scene::INTRO);
          scene.SetExposure(1.0f);
          credits_music.stop();
          menu_music.setVolume(GetVol());
          menu_music.play();
        } else if (game_mode == MIDPOINT) {
          game_mode = PLAYING;
          scene.SetExposure(1.0f);
          credits_music.stop();
          scene.StartNextLevel();
        } else if (keycode == sf::Keyboard::Escape) {
          if (game_mode == MAIN_MENU) {
            window.close();
            break;
          } else if (game_mode == CONTROLS || game_mode == LEVELS || game_mode == OPTIONS) {
            game_mode = MAIN_MENU;
            scene.SetExposure(1.0f);
          } else if (game_mode == CHOOSE_MULTIPLAYER) {
            game_mode = LEVELS;
            scene.SetExposure(0.5f);
          } else if (game_mode == SCREEN_SAVER) {
            game_mode = MAIN_MENU;
            scene.SetMode(Scene::INTRO);
          } else if (game_mode == PAUSED) {
            game_mode = PLAYING;
            scene.GetCurMusic().setVolume(GetVol());
            scene.SetExposure(1.0f);
            LockMouse(window);
          }else if (game_mode == PAUSED_MULTIPLAYER){
            game_mode = PLAYING_MULTIPLAYER;
            scene.GetCurMusic().setVolume(GetVol());
            scene.SetExposure(1.0f);
            LockMouse(window);
          } else if (game_mode == PLAYING) {
            PauseGame(window, scene);
          } else if (game_mode == PLAYING_MULTIPLAYER){
            PauseGame(window, scene);
          }
        } else if (keycode == sf::Keyboard::R) {
          if (game_mode == PLAYING) {
            scene.ResetLevel();
          }
        } else if (keycode == sf::Keyboard::F1) {
          if (game_mode == PLAYING && high_scores.HasCompleted(num_levels - 1)) {
            show_cheats = !show_cheats;
            scene.EnableCheats();         // changed Enbale to Enable   
          }
        } else if (keycode == sf::Keyboard::C) {
          scene.Cheat_ColorChange();
        } else if (keycode == sf::Keyboard::F) {
          scene.Cheat_FreeCamera();
        } else if (keycode == sf::Keyboard::G) {
          scene.Cheat_Gravity();
        } else if (keycode == sf::Keyboard::H) {
          scene.Cheat_HyperSpeed();
        } else if (keycode == sf::Keyboard::I) {
          scene.Cheat_IgnoreGoal();
        } else if (keycode == sf::Keyboard::M) {
          scene.Cheat_Motion();
        } else if (keycode == sf::Keyboard::P) {
          scene.Cheat_Planet();
        } else if (keycode == sf::Keyboard::J){
          scene.Cheat_Jump(); 
        } else if (keycode == sf::Keyboard::Z) {
          if (scene.GetParamMod() == -1) {
            scene.Cheat_Zoom();
          } else {
            scene.Cheat_Param(-1);
          }
        } if (keycode >= sf::Keyboard::Num0 && keycode <= sf::Keyboard::Num9) {
          scene.Cheat_Param(int(keycode) - int(sf::Keyboard::Num1));
        }
        all_keys[keycode] = true;
      } else if (event.type == sf::Event::KeyReleased) {
        const sf::Keyboard::Key keycode = event.key.code;
        if (event.key.code < 0 || event.key.code >= sf::Keyboard::KeyCount) { continue; }
        all_keys[keycode] = false;
      } else if (event.type == sf::Event::MouseButtonPressed) {
        if (event.mouseButton.button == sf::Mouse::Left) {
          mouse_pos = sf::Vector2i(event.mouseButton.x, event.mouseButton.y);
          mouse_clicked = true;
          if (game_mode == MAIN_MENU) {
            const Overlays::Texts selected = overlays.GetOption(Overlays::PLAY, Overlays::EXIT);
            if (selected == Overlays::PLAY) {
              game_mode = PLAYING;
              menu_music.stop();
              scene.StartNewGame();
              scene.GetCurMusic().setVolume(GetVol());
              scene.GetCurMusic().play();
              LockMouse(window);
            } else if (selected == Overlays::MULTIPLAYER) {
              game_mode = PLAYING_MULTIPLAYER;
              menu_music.stop();
              scene1.StartNewGame();
              scene2.StartNewGame();
              
              // Offset marble positions so they spawn on each side
              const Eigen::Vector3f base_pos = scene1.GetMarble();
              const float offset = 8.0f;  // Distance from center
              scene1.SetMarble(base_pos.x() - offset, base_pos.y(), base_pos.z(), 1.0f);
              scene2.SetMarble(base_pos.x() + offset, base_pos.y(), base_pos.z(), 1.0f);
              scene1.GetCurMusic().setVolume(GetVol());
              scene1.GetCurMusic().play();
              LockMouse(window);

              // Snap cameras to new marble positions so each view centers correctly
              scene1.SnapCamera();
              scene1.UpdateCameraOnly(0.0f, 0.0f, 0.0f);
              scene2.SnapCamera();
              scene2.UpdateCameraOnly(0.0f, 0.0f, 0.0f);

              scene1.GetCurMusic().setVolume(GetVol());
              scene1.GetCurMusic().play();
              LockMouse(window);
            } else if (selected == Overlays::CONTROLS) {
              game_mode = CONTROLS;
            } 
              else if (selected == Overlays::OPTIONS){
              game_mode = OPTIONS;
            } else if (selected == Overlays::LEVELS) {
              game_mode = LEVELS;
              overlays.GetLevelPage() = 0;
              scene.SetExposure(0.5f);
            } else if (selected == Overlays::SCREEN_SAVER) {
              game_mode = SCREEN_SAVER;
              scene.SetMode(Scene::SCREEN_SAVER);
            } else if (selected == Overlays::EXIT) {
              window.close();
              break;
            }
          } else if (game_mode == CONTROLS) {
            const Overlays::Texts selected = overlays.GetOption(Overlays::BACK, Overlays::BACK);
            if (selected == Overlays::BACK) {
              game_mode = MAIN_MENU;
            }
          } else if (game_mode == OPTIONS) {
            const Overlays::Texts selected = overlays.GetOption(Overlays::CONTINUE, Overlays::BACK);
            if (selected == Overlays::BACK) {
              game_mode = MAIN_MENU;
            } else if (selected == Overlays::MOUSE) {
              game_settings.mouse_sensitivity = (game_settings.mouse_sensitivity + 1) % 3;
            } else if (selected == Overlays::MUSIC) {
              game_settings.mute = !game_settings.mute;
              if (!game_settings.mute){
                  menu_music.play();
              } else {(menu_music.stop()); }
                for (int i = 0; i < num_level_music; ++i) {
                  level_music[i].setVolume(GetVol());
                }
            } else if (selected == Overlays::FULLSCREEN){
              game_settings.fullscreen = !game_settings.fullscreen;
              if (game_settings.fullscreen){
                 screen_size = sf::VideoMode::getDesktopMode();
                  window_style = sf::Style::Fullscreen;
            
                  sf::RenderWindow window(screen_size, "Marble Marcher", window_style, settings);
                  window.display(); 
              }
            }
          } else if (game_mode == LEVELS) {
            const Overlays::Texts selected = overlays.GetOption(Overlays::L0, Overlays::BACK2);
            if (selected == Overlays::BACK2) {
              game_mode = MAIN_MENU;
              scene.SetExposure(1.0f);
            } else if (selected == Overlays::PREV) {
              overlays.GetLevelPage() -= 1;
            } else if (selected == Overlays::NEXT) {
              overlays.GetLevelPage() += 1;
            } else if (selected >= Overlays::L0 && selected <= Overlays::L14) {
              const int level = selected - Overlays::L0 + overlays.GetLevelPage() * Overlays::LEVELS_PER_PAGE;
              if (high_scores.HasUnlocked(level)) {
                selected_level = level;
                game_mode = CHOOSE_MULTIPLAYER;
                scene.SetExposure(0.5f);
              }
            }
          } else if (game_mode == SCREEN_SAVER) {
            scene.SetMode(Scene::INTRO);
            game_mode = MAIN_MENU;
          } else if (game_mode == CHOOSE_MULTIPLAYER) {
            const Overlays::Texts selected = overlays.GetOption(Overlays::SINGLE_PLAYER, Overlays::MULTIPLAYER_CHOICE);
            if (selected == Overlays::SINGLE_PLAYER) {
              game_mode = PLAYING;
              menu_music.stop();
              scene.SetExposure(1.0f);
              scene.StartSingle(selected_level);
              scene.GetCurMusic().setVolume(GetVol());
              scene.GetCurMusic().play();
              LockMouse(window);
            } else if (selected == Overlays::MULTIPLAYER_CHOICE) {
              game_mode = PLAYING_MULTIPLAYER;
              menu_music.stop();
              scene1.StartNewGame();
              scene2.StartNewGame();
              
              // Set both scenes to the selected level
              scene1.SetLevel(selected_level);
              scene2.SetLevel(selected_level);
              
              // Offset marble positions so they spawn on each side
              const Eigen::Vector3f base_pos = scene1.GetMarble();
              const float offset = 8.0f;
              scene1.SetMarble(base_pos.x() - offset, base_pos.y(), base_pos.z(), 1.0f);
              scene2.SetMarble(base_pos.x() + offset, base_pos.y(), base_pos.z(), 1.0f);
              
              // Snap cameras to new marble positions
              scene1.SnapCamera();
              scene1.UpdateCameraOnly(0.0f, 0.0f, 0.0f);
              scene2.SnapCamera();
              scene2.UpdateCameraOnly(0.0f, 0.0f, 0.0f);
              
              scene1.GetCurMusic().setVolume(GetVol());
              scene1.GetCurMusic().play();
              LockMouse(window);
            }
          } else if (game_mode == PAUSED ) {
            const Overlays::Texts selected = overlays.GetOption(Overlays::CONTINUE, Overlays::MOUSE);
            if (selected == Overlays::CONTINUE) {
              game_mode = PLAYING;
              scene.GetCurMusic().setVolume(GetVol());
              scene.SetExposure(1.0f);
              LockMouse(window);
            } else if (selected == Overlays::RESTART) {
              game_mode = PLAYING;
              scene.ResetLevel();
              scene.GetCurMusic().setVolume(GetVol());
              scene.SetExposure(1.0f);
              LockMouse(window);
            } else if (selected == Overlays::QUIT) {
              if (scene.IsSinglePlay()) {
                game_mode = LEVELS;
              } else {
                game_mode = MAIN_MENU;
                scene.SetExposure(1.0f);
              }
              scene.SetMode(Scene::INTRO);
              scene.StopAllMusic();
              menu_music.setVolume(GetVol());
              menu_music.play();
            } else if (selected == Overlays::MUSIC) {
              game_settings.mute = !game_settings.mute;
              for (int i = 0; i < num_level_music; ++i) {
                level_music[i].setVolume(GetVol());
              }
            } else if (selected == Overlays::MOUSE) {
              game_settings.mouse_sensitivity = (game_settings.mouse_sensitivity + 1) % 3;
            }
          } else if (game_mode == PAUSED_MULTIPLAYER) {
            const Overlays::Texts selected = overlays.GetOption(Overlays::CONTINUE, Overlays::MOUSE);
            if (selected == Overlays::CONTINUE) {
              game_mode = PLAYING_MULTIPLAYER;
              scene1.GetCurMusic().setVolume(GetVol());
              scene1.SetExposure(1.0f);
              LockMouse(window);
            } else if (selected == Overlays::RESTART) {
              game_mode = PLAYING_MULTIPLAYER;
              scene1.ResetLevel();
              scene2.ResetLevel();
              scene1.GetCurMusic().setVolume(GetVol());
              scene1.SetExposure(1.0f);
              LockMouse(window);
            } else if (selected == Overlays::QUIT) {
              game_mode = MAIN_MENU;
              scene1.SetExposure(1.0f);
              scene1.SetMode(Scene::INTRO);
              scene1.StopAllMusic();
              menu_music.setVolume(GetVol());
              menu_music.play();
            } else if (selected == Overlays::MUSIC) {
              game_settings.mute = !game_settings.mute;
              for (int i = 0; i < num_level_music; ++i) {
                level_music[i].setVolume(GetVol());
              }
            } else if (selected == Overlays::MOUSE) {
              game_settings.mouse_sensitivity = (game_settings.mouse_sensitivity + 1) % 3;
            }
          }
        } else if (event.mouseButton.button == sf::Mouse::Right) {
          if (game_mode == PLAYING) {
            scene.ResetLevel();
          }
        }
      } else if (event.type == sf::Event::MouseButtonReleased) {
        if (event.mouseButton.button == sf::Mouse::Left) {
          mouse_pos = sf::Vector2i(event.mouseButton.x, event.mouseButton.y);
          mouse_clicked = false;
        }
      } else if (event.type == sf::Event::MouseMoved) {
        mouse_pos = sf::Vector2i(event.mouseMove.x, event.mouseMove.y);
      } else if (event.type == sf::Event::MouseWheelScrolled) {
        mouse_wheel += event.mouseWheelScroll.delta;
      }
    }

    //Check if the game was beat
    if (scene.GetMode() == Scene::FINAL && game_mode != CREDITS) {
      game_mode = CREDITS;
      scene.StopAllMusic();
      scene.SetExposure(0.5f);
      credits_music.play();
    } else if (scene.GetMode() == Scene::MIDPOINT && game_mode != MIDPOINT) {
      game_mode = MIDPOINT;
      scene.StopAllMusic();
      scene.SetExposure(0.5f);
      credits_music.play();
    }

    //Check if either player won in multiplayer mode
    if (game_mode == PLAYING_MULTIPLAYER) {
      bool p1_at_goal = (scene1.GetMode() == Scene::FINAL || scene1.GetMode() == Scene::GOAL);
      bool p2_at_goal = (scene2.GetMode() == Scene::FINAL || scene2.GetMode() == Scene::GOAL);
      
      if (p1_at_goal || p2_at_goal) {
        // One player finished, advance to next level
        scene1.StartNextLevel();
        scene2.StartNextLevel();

        // Offset marble positions so they spawn on each side and snap cameras
        const Eigen::Vector3f base_pos = scene1.GetMarble();
        const float offset = 8.0f;  // Distance from center
        scene1.SetMarble(base_pos.x() - offset, base_pos.y(), base_pos.z(), 1.0f);
        scene2.SetMarble(base_pos.x() + offset, base_pos.y(), base_pos.z(), 1.0f);
        scene1.SnapCamera();
        scene1.UpdateCameraOnly(0.0f, 0.0f, 0.0f);
        scene2.SnapCamera();
        scene2.UpdateCameraOnly(0.0f, 0.0f, 0.0f);
      }
    }

    //Main game update
    if (game_mode == MAIN_MENU) {
      scene.UpdateCamera();
      overlays.UpdateMenu((float)mouse_pos.x, (float)mouse_pos.y);
    } else if (game_mode == CONTROLS) {
      scene.UpdateCamera();
      overlays.UpdateControls((float)mouse_pos.x, (float)mouse_pos.y);
    } else if (game_mode == OPTIONS) {
      scene.UpdateCamera();
      overlays.UpdateOptions((float)mouse_pos.x, (float)mouse_pos.y); 
    } else if (game_mode == LEVELS) {
      scene.UpdateCamera();
      overlays.UpdateLevels((float)mouse_pos.x, (float)mouse_pos.y);
    } else if (game_mode == CHOOSE_MULTIPLAYER) {
      scene.UpdateCamera();
      overlays.UpdateChooseMultiplayer((float)mouse_pos.x, (float)mouse_pos.y);
    } else if (game_mode == SCREEN_SAVER) {
      scene.UpdateCamera();
    } else if (game_mode == PLAYING || game_mode == CREDITS || game_mode == MIDPOINT) {
      //Collect keyboard input
      const float force_lr =
        (all_keys[sf::Keyboard::Left] || all_keys[sf::Keyboard::A] ? -1.0f : 0.0f) +
        (all_keys[sf::Keyboard::Right] || all_keys[sf::Keyboard::D] ? 1.0f : 0.0f);
      const float force_ud =
        (all_keys[sf::Keyboard::Down] || all_keys[sf::Keyboard::S] ? -1.0f : 0.0f) +
        (all_keys[sf::Keyboard::Up] || all_keys[sf::Keyboard::W] ? 1.0f : 0.0f);

      const float force_jump = 
        (all_keys[sf::Keyboard::Space] ? 1.0f : 0.0f);

      //Collect mouse input
      const sf::Vector2i mouse_delta = mouse_pos - screen_center;
      sf::Mouse::setPosition(screen_center, window);
      float ms = mouse_sensitivity;
      if (game_settings.mouse_sensitivity == 1) {
        ms *= 0.5f;
      } else if (game_settings.mouse_sensitivity == 2) {
        ms *= 0.25f;
      }
      const float cam_lr = float(-mouse_delta.x) * ms;
      const float cam_ud = float(-mouse_delta.y) * ms;
      const float cam_z = mouse_wheel * wheel_sensitivity;

      //Apply forces to marble and camera
      scene.UpdateMarble(force_lr, force_ud, force_jump);
      scene.UpdateCamera(cam_lr, cam_ud, cam_z, mouse_clicked);
    } else if (game_mode == PLAYING_MULTIPLAYER) {
      //Collect keyboard input for Player 1 (WASD + Mouse)
      const float p1_force_lr =
        (all_keys[sf::Keyboard::A] ? -1.0f : 0.0f) +
        (all_keys[sf::Keyboard::D] ? 1.0f : 0.0f);
      const float p1_force_ud =
        (all_keys[sf::Keyboard::S] ? -1.0f : 0.0f) +
        (all_keys[sf::Keyboard::W] ? 1.0f : 0.0f);

      const float p1_force_jump = 
        (all_keys[sf::Keyboard::Space] ? 1.0f : 0.0f);
      //Collect keyboard input for Player 2 (Arrow Keys)
      const float p2_force_lr =
        (all_keys[sf::Keyboard::Left] ? -1.0f : 0.0f) +
        (all_keys[sf::Keyboard::Right] ? 1.0f : 0.0f);
      const float p2_force_ud =
        (all_keys[sf::Keyboard::Down] ? -1.0f : 0.0f) +
        (all_keys[sf::Keyboard::Up] ? 1.0f : 0.0f);


      const float p2_force_jump = 
        (all_keys[sf::Keyboard::Backspace] ? 1.0f : 0.0f);

      //Collect mouse input for Player 1 (center relative to left viewport)
      const sf::Vector2i left_center((int)left_w / 2, (int)view_h / 2);
      const sf::Vector2i mouse_delta = mouse_pos - left_center;
      sf::Mouse::setPosition(left_center, window);
      float ms = mouse_sensitivity;
      if (game_settings.mouse_sensitivity == 1) {
        ms *= 0.5f;
      } else if (game_settings.mouse_sensitivity == 2) {
        ms *= 0.25f;
      }
      const float cam_lr = float(-mouse_delta.x) * ms;
      const float cam_ud = float(-mouse_delta.y) * ms;
      const float cam_z = mouse_wheel * wheel_sensitivity;

      //Apply forces to both marbles
      scene1.UpdateMarble(p1_force_lr, p1_force_ud, p1_force_jump);
      scene2.UpdateMarble(p2_force_lr, p2_force_ud, p2_force_jump);
      
      //Player 1 camera follows mouse, Player 2 has static camera
      //scene1.UpdateCamera(cam_lr, cam_ud, cam_z, mouse_clicked);
      scene1.UpdateCamera(0, 0, 0, mouse_clicked);
      scene2.UpdateCamera(0, 0, 0, mouse_clicked);
    } else if (game_mode == PAUSED) {
      overlays.UpdatePaused((float)mouse_pos.x, (float)mouse_pos.y);
    } else if (game_mode == PAUSED_MULTIPLAYER){
      overlays.UpdatePausedMultiplayer((float)mouse_pos.x, (float)mouse_pos.y);
    }


    bool skip_frame = false;
    if (lag_ms >= 1000.0f / target_fps) {
      //If there is too much lag, just do another frame of physics and skip the draw
      lag_ms -= 1000.0f / target_fps;
      skip_frame = true;
    } else {
      //Update the shader values based on game mode
      if (game_mode == PLAYING_MULTIPLAYER) {
        // per-viewport iResolution is set when rendering each view below
        scene1.Write(shader);
      } else {
        shader.setUniform("iResolution", window_res);
        scene.Write(shader);
      }

      //Setup full-screen shader
      sf::RenderStates states = sf::RenderStates::Default;
      states.shader = &shader;

      if (game_mode == PLAYING_MULTIPLAYER) {
        //Draw splitscreen for multiplayer by rendering each scene into its
        //own offscreen texture (so fragment shader coordinates are local)

        //Left viewport
        shader.setUniform("iResolution", left_res);
        scene1.Write(shader);
        renderLeft.setActive(true);
        renderLeft.clear();
        renderLeft.draw(rect_view_left, states);
        renderLeft.display();

        //Right viewport
        shader.setUniform("iResolution", right_res);
        scene2.Write(shader);
        renderRight.setActive(true);
        renderRight.clear();
        renderRight.draw(rect_view_right, states);
        renderRight.display();

        //Composite into main output
        if (fullscreen) {
          // Draw left and right into the main renderTexture then scale to window
          renderTexture.setActive(true);
          renderTexture.clear();
          sf::Sprite sprL(renderLeft.getTexture());
          sprL.setPosition(0, 0);
          renderTexture.draw(sprL);
          sf::Sprite sprR(renderRight.getTexture());
          sprR.setPosition((float)left_w, 0);
          renderTexture.draw(sprR);
          renderTexture.display();

          sf::Sprite finalS(renderTexture.getTexture());
          finalS.setScale(float(screen_size.width) / float(resolution->width),
                          float(screen_size.height) / float(resolution->height));
          window.draw(finalS);
        } else {
          // Windowed: draw the two textures directly to the window side-by-side
          sf::Sprite sprL(renderLeft.getTexture());
          sprL.setPosition(0, 0);
          window.draw(sprL);
          sf::Sprite sprR(renderRight.getTexture());
          sprR.setPosition((float)left_w, 0);
          window.draw(sprR);
        }
      } else {
        //Draw single player normally
        if (fullscreen) {
          //Draw to the render texture
          renderTexture.draw(rect, states);
          renderTexture.display();

          //Draw render texture to main window
          sf::Sprite sprite(renderTexture.getTexture());
          sprite.setScale(float(screen_size.width) / float(resolution->width),
                          float(screen_size.height) / float(resolution->height));
          window.draw(sprite);
        } else {
          //Draw directly to the main window
          window.draw(rect, states);
        }
      }
    }

    //Draw text overlays to the window
    if (game_mode == MAIN_MENU) {
      overlays.DrawMenu(window);
    } else if (game_mode == CONTROLS) {
      overlays.DrawControls(window);
    } else if (game_mode == LEVELS) {
      overlays.DrawLevels(window);
    } else if (game_mode == CHOOSE_MULTIPLAYER) {
      overlays.DrawChooseMultiplayer(window);
    } else if (game_mode == OPTIONS){
        overlays.DrawOptions(window); 
    } else if (game_mode == PLAYING) {
      if (scene.GetMode() == Scene::ORBIT && scene.GetMarble().x() < 998.0f) {
        overlays.DrawLevelDesc(window, scene.GetLevel());
      } else if (scene.GetMode() == Scene::MARBLE && !scene.IsFreeCamera()) {
        overlays.DrawArrow(window, scene.GetGoalDirection());
      }
      if (!scene.HasCheats() || scene.GetCountdownTime() < 4 * 60) {
        overlays.DrawTimer(window, scene.GetCountdownTime(), scene.IsHighScore());
      }
      if (!scene.HasCheats() && scene.IsFullRun() && !scene.IsFreeCamera()) {
        overlays.DrawSumTime(window, scene.GetSumTime());
      }
      if (scene.HasCheats() && !scene.IsFreeCamera()) {
        overlays.DrawCheatsEnabled(window);
      }
      if (show_cheats) {
        overlays.DrawCheats(window);
      }
    } else if (game_mode == PAUSED) {
      overlays.DrawPaused(window);
      if (scene.HasCheats()) {
        overlays.DrawCheatsEnabled(window);
      }
    } else if (game_mode == PAUSED_MULTIPLAYER) { 
      overlays.DrawPausedMultiplayer(window);
      if (scene.HasCheats()) {
        overlays.DrawCheatsEnabled(window);
      }
    } 
    
    else if (game_mode == CREDITS) {
      overlays.DrawCredits(window, scene.IsFullRun(), scene.GetSumTime());
    } else if (game_mode == MIDPOINT) {
      overlays.DrawMidPoint(window, scene.IsFullRun(), scene.GetSumTime());
    }
    if (!scene.IsFreeCamera()) {
      overlays.DrawFPS(window, int(smooth_fps + 0.5f));
    }

    if (!skip_frame) {
      //Finally display to the screen
      window.display();

      //If V-Sync is running higher than desired fps, slow down!
      const float s = clock.restart().asSeconds();
      if (s > 0.0f) {
        smooth_fps = smooth_fps*0.9f + std::min(1.0f / s, target_fps)*0.1f;
      }
      const float time_diff_ms = 1000.0f * (1.0f / target_fps - s);
      if (time_diff_ms > 0) {
        sf::sleep(sf::seconds(time_diff_ms / 1000.0f));
        lag_ms = std::max(lag_ms - time_diff_ms, 0.0f);
      } else if (time_diff_ms < 0) {
        lag_ms += std::max(-time_diff_ms, 0.0f);
      }
    }
  }

  //Stop all music
  menu_music.stop();
  scene.StopAllMusic();
  credits_music.stop();
  high_scores.Save(save_file);
  game_settings.Save(settings_file);

#ifdef _DEBUG
  system("pause");
#endif
  return 0;
}
