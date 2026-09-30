cmake_minimum_required(VERSION 3.8)
project(hikrobot_mvs_camera)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)

set(MVS_ROOT "/opt/MVS" CACHE PATH "Hikrobot MVS SDK root")
find_path(MVS_INCLUDE_DIR MvCameraControl.h HINTS "${MVS_ROOT}/include" REQUIRED)
find_library(MVS_LIBRARY NAMES MvCameraControl HINTS "${MVS_ROOT}/lib/64" REQUIRED)
if(NOT EXISTS "${MVS_INCLUDE_DIR}/MvCameraControl.h" OR NOT MVS_LIBRARY)
  message(FATAL_ERROR "MVS SDK C headers and 64-bit library are required; set -DMVS_ROOT=<SDK path>")
endif()

add_library(${PROJECT_NAME}_core SHARED src/camera_node.cpp src/mvs_camera.cpp)
target_include_directories(${PROJECT_NAME}_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
  PRIVATE "${MVS_INCLUDE_DIR}")
ament_target_dependencies(${PROJECT_NAME}_core rclcpp sensor_msgs std_msgs)
target_link_libraries(${PROJECT_NAME}_core "${MVS_LIBRARY}")
set_target_properties(${PROJECT_NAME}_core PROPERTIES BUILD_RPATH "${MVS_ROOT}/lib/64" INSTALL_RPATH "${MVS_ROOT}/lib/64")

add_executable(camera_node src/main.cpp)
target_link_libraries(camera_node ${PROJECT_NAME}_core)
ament_target_dependencies(camera_node rclcpp)

add_executable(camera_probe src/camera_probe.cpp)
target_include_directories(camera_probe PRIVATE "${MVS_INCLUDE_DIR}")
target_link_libraries(camera_probe "${MVS_LIBRARY}")
set_target_properties(camera_probe PROPERTIES BUILD_RPATH "${MVS_ROOT}/lib/64" INSTALL_RPATH "${MVS_ROOT}/lib/64")

install(TARGETS ${PROJECT_NAME}_core camera_node camera_probe
  ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY launch DESTINATION share/${PROJECT_NAME})
install(DIRECTORY config DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_camera test/test_camera.cpp)
  target_link_libraries(test_camera ${PROJECT_NAME}_core)
  ament_target_dependencies(test_camera rclcpp sensor_msgs)
endif()

ament_package()
