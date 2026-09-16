/*
 * SPDX-FileCopyrightText: Copyright (c) 2021 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "postprocess.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

const float ThresHold = 1e-8;

inline float cross(const float2 p1, const float2 p2, const float2 p0)
{
  return (p1.x - p0.x) * (p2.y - p0.y) - (p2.x - p0.x) * (p1.y - p0.y);
}

inline int check_box2d(const Bndbox box, const float2 p)
{
  const float margin = 1e-2;
  const float center_x = box.x;
  const float center_y = box.y;
  const float angle_cos = std::cos(-box.rt);
  const float angle_sin = std::sin(-box.rt);
  const float rot_x = (p.x - center_x) * angle_cos + (p.y - center_y) * (-angle_sin);
  const float rot_y = (p.x - center_x) * angle_sin + (p.y - center_y) * angle_cos;
  return std::fabs(rot_x) < box.w / 2 + margin && std::fabs(rot_y) < box.l / 2 + margin;
}

bool intersection(const float2 p1, const float2 p0, const float2 q1, const float2 q0, float2 & ans)
{
  if (!(
      std::min(p0.x, p1.x) <= std::max(q0.x, q1.x) &&
      std::min(q0.x, q1.x) <= std::max(p0.x, p1.x) &&
      std::min(p0.y, p1.y) <= std::max(q0.y, q1.y) &&
      std::min(q0.y, q1.y) <= std::max(p0.y, p1.y)))
  {
    return false;
  }

  const float s1 = cross(q0, p1, p0);
  const float s2 = cross(p1, q1, p0);
  const float s3 = cross(p0, q1, q0);
  const float s4 = cross(q1, p1, q0);
  if (!(s1 * s2 > 0 && s3 * s4 > 0)) {
    return false;
  }

  const float s5 = cross(q1, p1, p0);
  if (std::fabs(s5 - s1) > ThresHold) {
    ans.x = (s5 * q0.x - s1 * q1.x) / (s5 - s1);
    ans.y = (s5 * q0.y - s1 * q1.y) / (s5 - s1);
  } else {
    const float a0 = p0.y - p1.y;
    const float b0 = p1.x - p0.x;
    const float c0 = p0.x * p1.y - p1.x * p0.y;
    const float a1 = q0.y - q1.y;
    const float b1 = q1.x - q0.x;
    const float c1 = q0.x * q1.y - q1.x * q0.y;
    const float d = a0 * b1 - a1 * b0;
    if (std::fabs(d) <= ThresHold) {
      return false;
    }
    ans.x = (b0 * c1 - b1 * c0) / d;
    ans.y = (a1 * c0 - a0 * c1) / d;
  }
  return true;
}

inline void rotate_around_center(
  const float2 & center, const float angle_cos, const float angle_sin, float2 & p)
{
  const float new_x =
    (p.x - center.x) * angle_cos + (p.y - center.y) * (-angle_sin) + center.x;
  const float new_y =
    (p.x - center.x) * angle_sin + (p.y - center.y) * angle_cos + center.y;
  p = float2{new_x, new_y};
}

inline float box_overlap(const Bndbox & box_a, const Bndbox & box_b)
{
  const float a_dx_half = box_a.w / 2;
  const float b_dx_half = box_b.w / 2;
  const float a_dy_half = box_a.l / 2;
  const float b_dy_half = box_b.l / 2;

  const float a_x1 = box_a.x - a_dx_half;
  const float a_y1 = box_a.y - a_dy_half;
  const float a_x2 = box_a.x + a_dx_half;
  const float a_y2 = box_a.y + a_dy_half;
  const float b_x1 = box_b.x - b_dx_half;
  const float b_y1 = box_b.y - b_dy_half;
  const float b_x2 = box_b.x + b_dx_half;
  const float b_y2 = box_b.y + b_dy_half;

  float2 box_a_corners[5] = {
    {a_x1, a_y1}, {a_x2, a_y1}, {a_x2, a_y2}, {a_x1, a_y2}, {a_x1, a_y1}};
  float2 box_b_corners[5] = {
    {b_x1, b_y1}, {b_x2, b_y1}, {b_x2, b_y2}, {b_x1, b_y2}, {b_x1, b_y1}};

  const float2 center_a{box_a.x, box_a.y};
  const float2 center_b{box_b.x, box_b.y};
  const float a_angle_cos = std::cos(box_a.rt);
  const float a_angle_sin = std::sin(box_a.rt);
  const float b_angle_cos = std::cos(box_b.rt);
  const float b_angle_sin = std::sin(box_b.rt);

  for (int k = 0; k < 4; ++k) {
    rotate_around_center(center_a, a_angle_cos, a_angle_sin, box_a_corners[k]);
    rotate_around_center(center_b, b_angle_cos, b_angle_sin, box_b_corners[k]);
  }
  box_a_corners[4] = box_a_corners[0];
  box_b_corners[4] = box_b_corners[0];

  float2 cross_points[16];
  float2 poly_center{0.0f, 0.0f};
  int count = 0;

  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      if (intersection(
          box_a_corners[i + 1], box_a_corners[i],
          box_b_corners[j + 1], box_b_corners[j], cross_points[count]))
      {
        poly_center.x += cross_points[count].x;
        poly_center.y += cross_points[count].y;
        ++count;
      }
    }
  }

  for (int k = 0; k < 4; ++k) {
    if (check_box2d(box_a, box_b_corners[k])) {
      poly_center.x += box_b_corners[k].x;
      poly_center.y += box_b_corners[k].y;
      cross_points[count++] = box_b_corners[k];
    }
    if (check_box2d(box_b, box_a_corners[k])) {
      poly_center.x += box_a_corners[k].x;
      poly_center.y += box_a_corners[k].y;
      cross_points[count++] = box_a_corners[k];
    }
  }

  if (count < 3) {
    return 0.0f;
  }

  poly_center.x /= count;
  poly_center.y /= count;

  std::sort(
    cross_points, cross_points + count,
    [&poly_center](const float2 & lhs, const float2 & rhs) {
      return std::atan2(lhs.y - poly_center.y, lhs.x - poly_center.x) <
             std::atan2(rhs.y - poly_center.y, rhs.x - poly_center.x);
    });

  float area = 0.0f;
  for (int k = 0; k < count; ++k) {
    const int next = (k + 1) % count;
    area += cross_points[k].x * cross_points[next].y -
            cross_points[next].x * cross_points[k].y;
  }
  return std::fabs(area) * 0.5f;
}

int nms_cpu(
  std::vector<Bndbox> bndboxes,
  const float nms_thresh,
  std::vector<Bndbox> & nms_pred,
  const bool class_aware)
{
  std::sort(
    bndboxes.begin(), bndboxes.end(),
    [](const Bndbox & a, const Bndbox & b) {return a.score > b.score;});

  std::vector<int> suppressed(bndboxes.size(), 0);
  for (size_t i = 0; i < bndboxes.size(); ++i) {
    if (suppressed[i]) {
      continue;
    }
    nms_pred.emplace_back(bndboxes[i]);

    for (size_t j = i + 1; j < bndboxes.size(); ++j) {
      if (suppressed[j]) {
        continue;
      }
      if (class_aware && bndboxes[i].id != bndboxes[j].id) {
        continue;
      }

      const float sa = bndboxes[i].w * bndboxes[i].l;
      const float sb = bndboxes[j].w * bndboxes[j].l;
      const float overlap = box_overlap(bndboxes[i], bndboxes[j]);
      const float iou = overlap / std::fmax(sa + sb - overlap, ThresHold);
      if (iou >= nms_thresh) {
        suppressed[j] = 1;
      }
    }
  }
  return 0;
}

PostProcessCuda::PostProcessCuda(cudaStream_t stream)
: stream_(stream)
{
  checkCudaErrors(cudaMalloc(
    reinterpret_cast<void **>(&anchors_),
    params_.num_anchors * params_.len_per_anchor * sizeof(float)));
  checkCudaErrors(cudaMalloc(
    reinterpret_cast<void **>(&anchor_bottom_heights_),
    params_.num_classes * sizeof(float)));
  checkCudaErrors(cudaMalloc(reinterpret_cast<void **>(&object_counter_), sizeof(int)));

  checkCudaErrors(cudaMemcpyAsync(
    anchors_, params_.anchors,
    params_.num_anchors * params_.len_per_anchor * sizeof(float),
    cudaMemcpyDefault, stream_));
  checkCudaErrors(cudaMemcpyAsync(
    anchor_bottom_heights_, params_.anchor_bottom_heights,
    params_.num_classes * sizeof(float), cudaMemcpyDefault, stream_));
  checkCudaErrors(cudaMemsetAsync(object_counter_, 0, sizeof(int), stream_));
}

PostProcessCuda::~PostProcessCuda()
{
  checkCudaErrors(cudaFree(anchors_));
  checkCudaErrors(cudaFree(anchor_bottom_heights_));
  checkCudaErrors(cudaFree(object_counter_));
}

int PostProcessCuda::doPostprocessCuda(
  const float * cls_input,
  float * box_input,
  const float * dir_cls_input,
  float * bndbox_output,
  const float score_threshold)
{
  checkCudaErrors(cudaMemsetAsync(object_counter_, 0, sizeof(int), stream_));
  checkCudaErrors(postprocess_launch(
    cls_input,
    box_input,
    dir_cls_input,
    anchors_,
    anchor_bottom_heights_,
    bndbox_output,
    object_counter_,
    params_.min_x_range,
    params_.max_x_range,
    params_.min_y_range,
    params_.max_y_range,
    params_.feature_x_size,
    params_.feature_y_size,
    params_.num_anchors,
    params_.num_classes,
    params_.num_box_values,
    score_threshold,
    params_.dir_offset,
    stream_));
  return 0;
}
