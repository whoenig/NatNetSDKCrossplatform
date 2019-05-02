cmake_minimum_required(VERSION 3.5)

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
)
target_link_libraries(natnettest
  ${Boost_LIBRARIES}
)


## natnettest
add_executable(mcr
  src/multicast_receiver.cpp
  src/PacketClientHelper.cpp
)
target_link_libraries(mcr
  ${Boost_LIBRARIES}
)
set_property(TARGET mcr PROPERTY C_STANDARD 11)
