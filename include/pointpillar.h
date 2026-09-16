/*
 * SPDX-FileCopyrightText: Copyright (c) 2021 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef POINTPILLAR_H_
#define POINTPILLAR_H_

#include <memory>
#include <string>
#include <vector>

#include "cuda_runtime.h"
#include "NvInfer.h"
#include "NvOnnxConfig.h"
#include "NvOnnxParser.h"
#include "NvInferRuntime.h"

#include "postprocess.h"
#include "preprocess.h"

#define PERFORMANCE_LOG 0

class Logger : public nvinfer1::ILogger
{
public:
  void log(Severity severity, const char * msg) noexcept override
  {
    if (severity == Severity::kERROR || severity == Severity::kINTERNAL_ERROR) {
      std::cerr << "trt_infer: " << msg << std::endl;
    }
  }
};

class TRT
{
private:
  cudaEvent_t start_, stop_;
  Logger gLogger_;
  nvinfer1::IExecutionContext * context_ = nullptr;
  nvinfer1::ICudaEngine * engine_ = nullptr;
  cudaStream_t stream_ = 0;

public:
  TRT(std::string model_file, cudaStream_t stream = 0);
  ~TRT();
  int doinfer(void ** buffers);
};

class PointPillar
{
private:
  Params params_;
  cudaEvent_t start_, stop_;
  cudaStream_t stream_;

  std::shared_ptr<PreProcessCuda> pre_;
  std::shared_ptr<TRT> trt_;
  std::shared_ptr<PostProcessCuda> post_;

  float * voxel_features_ = nullptr;
  unsigned int * voxel_num_ = nullptr;
  unsigned int * voxel_idxs_ = nullptr;
  unsigned int * pillar_num_ = nullptr;

  unsigned int voxel_features_size_ = 0;
  unsigned int voxel_num_size_ = 0;
  unsigned int voxel_idxs_size_ = 0;

  float * features_input_ = nullptr;
  unsigned int * params_input_ = nullptr;
  unsigned int features_input_size_ = 0;

  float * cls_output_ = nullptr;
  float * box_output_ = nullptr;
  float * dir_cls_output_ = nullptr;
  unsigned int cls_size_ = 0;
  unsigned int box_size_ = 0;
  unsigned int dir_cls_size_ = 0;

  float * bndbox_output_ = nullptr;
  unsigned int bndbox_size_ = 0;
  std::vector<Bndbox> res_;

public:
  PointPillar(std::string model_file, cudaStream_t stream = 0);
  ~PointPillar();

  int doinfer(
    void * points,
    unsigned int point_size,
    std::vector<Bndbox> & result,
    float score_threshold,
    float nms_iou_threshold,
    bool class_aware_nms);
};

#endif  // POINTPILLAR_H_
