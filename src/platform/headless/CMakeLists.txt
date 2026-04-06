project(nba-headless LANGUAGES CXX)

add_executable(nba-headless main.cpp)
target_link_libraries(nba-headless PRIVATE nba)
target_compile_features(nba-headless PRIVATE cxx_std_17)
set_target_properties(nba-headless PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/headless"
)
