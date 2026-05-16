cmake_minimum_required(VERSION 3.20)
project(Forj VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt auto-generated code (MOC / UIC / RCC)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ── Locate Qt6 ─────────────────────────────────────────────────────────────────
# If Qt is not on the system PATH, point CMake at your installation, e.g.:
#   cmake -DCMAKE_PREFIX_PATH="C:/Qt/6.7.0/msvc2019_64" ..
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network WebSockets Multimedia)

# ── Sources ────────────────────────────────────────────────────────────────────
set(SOURCES
    resources.qrc
    src/main.cpp
    src/ApiClient.cpp
    src/WebSocketClient.cpp
    src/LoginWindow.cpp
    src/MainWindow.cpp
    src/SettingsDialog.cpp
    src/ServerSettingsDialog.cpp
    src/AvatarCropDialog.cpp
    src/FriendsDialog.cpp
    src/TitleBar.cpp
    src/ForjDialog.cpp
    src/ForjPrompt.cpp
    src/VoiceEngine.cpp
    src/ForjRNoiseReducer.cpp
    src/KycDialog.cpp
)

set(HEADERS
    src/ApiClient.h
    src/WebSocketClient.h
    src/LoginWindow.h
    src/MainWindow.h
    src/SettingsDialog.h
    src/ServerSettingsDialog.h
    src/AvatarCropDialog.h
    src/FriendsDialog.h
    src/TitleBar.h
    src/ForjDialog.h
    src/ForjPrompt.h
    src/VoiceEngine.h
    src/ForjRNoiseReducer.h
    src/KycDialog.h
)

# ── Target ─────────────────────────────────────────────────────────────────────
if(WIN32)
    add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS} app.rc)
else()
    add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::WebSockets
    Qt6::Multimedia
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE winmm)
endif()

# ── Install ────────────────────────────────────────────────────────────────────
install(TARGETS ${PROJECT_NAME}
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# On Windows, deploy Qt DLLs next to the executable
if(WIN32)
    find_program(WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND "${WINDEPLOYQT}"
                    --no-translations
                    "$<TARGET_FILE:${PROJECT_NAME}>"
            COMMENT "Deploying Qt dependencies…"
        )
    endif()
endif()
