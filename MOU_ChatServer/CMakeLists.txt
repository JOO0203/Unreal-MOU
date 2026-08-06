cmake_minimum_required(VERSION 3.16)
project(MOU_ChatServer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # /utf-8 : 소스에 한글 문자열이 있으므로 필수
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra)
endif()

find_package(Threads REQUIRED)

# 서버와 클라이언트가 같은 프레이밍 코드를 쓴다.
add_library(mou_shared STATIC
    Shared/Framing.cpp
)
target_include_directories(mou_shared PUBLIC Shared)
target_link_libraries(mou_shared PUBLIC Threads::Threads)

if(WIN32)
    target_link_libraries(mou_shared PUBLIC ws2_32)
endif()

add_executable(ChatServer
    Server/ChatServer.cpp
    Server/Session.cpp
)
target_include_directories(ChatServer PRIVATE Server)
target_link_libraries(ChatServer PRIVATE mou_shared)

add_executable(TestClient
    TestClient/TestClient.cpp
)
target_link_libraries(TestClient PRIVATE mou_shared)
