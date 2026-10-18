cmake_minimum_required(VERSION 3.16)

project(custom_reactor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(reactor_core
    Acceptor.cpp
    Buffer.cpp
    Channel.cpp
    EventLoop.cpp
    EventLoopThread.cpp
    EventLoopThreadPool.cpp
    Packet.cpp
    Tcpconnection.cpp
    TcpServer.cpp
    Timer.cpp
)

target_include_directories(reactor_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(reactor_core PUBLIC Threads::Threads)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(reactor_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(reactor_server main.cpp)
target_link_libraries(reactor_server PRIVATE reactor_core)

add_executable(reactor_client Client.cpp)
target_link_libraries(reactor_client PRIVATE Threads::Threads)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(reactor_server PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(reactor_client PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)

if(BUILD_TESTING)
    add_executable(buffer_packet_test tests/buffer_packet_test.cpp)
    target_link_libraries(buffer_packet_test PRIVATE reactor_core)
    add_test(NAME buffer_packet_unit COMMAND buffer_packet_test)

    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(
        NAME reactor_protocol_integration
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/reactor_protocol_integration.py
                $<TARGET_FILE:reactor_server>
    )
    set_tests_properties(
        reactor_protocol_integration
        PROPERTIES TIMEOUT 20 RUN_SERIAL TRUE
    )
endif()
