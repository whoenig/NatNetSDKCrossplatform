cmake_minimum_required(VERSION 3.5)
project(NatNetSDKCrossplatform)

find_package(Boost 1.65 REQUIRED COMPONENTS system thread)
# Enable C++14 and warnings
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# Executables

## natnettest
add_executable(natnettest
  src/main.cpp
  src/PacketClientHelper.cpp
)
target_link_libraries(natnettest
  ${Boost_LIBRARIES}
)
