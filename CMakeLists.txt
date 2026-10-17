cmake_minimum_required(VERSION 3.20)
project(mini-ocr VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Sql Concurrent Network)
find_package(OpenCV REQUIRED COMPONENTS core imgproc dnn imgcodecs)
if(TARGET opencv_geometry)
    list(APPEND OpenCV_LIBS opencv_geometry)
endif()

add_executable(mini-ocr
    src/main.cpp
    src/ocr_engine.cpp
    src/ocr_backend.cpp
    src/cloud_engine.cpp
    src/batch_window.cpp
    src/image_view.cpp
    src/history.cpp
    src/result_window.cpp
    src/settings.cpp
)

target_include_directories(mini-ocr PRIVATE ${OpenCV_INCLUDE_DIRS})
if(MSVC)
    target_compile_options(mini-ocr PRIVATE /utf-8)
endif()
target_compile_definitions(mini-ocr PRIVATE
    MINI_OCR_DEFAULT_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/models"
)
if(WIN32)
    set_target_properties(mini-ocr PROPERTIES WIN32_EXECUTABLE TRUE)
    target_sources(mini-ocr PRIVATE
        src/platform_win.cpp
        src/capture_win.cpp
        src/hotkey_win.cpp
        resources/mini-ocr.rc
    )
    target_include_directories(mini-ocr PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/resources)
    target_link_libraries(mini-ocr PRIVATE user32)
    if(TARGET Qt6::EntryPoint)
        target_link_libraries(mini-ocr PRIVATE Qt6::EntryPoint)
    endif()
else()
    target_sources(mini-ocr PRIVATE
        src/platform_linux.cpp
        src/capture.cpp
        src/hotkey_linux.cpp
    )
endif()

target_link_libraries(mini-ocr PRIVATE
    Qt6::Widgets Qt6::Sql Qt6::Concurrent Qt6::Network
    ${OpenCV_LIBS}
)

if(WIN32)
    message(STATUS "Tesseract backend disabled on Windows (pending vcpkg/tessdata integration)")
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(TESSERACT QUIET IMPORTED_TARGET tesseract)
    endif()
endif()
if(TESSERACT_FOUND)
    target_sources(mini-ocr PRIVATE src/tesseract_engine.cpp)
    target_link_libraries(mini-ocr PRIVATE PkgConfig::TESSERACT)
    target_compile_definitions(mini-ocr PRIVATE MINI_OCR_HAVE_TESSERACT=1)
    message(STATUS "Tesseract backend enabled")
else()
    message(STATUS "Tesseract backend disabled (libtesseract not found)")
endif()

include(GNUInstallDirs)
install(TARGETS mini-ocr RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY models/ DESTINATION ${CMAKE_INSTALL_DATADIR}/mini-ocr/models)
if(UNIX AND NOT APPLE)
    install(FILES resources/mini-ocr.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES resources/mini-ocr.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()
