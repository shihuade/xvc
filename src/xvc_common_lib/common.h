/******************************************************************************
* Copyright (C) 2017, Divideon. All rights reserved.
* No part of this code may be reproduced in any form
* without the written permission of the copyright holder.
******************************************************************************/

#ifndef XVC_COMMON_LIB_COMMON_H_
#define XVC_COMMON_LIB_COMMON_H_

#include <stdint.h>
#include <cstddef>

#ifndef XVC_HIGH_BITDEPTH
#define XVC_HIGH_BITDEPTH 1
#endif

#define HM_STRICT 1

namespace xvc {

#if !XVC_HIGH_BITDEPTH
typedef uint8_t Sample;
typedef int16_t Coeff;
typedef int16_t Residual;
#else
typedef uint16_t Sample;
typedef int16_t Coeff;
typedef int16_t Residual;
#endif
typedef uint64_t Cost;
typedef uint64_t Distortion;
typedef uint32_t Bits;
typedef uint64_t PicNum;
typedef uint8_t SegmentNum;

enum class ChromaFormat : uint8_t {
  kMonochrome = 0,
  k420 = 1,
  k422 = 2,
  k444 = 3,
  kUndefinedChromaFormat = 255,
};

enum YuvComponent {
  kY = 0,
  kU = 1,
  kV = 2,
};

namespace constants {

// xvc version
const int kXvcMajorVersion = 1;
const int kXvcMinorVersion = 0;

// Picture
const int kMaxYuvComponents = 3;

// CU limits
const int kCtuSize = 64;
const int kMaxCuDepth = 3;
const int kMaxBlockSize = kCtuSize;
const int kMinBlockSize = (kCtuSize >> kMaxCuDepth);
const int kQuadSplit = 4;

// Transform
const int kMaxTransformSize = 32;
static_assert(kMinBlockSize >= 8, "Minimum CU size is 8");

// Prediction
const int kNumIntraMPM = 3;
const int kNumIntraChromaModes = 5;
const int kNumInterMvPredictors = 2;
const int kNumInterMergeCandidates = 5;
const int kMvPrecisionShift = 2;

// Quant
const int kMaxTrDynamicRange = 15;
const int kQuantShift = 14;
const int kIQuantShift = 20;
const int kMinAllowedQp = -64;
const int kMaxAllowedQp = 63;

// Residual coding
const int kMaxNumC1Flags = 8;
const int kSubblockShift = 2;
const int kCoeffRemainBinReduction = 3;

// Deblocking
const int kDeblockOffsetBits = 6;

// Picture buffer management
// TODO(dev): Consider allowing the encoder to signal a smaller value.
const int kMaxNumLongTermPics = 2;
// Must be smaller than or equal to kMaxNumLongTermPics.
const int kNumPicsInRefPicLists = 2;

// High-level syntax
const int kTimeScale = 90000;
const int kMaxTid = 8;
const int kFrameRateBitDepth = 24;
const int kMaxSubGopLength = 64;

// Min and Max
const int16_t kInt16Max = INT16_MAX;
const int16_t kInt16Min = INT16_MIN;

}   // namespace constants

}   // namespace xvc

#endif  // XVC_COMMON_LIB_COMMON_H_
