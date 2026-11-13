cmake_minimum_required(VERSION 3.13)
project(minicv)

# The subset of the OpenCV API the tree uses (see opencv2/core.hpp), for
# platforms that do not build OpenCV. Decodes PNG and JPEG with the system's
# libraries.
find_package(PNG REQUIRED)
find_package(JPEG REQUIRED)

add_library(minicv STATIC
    opencv2/core.hpp
    opencv2/imgcodecs.hpp
    opencv2/imgproc.hpp
    opencv2/opencv.hpp
    minicv.cpp
    minicv_imgcodecs.cpp
    minicv_imgproc.cpp
)
target_include_directories(minicv PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(minicv PRIVATE PNG::PNG JPEG::JPEG)

# Its self-test (run it on the target: it checks against OpenCV's results).
add_executable(minicv_test EXCLUDE_FROM_ALL tests/minicv_test.cpp tests/test_images.h)
target_link_libraries(minicv_test PRIVATE minicv)
