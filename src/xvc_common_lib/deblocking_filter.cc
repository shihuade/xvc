/******************************************************************************
* Copyright (C) 2017, Divideon. All rights reserved.
* No part of this code may be reproduced in any form
* without the written permission of the copyright holder.
******************************************************************************/

#include "xvc_common_lib/deblocking_filter.h"

#include <cmath>

#include "xvc_common_lib/common.h"
#include "xvc_common_lib/restrictions.h"
#include "xvc_common_lib/quantize.h"
#include "xvc_common_lib/utils.h"

namespace xvc {

static const std::array<uint8_t, 54> kTcTable = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 9, 10, 11, 13, 14, 16,
  18, 20, 22, 24,
};

static const std::array<uint8_t, constants::kMaxAllowedQp + 1> kBetaTable = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 7, 8, 9, 10, 11, 12, 13,
  14, 15, 16, 17, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46,
  48, 50, 52, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 78, 80, 82, 84,
  86, 88,
};

void DeblockingFilter::DeblockPicture() {
  int num_ctus = pic_data_->GetNumberOfCtu();
  for (int rsaddr = 0; rsaddr < num_ctus; rsaddr++) {
    DeblockCtu(rsaddr, Direction::kVertical);
  }
  for (int rsaddr = 0; rsaddr < num_ctus; rsaddr++) {
    DeblockCtu(rsaddr, Direction::kHorizontal);
  }
}

void DeblockingFilter::DeblockCtu(int rsaddr, Direction dir) {
  const CodingUnit *cu = pic_data_->GetCtu(rsaddr);
  YuvComponent luma = YuvComponent::kY;
  YuvComponent chroma = YuvComponent::kU;
  int pos_x = cu->GetPosX(luma);
  int pos_y = cu->GetPosY(luma);
  int chroma_scale_x = 1 << pic_data_->GetRecPic()->GetSizeShiftX(chroma);
  int chroma_scale_y = 1 << pic_data_->GetRecPic()->GetSizeShiftY(chroma);
  int height_in_8x8 = cu->GetHeight(luma) >> 3;
  int width_in_8x8 = cu->GetWidth(luma) >> 3;

  for (int i = 0; i < height_in_8x8; i++) {
    for (int j = 0; j < width_in_8x8; j++) {
      int x = pos_x + (j << 3);
      int y = pos_y + (i << 3);
      // cu_q is the current coding unit
      const CodingUnit *cu_q = pic_data_->GetCuAt(x, y);
      if (cu_q == nullptr) {
        continue;
      }

      // cu_p is the coding unit to the left/above
      // of the edge that is evaluated (might be the same cu).
      const CodingUnit *cu_p = nullptr;
      if (dir == Direction::kVertical) {
        cu_p = pic_data_->GetCuAt(x - 1, y);
      } else if (dir == Direction::kHorizontal) {
        cu_p = pic_data_->GetCuAt(x, y - 1);
      }
      if (cu_p == nullptr || (cu_p->GetPosX(luma) == cu_q->GetPosX(luma) &&
                              cu_p->GetPosY(luma) == cu_q->GetPosY(luma))) {
        continue;
      }

      // Derive boundary strength.
      int boundary_strength = GetBoundaryStrength(*cu_p, *cu_q);
      if (!boundary_strength) {
        continue;
      }

      int qp = (cu_p->GetQp(luma) + cu_q->GetQp(luma) + 1) >> 1;
      if (Restrictions::Get().disable_deblock_depending_on_qp) {
        qp = 32;
      }
      // TODO(dev): Add check for if a PU (CU) is coded losslessly
      FilterEdgeLuma(x, y, dir, boundary_strength, qp);

      if (boundary_strength == 2 && pic_data_->GetNumComponents() > 1 &&
          !Restrictions::Get().disable_deblock_chroma_filter) {
        int chroma_qp = (cu_p->GetQp(chroma) + cu_q->GetQp(chroma) + 1) >> 1;
        if (Restrictions::Get().disable_deblock_depending_on_qp) {
          qp = 31;
        }
        if (dir == Direction::kVertical && (j % chroma_scale_x) == 0) {
          FilterEdgeChroma(x / chroma_scale_x, y / chroma_scale_y,
                           chroma_scale_x, chroma_scale_y, dir,
                           boundary_strength, chroma_qp);
        } else if (dir == Direction::kHorizontal && (i % chroma_scale_y) == 0) {
          FilterEdgeChroma(x / chroma_scale_x, y / chroma_scale_y,
                           chroma_scale_x, chroma_scale_y, dir,
                           boundary_strength, chroma_qp);
        }
      }
    }
  }
}

int DeblockingFilter::GetBoundaryStrength(const CodingUnit &cu_p,
                                          const CodingUnit &cu_q) {
  static const int one_integer_step = 1 << constants::kMvPrecisionShift;
  int boundary_strength = 0;
  if (Restrictions::Get().disable_deblock_boundary_strength_zero) {
    boundary_strength = 1;
  }
  YuvComponent luma = YuvComponent::kY;

  if (cu_p.IsIntra() || cu_q.IsIntra()) {
    boundary_strength = 2;
  } else if (cu_p.GetCbf(luma) || cu_q.GetCbf(luma)) {
    boundary_strength = 1;
  } else if (pic_data_->GetPredictionType() == PicturePredictionType::kBi) {
    PicNum refP0 = cu_p.GetRefPoc(RefPicList::kL0);
    PicNum refP1 = cu_p.GetRefPoc(RefPicList::kL1);
    PicNum refQ0 = cu_q.GetRefPoc(RefPicList::kL0);
    PicNum refQ1 = cu_q.GetRefPoc(RefPicList::kL1);
    if ((refP0 == refQ0 && refP1 == refQ1) ||
      (refP0 == refQ1 && refP1 == refQ0)) {
      auto &mvP0 = cu_p.GetMv(RefPicList::kL0);
      auto &mvP1 = cu_p.GetMv(RefPicList::kL1);
      auto &mvQ0 = cu_q.GetMv(RefPicList::kL0);
      auto &mvQ1 = cu_q.GetMv(RefPicList::kL1);
      auto cond1 = [&mvP0, &mvP1, &mvQ0, &mvQ1]() {
        return (std::abs(mvP0.x - mvQ0.x) >= one_integer_step) ||
          (std::abs(mvP0.y - mvQ0.y) >= one_integer_step) ||
          (std::abs(mvP1.x - mvQ1.x) >= one_integer_step) ||
          (std::abs(mvP1.y - mvQ1.y) >= one_integer_step);
      };
      auto cond2 = [&mvP0, &mvP1, &mvQ0, &mvQ1]() {
        return (std::abs(mvP0.x - mvQ1.x) >= one_integer_step) ||
          (std::abs(mvP0.y - mvQ1.y) >= one_integer_step) ||
          (std::abs(mvP1.x - mvQ0.x) >= one_integer_step) ||
          (std::abs(mvP1.y - mvQ0.y) >= one_integer_step);
      };
      if (refP0 != refP1) {
        if (refP0 == refQ0) {
          if (cond1()) {
            boundary_strength = 1;
          }
        } else {
          if (cond2()) {
            boundary_strength = 1;
          }
        }
      } else {
        if (cond1() && cond2()) {
          boundary_strength = 1;
        }
      }
    } else {
      boundary_strength = 1;
    }
  } else {
    // For uni-prediction assumes that a POC is only referenced once
    if (cu_p.GetRefIdx(RefPicList::kL0) != cu_q.GetRefIdx(RefPicList::kL0)) {
      boundary_strength = 1;
    } else if (std::abs(cu_p.GetMv(RefPicList::kL0).x -
                        cu_q.GetMv(RefPicList::kL0).x) >= one_integer_step ||
               std::abs(cu_p.GetMv(RefPicList::kL0).y -
                        cu_q.GetMv(RefPicList::kL0).y) >= one_integer_step) {
      boundary_strength = 1;
    }
  }
  if (boundary_strength == 1 &&
      Restrictions::Get().disable_deblock_boundary_strength_one) {
    boundary_strength = 2;
  }
  return boundary_strength;
}

void DeblockingFilter::FilterEdgeLuma(int x, int y, Direction dir,
                                      int boundary_strength, int qp) {
  std::shared_ptr<YuvPicture> src_pic = pic_data_->GetRecPic();
  YuvComponent luma = YuvComponent::kY;
  Sample *src = src_pic->GetSamplePtr(luma, x, y);
  ptrdiff_t src_stride = src_pic->GetStride(luma);
  ptrdiff_t offset;
  ptrdiff_t step_size;

  if (dir == Direction::kVertical) {
    offset = 1;
    step_size = src_stride;
  } else {
    offset = src_stride;
    step_size = 1;
  }

  auto calculate_dp = [&offset](Sample* sample) {
    return std::abs(sample[-offset * 3] - 2 * sample[-offset * 2] +
                    sample[-offset]);
  };
  auto calculate_dq = [&offset](Sample* sample) {
    return std::abs(sample[0] - 2 * sample[offset] + sample[offset * 2]);
  };
  int bitdepth_shift = pic_data_->GetBitdepth() - 8;
  for (int i = 0; i < 2; i++) {
    int index_beta = util::Clip3(qp + beta_offset_, 0,
                                 static_cast<int>(kBetaTable.size()));
    int beta = kBetaTable[index_beta] << bitdepth_shift;
    ptrdiff_t block_offset = i * (step_size << 2);
    int dp0 = calculate_dp(src + block_offset);
    int dq0 = calculate_dq(src + block_offset);
    int dp3 = calculate_dp(src + block_offset + step_size * 3);
    int dq3 = calculate_dq(src + block_offset + step_size * 3);
    int d0 = dp0 + dq0;
    int d3 = dp3 + dq3;
    int d = d0 + d3;

    if (d >= beta &&
        !Restrictions::Get().disable_deblock_initial_sample_decision) {
      continue;
    }

    int index_tc = util::Clip3(qp + tc_offset_ + 2 * (boundary_strength - 1), 0,
                               static_cast<int>(kTcTable.size()));
    int tc = kTcTable[index_tc] << bitdepth_shift;

    // Check if strong filtering should be applied.
    bool strong_filter = (d0 << 1) < (beta >> 2) && (d3 << 1) < (beta >> 2);
    strong_filter &= CheckStrongFilter(src + block_offset, beta, tc, offset);
    strong_filter &=
      CheckStrongFilter(src + block_offset + step_size * 3, beta, tc, offset);

    if (strong_filter && !Restrictions::Get().disable_deblock_strong_filter) {
      FilterLumaStrong(src + block_offset, step_size, offset, 2 * tc);
    } else {
      if (Restrictions::Get().disable_deblock_weak_filter) {
        continue;
      }
      int side_threshold = (beta + (beta >> 1)) >> 3;
      int dp = dp0 + dp3;
      int dq = dq0 + dq3;
      bool filter_p1 = dp < side_threshold;
      bool filter_q1 = dq < side_threshold;
      FilterLumaWeak(src + block_offset, step_size, offset, tc,
                     filter_p1, filter_q1);
    }
  }
}

bool DeblockingFilter::CheckStrongFilter(Sample* src, int beta, int tc,
                                         ptrdiff_t offset) {
  Sample p3 = src[-offset * 4];
  Sample p0 = src[-offset];
  Sample q0 = src[0];
  Sample q3 = src[offset * 3];
  bool test2 = (std::abs(p3 - p0) + std::abs(q0 - q3)) < (beta >> 3);
  bool test3 = std::abs(p0 - q0) < ((tc * 5 + 1) >> 1);
  return test2 && test3;
}

void DeblockingFilter::FilterLumaWeak(Sample* src_ptr, ptrdiff_t step_size,
                                      ptrdiff_t offset, int tc, bool filter_p1,
                                      bool filter_q1) {
  const Sample sample_max = (1 << pic_data_->GetBitdepth()) - 1;
  int32_t threashold = tc * 10;
  int32_t half_tc = tc >> 1;
  Sample* src = src_ptr;

  for (int i = 0; i < 4; i++) {
    Sample p1 = src[-offset * 2];
    Sample p0 = src[-offset];
    Sample q0 = src[0];
    Sample q1 = src[offset];
    int32_t delta = (9 * (q0 - p0) - 3 * (q1 - p1) + 8) >> 4;

    if (std::abs(delta) >= threashold &&
        !Restrictions::Get().disable_deblock_initial_sample_decision) {
      src += step_size;
      continue;
    }

    delta = util::Clip3(delta, -tc, tc);
    src[-offset] = util::ClipBD(p0 + delta, sample_max);
    src[0] = util::ClipBD(q0 - delta, sample_max);

    if (!Restrictions::Get().disable_deblock_two_samples_weak_filter) {
      if (filter_p1) {
        Sample p2 = src[-offset * 3];
        int32_t delta_p1 = util::Clip3(
          ((((p2 + p0 + 1) >> 1) - p1 + delta) >> 1), -half_tc, half_tc);
        src[-offset * 2] = util::ClipBD(p1 + delta_p1, sample_max);
      }
      if (filter_q1) {
        Sample q2 = src[offset * 2];
        int32_t delta_q1 = util::Clip3(
          ((((q2 + q0 + 1) >> 1) - q1 - delta) >> 1), -half_tc, half_tc);
        src[offset] = util::ClipBD(q1 + delta_q1, sample_max);
      }
    }
    src += step_size;
  }
}

void DeblockingFilter::FilterLumaStrong(Sample* src, ptrdiff_t step_size,
                                        ptrdiff_t offset, int tc2) {
  auto clip_sample_3 = [](int value, int min, int max) {
    return static_cast<Sample>(util::Clip3(value, min, max));
  };
  for (int i = 0; i < 4; i++) {
    Sample p3 = src[-offset * 4];
    Sample p2 = src[-offset * 3];
    Sample p1 = src[-offset * 2];
    Sample p0 = src[-offset];
    Sample q0 = src[0];
    Sample q1 = src[offset];
    Sample q2 = src[offset * 2];
    Sample q3 = src[offset * 3];

    // Calculate filtered values.
    int np2 = (2 * p3 + 3 * p2 + p1 + p0 + q0 + 4) >> 3;
    int np1 = (p2 + p1 + p0 + q0 + 2) >> 2;
    int np0 = (p2 + 2 * p1 + 2 * p0 + 2 * q0 + q1 + 4) >> 3;
    int nq0 = (p1 + 2 * p0 + 2 * q0 + 2 * q1 + q2 + 4) >> 3;
    int nq1 = (p0 + q0 + q1 + q2 + 2) >> 2;
    int nq2 = (p0 + q0 + q1 + 3 * q2 + 2 * q3 + 4) >> 3;

    // Clip and write back.
    src[-offset * 3] = p2 + clip_sample_3(np2 - p2, -tc2, tc2);
    src[-offset * 2] = p1 + clip_sample_3(np1 - p1, -tc2, tc2);
    src[-offset] = p0 + clip_sample_3(np0 - p0, -tc2, tc2);
    src[0] = q0 + clip_sample_3(nq0 - q0, -tc2, tc2);
    src[offset] = q1 + clip_sample_3(nq1 - q1, -tc2, tc2);
    src[offset * 2] = q2 + clip_sample_3(nq2 - q2, -tc2, tc2);

    src += step_size;
  }
}

void DeblockingFilter::FilterEdgeChroma(int x, int y, int scale_x, int scale_y,
                                        Direction dir, int boundary_strength,
                                        int qp) {
  std::shared_ptr<YuvPicture> src_pic = pic_data_->GetRecPic();
  for (int i = 1; i < constants::kMaxYuvComponents; i++) {
    YuvComponent comp = YuvComponent(i);
    Sample *src = src_pic->GetSamplePtr(comp, x, y);
    ptrdiff_t src_stride = src_pic->GetStride(comp);
    ptrdiff_t offset;
    ptrdiff_t step_size;

    if (dir == Direction::kVertical) {
      offset = 1;
      step_size = src_stride;
    } else {
      offset = src_stride;
      step_size = 1;
    }

    int bitdepth_shift = pic_data_->GetBitdepth() - 8;
    int index_tc = util::Clip3(qp + tc_offset_ + 2, 0,
                               static_cast<int>(kTcTable.size()));
    int tc = kTcTable[index_tc] << bitdepth_shift;
    FilterChroma(src, step_size, offset, tc);
  }
}

void DeblockingFilter::FilterChroma(Sample* src, ptrdiff_t step_size,
                                    ptrdiff_t offset, int tc) {
  const Sample sample_max = (1 << pic_data_->GetBitdepth()) - 1;
  for (int i = 0; i < 4; i++) {
    Sample p1 = src[-offset * 2];
    Sample p0 = src[-offset];
    Sample q0 = src[0];
    Sample q1 = src[offset];

    int delta = util::Clip3((((q0 - p0) << 2) + p1 - q1 + 4) >> 3, -tc, tc);
    src[-offset] = util::ClipBD(p0 + delta, sample_max);
    src[0] = util::ClipBD(q0 - delta, sample_max);
    src += step_size;
  }
}

}   // namespace xvc
