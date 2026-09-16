cmake_minimum_required(VERSION 3.18)
project(cuda_pointpillars_ros LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 14)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
  # RTX 30-series / Ampere default. Override with
  # -DCMAKE_CUDA_ARCHITECTURES=<arch> when building for another GPU.
  set(CMAKE_CUDA_ARCHITECTURES 86)
endif()

find_package(ament_cmake REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(vision_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(visualization_msgs REQUIRED)
find_package(CUDAToolkit REQUIRED)

find_path(TENSORRT_INCLUDE_DIR NvInfer.h
  HINTS
    /usr/include/x86_64-linux-gnu
    /usr/include
)
find_library(TENSORRT_LIBRARY nvinfer
  HINTS
    /usr/lib/x86_64-linux-gnu
    /usr/lib
)
find_library(TENSORRT_ONNX_PARSER_LIBRARY nvonnxparser
  HINTS
    /usr/lib/x86_64-linux-gnu
    /usr/lib
)
find_library(TENSORRT_PLUGIN_LIBRARY nvinfer_plugin
  HINTS
    /usr/lib/x86_64-linux-gnu
    /usr/lib
)

if(NOT TENSORRT_INCLUDE_DIR OR NOT TENSORRT_LIBRARY OR NOT TENSORRT_ONNX_PARSER_LIBRARY)
  message(FATAL_ERROR "TensorRT headers/libraries were not found")
endif()

add_executable(pointpillars_node
  src/pc_process.cpp
  src/pillarScatter.cpp
  src/pointpillar.cpp
  src/postprocess.cpp
  src/preprocess.cpp
  src/postprocess_kernels.cu
  src/pillarScatterKernels.cu
  src/preprocess_kernels.cu
)

set_target_properties(pointpillars_node PROPERTIES
  CUDA_SEPARABLE_COMPILATION ON
)

target_include_directories(pointpillars_node PRIVATE
  include
  ${TENSORRT_INCLUDE_DIR}
)

target_compile_options(pointpillars_node PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-Wall>
  $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
  $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>
  $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-declarations>
)

ament_target_dependencies(pointpillars_node
  ament_index_cpp
  rclcpp
  sensor_msgs
  vision_msgs
  geometry_msgs
)

target_link_libraries(pointpillars_node
  CUDA::cudart
  ${TENSORRT_LIBRARY}
  ${TENSORRT_ONNX_PARSER_LIBRARY}
)

if(TENSORRT_PLUGIN_LIBRARY)
  target_link_libraries(pointpillars_node ${TENSORRT_PLUGIN_LIBRARY})
endif()

add_executable(pointpillars_visualizer
  src/pointpillars_visualizer.cpp
)

target_compile_options(pointpillars_visualizer PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)

ament_target_dependencies(pointpillars_visualizer
  rclcpp
  vision_msgs
  visualization_msgs
)

install(TARGETS
  pointpillars_node
  pointpillars_visualizer
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY
  launch
  config
  model
  DESTINATION share/${PROJECT_NAME}
)

ament_package()
