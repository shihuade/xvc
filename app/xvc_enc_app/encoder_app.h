/******************************************************************************
* Copyright (C) 2017, Divideon.
*
* Redistribution and use in source and binary form, with or without
* modifications is permitted only under the terms and conditions set forward
* in the xvc License Agreement. For commercial redistribution and use, you are
* required to send a signed copy of the xvc License Agreement to Divideon.
*
* Redistribution and use in source and binary form is permitted free of charge
* for non-commercial purposes. See definition of non-commercial in the xvc
* License Agreement.
*
* All redistribution of source code must retain this copyright notice
* unmodified.
*
* The xvc License Agreement is available at https://xvc.io/license/.
******************************************************************************/

#ifndef XVC_ENC_APP_ENCODER_APP_H_
#define XVC_ENC_APP_ENCODER_APP_H_

#include <stddef.h>

#include <chrono>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "xvc_enc_lib/xvcenc.h"

namespace xvc_app {

class EncoderApp {
public:
  EncoderApp();
  ~EncoderApp();
  void ReadArguments(int argc, const char *argv[]);
  void CheckParameters();
  void PrintEncoderSettings();
  void MainEncoderLoop();
  void PrintStatistics();

private:
  xvc_enc_return_code ConfigureApiParams(xvc_encoder_parameters *params);
  std::pair<uint64_t, int> EncodeOnePass(xvc_encoder_parameters *params,
                                         bool last = false);
  void StartPictureDetermination(xvc_encoder_parameters *out_params);
  void MultiPass(xvc_encoder_parameters *out_params);
  void ResetStreams();
  bool ReadNextPicture(std::vector<uint8_t> *picture_bytes);
  void PrintUsage();
  void PrintNalInfo(xvc_enc_nal_unit nal_unit);

  std::istream *input_stream_ = nullptr;
  bool input_seekable_ = true;
  std::ifstream file_input_stream_;
  std::ofstream file_output_stream_;
  std::ofstream rec_stream_;
  std::streamoff start_skip_;
  std::streamoff picture_skip_;
  std::streamsize input_file_size_;

  int picture_index_ = 0;
  size_t total_bytes_ = 0;
  size_t max_segment_bytes_ = 0;
  int max_segment_pics_ = 0;

  // command line arguments
  struct {
    std::string input_filename;
    std::string output_filename;
    std::string rec_file;
    int width = 0;
    int height = 0;
    xvc_enc_chroma_format chroma_format = XVC_ENC_CHROMA_FORMAT_UNDEFINED;
    xvc_enc_color_matrix color_matrix = XVC_ENC_COLOR_MATRIX_UNDEFINED;
    int input_bitdepth = 0;
    int internal_bitdepth = 0;
    double framerate = 0;
    int skip_pictures = 0;
    int temporal_subsample = -1;
    int max_num_pictures = -1;
    int sub_gop_length = -1;
    int max_keypic_distance = -1;
    int closed_gop = -1;
    int num_ref_pics = -1;
    int restricted_mode = -1;
    int checksum_mode = -1;
    int chroma_qp_offset_table = -1;
    int chroma_qp_offset_u = std::numeric_limits<int>::min();
    int chroma_qp_offset_v = std::numeric_limits<int>::min();
    int deblock = -1;
    int beta_offset = std::numeric_limits<int>::min();
    int tc_offset = std::numeric_limits<int>::min();
    int qp = -1;
    int flat_lambda = -1;
    int multipass_rd = 0;
    int speed_mode = -1;
    int tune_mode = -1;
    int simd_mask = -1;
    std::string explicit_encoder_settings;
    int verbose = 0;
  } cli_;

  const xvc_encoder_api *xvc_api_ = nullptr;
  xvc_encoder_parameters *params_ = nullptr;
  xvc_encoder *encoder_ = nullptr;
  xvc_enc_pic_buffer rec_pic_buffer_ = { 0, 0 };
  std::vector<uint8_t> picture_bytes_;

  std::chrono::time_point<std::chrono::steady_clock> start_;
  std::chrono::time_point<std::chrono::steady_clock> end_;
};

class LambdaCurve {
public:
  LambdaCurve(const std::pair<uint64_t, int> &p0, int qp0,
              const std::pair<uint64_t, int> &p1, int qp1);
  LambdaCurve(const LambdaCurve &curve,
              const std::pair<uint64_t, int> &p, int qp);
  bool IsPointBetter(const std::pair<uint64_t, int> &p);
  double GetQpAtDistortion(uint64_t distortion);

private:
  double dist_scale_;
  double dist_offset_;
  double qp_scale_;
  double qp_offset_;
};

}  // namespace xvc_app

#endif  // XVC_ENC_APP_ENCODER_APP_H_
