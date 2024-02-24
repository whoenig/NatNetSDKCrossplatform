cmake_minimum_required(VERSION 3.5)
project(NatNetSDKCrossplatform)

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)

find_package(Boost 1.5 REQUIRED COMPONENTS system thread)

# Enable C++14 and warnings
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# Executables

## PacketClient
add_executable(packetClient
  src/main.cpp
  samples/PacketClient/PacketClient.cpp
)
target_link_libraries(packetClient
  Boost::system
  Boost::thread
)

## SampleClient
include_directories(include)
link_directories(lib/ubuntu)
add_executable(sampleClient
  samples/SampleClient/SampleClient.cpp
)
target_link_libraries(sampleClient
  NatNet
  Threads::Threads
)

## MinimalClient
include_directories(include)
link_directories(lib/ubuntu)
add_executable(minimalClient
  samples/MinimalClient/MinimalClient.cpp
)
target_link_libraries(minimalClient
  NatNet
  Threads::Threads
)
