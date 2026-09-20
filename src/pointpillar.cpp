/*
 * SPDX-FileCopyrightText: Copyright (c) 2021 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pointpillar.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

#include <cuda_runtime.h>
#include "NvInfer.h"
#include "NvInferPlugin.h"
#include "NvInferRuntime.h"
#include "NvInferVersion.h"
#include "NvOnnxConfig.h"
#include "NvOnnxParser.h"

#if NV_TENSORRT_MAJOR < 10 || NV_TENSORRT_MAJOR >= 11
#error "blackwell-tensorrt10 requires TensorRT 10.x"
#endif

TRT::~TRT()
{
  delete context_;
  delete engine_;
  checkCudaErrors(cudaEventDestroy(start_));
  checkCudaErrors(cudaEventDestroy(stop_));
}

TRT::TRT(std::string model_file, cudaStream_t stream)
: stream_(stream)
{
  int device = 0;
  cudaDeviceProp device_properties{};
  checkCudaErrors(cudaGetDevice(&device));
  checkCudaErrors(cudaGetDeviceProperties(&device_properties, device));

  const std::string model_cache =
    model_file + ".trt" +
    std::to_string(NV_TENSORRT_MAJOR) + "." +
    std::to_string(NV_TENSORRT_MINOR) + "." +
    std::to_string(NV_TENSORRT_PATCH) + ".sm" +
    std::to_string(device_properties.major) +
    std::to_string(device_properties.minor) + ".cache";

  checkCudaErrors(cudaEventCreate(&start_));
  checkCudaErrors(cudaEventCreate(&stop_));

  initLibNvInferPlugins(&gLogger_, "");

  std::ifstream trt_cache(model_cache, std::ios::binary);
  if (!trt_cache.is_open()) {
    std::cout << "Building TensorRT engine from " << model_file << std::endl;

    auto * builder = nvinfer1::createInferBuilder(gLogger_);
    if (builder == nullptr) {
      std::cerr << "Failed to create TensorRT builder" << std::endl;
      std::exit(EXIT_FAILURE);
    }

    // TensorRT 10 removed implicit batch mode and kEXPLICIT_BATCH.
    auto * network = builder->createNetworkV2(0U);
    auto * parser = nvonnxparser::createParser(*network, gLogger_);

    if (!parser->parseFromFile(
        model_file.c_str(), static_cast<int>(nvinfer1::ILogger::Severity::kWARNING)))
    {
      std::cerr << "Failed to parse ONNX model: " << model_file << std::endl;
      std::exit(EXIT_FAILURE);
    }

    auto * network_config = builder->createBuilderConfig();
    if (builder->platformHasFastFp16()) {
      network_config->setFlag(nvinfer1::BuilderFlag::kFP16);
      std::cout << "TensorRT FP16 enabled" << std::endl;
    }
    network_config->setMemoryPoolLimit(
      nvinfer1::MemoryPoolType::kWORKSPACE, static_cast<std::size_t>(1) << 30);

    auto * serialized_engine = builder->buildSerializedNetwork(*network, *network_config);
    if (serialized_engine == nullptr) {
      std::cerr << "Failed to build serialized TensorRT engine" << std::endl;
      std::exit(EXIT_FAILURE);
    }

    std::ofstream trt_out(model_cache, std::ios::binary);
    if (!trt_out.is_open()) {
      std::cerr << "Cannot write TensorRT cache: " << model_cache << std::endl;
      std::exit(EXIT_FAILURE);
    }
    trt_out.write(
      reinterpret_cast<const char *>(serialized_engine->data()),
      static_cast<std::streamsize>(serialized_engine->size()));
    trt_out.close();

    auto * runtime = nvinfer1::createInferRuntime(gLogger_);
    if (runtime == nullptr) {
      std::cerr << "Failed to create TensorRT runtime" << std::endl;
      std::exit(EXIT_FAILURE);
    }

    engine_ = runtime->deserializeCudaEngine(
      serialized_engine->data(), serialized_engine->size());

    delete runtime;
    delete serialized_engine;
    delete network_config;
    delete parser;
    delete network;
    delete builder;

    if (engine_ == nullptr) {
      std::cerr << "Failed to deserialize newly built TensorRT engine" << std::endl;
      std::exit(EXIT_FAILURE);
    }
  } else {
    std::cout << "Loading TensorRT cache: " << model_cache << std::endl;
    trt_cache.seekg(0, std::ios::end);
    const std::streamsize length = trt_cache.tellg();
    trt_cache.seekg(0, std::ios::beg);

    std::vector<char> data(static_cast<std::size_t>(length));
    if (!trt_cache.read(data.data(), length)) {
      std::cerr << "Failed to read TensorRT cache: " << model_cache << std::endl;
      std::exit(EXIT_FAILURE);
    }

    auto * runtime = nvinfer1::createInferRuntime(gLogger_);
    if (runtime == nullptr) {
      std::cerr << "Failed to create TensorRT runtime" << std::endl;
      std::exit(EXIT_FAILURE);
    }

    engine_ = runtime->deserializeCudaEngine(data.data(), static_cast<std::size_t>(length));
    delete runtime;

    if (engine_ == nullptr) {
      std::cerr << "Failed to deserialize TensorRT cache. Delete " << model_cache
                << " and rebuild it for the current TensorRT/GPU environment." << std::endl;
      std::exit(EXIT_FAILURE);
    }
  }

  context_ = engine_->createExecutionContext();
  if (context_ == nullptr) {
    std::cerr << "Failed to create TensorRT execution context" << std::endl;
    std::exit(EXIT_FAILURE);
  }
}

int TRT::doinfer(void ** buffers)
{
  // The ONNX exported by NVIDIA CUDA-PointPillars uses these stable tensor names:
  // inputs: voxels, voxel_idxs, voxel_num
  // outputs: cls_preds, box_preds, dir_cls_preds
  struct TensorBinding
  {
    const char * name;
    void * address;
  };

  const TensorBinding tensor_bindings[] = {
    {"voxels", buffers[0]},
    {"voxel_idxs", buffers[1]},
    {"voxel_num", buffers[2]},
    {"cls_preds", buffers[3]},
    {"box_preds", buffers[4]},
    {"dir_cls_preds", buffers[5]},
  };

  for (const auto & binding : tensor_bindings) {
    if (engine_->getTensorIOMode(binding.name) == nvinfer1::TensorIOMode::kNONE) {
      std::cerr << "TensorRT engine is missing expected I/O tensor: "
                << binding.name << std::endl;
      return -1;
    }
    if (!context_->setTensorAddress(binding.name, binding.address)) {
      std::cerr << "Failed to bind TensorRT I/O tensor: "
                << binding.name << std::endl;
      return -1;
    }
  }

  return context_->enqueueV3(stream_) ? 0 : -1;
}

PointPillar::PointPillar(std::string model_file, cudaStream_t stream)
: stream_(stream)
{
  checkCudaErrors(cudaEventCreate(&start_));
  checkCudaErrors(cudaEventCreate(&stop_));

  pre_.reset(new PreProcessCuda(stream_));
  trt_.reset(new TRT(model_file, stream_));
  post_.reset(new PostProcessCuda(stream_));

  voxel_features_size_ =
    MAX_VOXELS * params_.max_num_points_per_pillar * 4 * sizeof(float);
  voxel_num_size_ = MAX_VOXELS * sizeof(unsigned int);
  voxel_idxs_size_ = MAX_VOXELS * 4 * sizeof(unsigned int);

  checkCudaErrors(cudaMallocManaged(reinterpret_cast<void **>(&voxel_features_), voxel_features_size_));
  checkCudaErrors(cudaMallocManaged(reinterpret_cast<void **>(&voxel_num_), voxel_num_size_));
  checkCudaErrors(cudaMallocManaged(reinterpret_cast<void **>(&voxel_idxs_), voxel_idxs_size_));

  checkCudaErrors(cudaMemsetAsync(voxel_features_, 0, voxel_features_size_, stream_));
  checkCudaErrors(cudaMemsetAsync(voxel_num_, 0, voxel_num_size_, stream_));
  checkCudaErrors(cudaMemsetAsync(voxel_idxs_, 0, voxel_idxs_size_, stream_));

  features_input_size_ =
    MAX_VOXELS * params_.max_num_points_per_pillar * 10 * sizeof(float);
  checkCudaErrors(cudaMallocManaged(reinterpret_cast<void **>(&features_input_), features_input_size_));
  checkCudaErrors(cudaMallocManaged(reinterpret_cast<void **>(&params_input_), sizeof(unsigned int)));
  checkCudaErrors(cudaMemsetAsync(features_input_, 0, features_input_size_, stream_));
  checkCudaErrors(cudaMemsetAsync(params_input_, 0, sizeof(unsigned int), stream_));

  cls_size_ =
    params_.feature_x_size * params_.feature_y_size * params_.num_classes *
    params_.num_anchors * sizeof(float);
  box_size_ =
    params_.feature_x_size * params_.feature_y_size * params_.num_box_values *
    params_.num_anchors * sizeof(float);
  dir_cls_size_ =
    params_.feature_x_size * params_.feature_y_size * params_.num_dir_bins *
    params_.num_anchors * sizeof(float);

  checkCudaErrors(cudaMallocManaged(reinterpret_cast<void **>(&cls_output_), cls_size_));
  checkCudaErrors(cudaMallocManaged(reinterpret_cast<void **>(&box_output_), box_size_));
  checkCudaErrors(cudaMallocManaged(reinterpret_cast<void **>(&dir_cls_output_), dir_cls_size_));

  bndbox_size_ =
    (params_.feature_x_size * params_.feature_y_size * params_.num_anchors * 9 + 1) *
    sizeof(float);
  checkCudaErrors(cudaMallocManaged(reinterpret_cast<void **>(&bndbox_output_), bndbox_size_));

  res_.reserve(256);
  checkCudaErrors(cudaStreamSynchronize(stream_));
}

PointPillar::~PointPillar()
{
  pre_.reset();
  trt_.reset();
  post_.reset();

  checkCudaErrors(cudaFree(voxel_features_));
  checkCudaErrors(cudaFree(voxel_num_));
  checkCudaErrors(cudaFree(voxel_idxs_));
  checkCudaErrors(cudaFree(features_input_));
  checkCudaErrors(cudaFree(params_input_));
  checkCudaErrors(cudaFree(cls_output_));
  checkCudaErrors(cudaFree(box_output_));
  checkCudaErrors(cudaFree(dir_cls_output_));
  checkCudaErrors(cudaFree(bndbox_output_));
  checkCudaErrors(cudaEventDestroy(start_));
  checkCudaErrors(cudaEventDestroy(stop_));
}

int PointPillar::doinfer(
  void * points_data,
  unsigned int points_size,
  std::vector<Bndbox> & nms_pred,
  const float score_threshold,
  const float nms_iou_threshold,
  const bool class_aware_nms)
{
  nms_pred.clear();
  res_.clear();

#if PERFORMANCE_LOG
  float generate_voxels_time = 0.0f;
  checkCudaErrors(cudaEventRecord(start_, stream_));
#endif

  pre_->generateVoxels(
    static_cast<float *>(points_data), points_size,
    params_input_, voxel_features_, voxel_num_, voxel_idxs_);

#if PERFORMANCE_LOG
  checkCudaErrors(cudaEventRecord(stop_, stream_));
  checkCudaErrors(cudaEventSynchronize(stop_));
  checkCudaErrors(cudaEventElapsedTime(&generate_voxels_time, start_, stop_));
#endif

#if PERFORMANCE_LOG
  float generate_features_time = 0.0f;
  checkCudaErrors(cudaEventRecord(start_, stream_));
#endif

  pre_->generateFeatures(
    voxel_features_, voxel_num_, voxel_idxs_, params_input_, features_input_);

#if PERFORMANCE_LOG
  checkCudaErrors(cudaEventRecord(stop_, stream_));
  checkCudaErrors(cudaEventSynchronize(stop_));
  checkCudaErrors(cudaEventElapsedTime(&generate_features_time, start_, stop_));
#endif

#if PERFORMANCE_LOG
  float inference_time = 0.0f;
  checkCudaErrors(cudaEventRecord(start_, stream_));
#endif

  void * buffers[] = {
    features_input_, voxel_idxs_, params_input_, cls_output_, box_output_, dir_cls_output_};
  if (trt_->doinfer(buffers) != 0) {
    return -1;
  }
  checkCudaErrors(cudaMemsetAsync(params_input_, 0, sizeof(unsigned int), stream_));

#if PERFORMANCE_LOG
  checkCudaErrors(cudaEventRecord(stop_, stream_));
  checkCudaErrors(cudaEventSynchronize(stop_));
  checkCudaErrors(cudaEventElapsedTime(&inference_time, start_, stop_));
#endif

  post_->doPostprocessCuda(
    cls_output_, box_output_, dir_cls_output_, bndbox_output_, score_threshold);
  checkCudaErrors(cudaStreamSynchronize(stream_));

  const int num_obj = static_cast<int>(bndbox_output_[0]);
  const auto * output = bndbox_output_ + 1;
  for (int i = 0; i < num_obj; ++i) {
    res_.emplace_back(
      output[i * 9],
      output[i * 9 + 1], output[i * 9 + 2], output[i * 9 + 3],
      output[i * 9 + 4], output[i * 9 + 5], output[i * 9 + 6],
      static_cast<int>(output[i * 9 + 7]), output[i * 9 + 8]);
  }

  nms_cpu(res_, nms_iou_threshold, nms_pred, class_aware_nms);

#if PERFORMANCE_LOG
  std::cout << "TIME: generateVoxels: " << generate_voxels_time << " ms\n"
            << "TIME: generateFeatures: " << generate_features_time << " ms\n"
            << "TIME: inference: " << inference_time << " ms" << std::endl;
#endif
  return 0;
}
