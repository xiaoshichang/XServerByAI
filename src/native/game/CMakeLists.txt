add_executable(game
    game_main.cpp
)

target_link_libraries(game PRIVATE core net ipc host)