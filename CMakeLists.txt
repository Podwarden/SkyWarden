cmake_minimum_required(VERSION 3.20)
project(skywarden_game C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)

# Pure simulation core (no Playdate deps) — shared by tests now, pdx later.
add_library(skywarden_core STATIC
  src/physics.c
  src/wind.c
  src/balloon.c
  src/camera.c
  src/tower.c
  src/decor.c
  src/enemies.c
  src/audio_util.c
  src/nav.c
  src/tutorial.c
  src/music_map.c)
target_include_directories(skywarden_core PUBLIC src)
target_link_libraries(skywarden_core PUBLIC m)

enable_testing()
add_subdirectory(tests)
