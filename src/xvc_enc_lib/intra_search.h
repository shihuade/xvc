/******************************************************************************
* Copyright (C) 2017, Divideon. All rights reserved.
* No part of this code may be reproduced in any form
* without the written permission of the copyright holder.
******************************************************************************/

#ifndef XVC_ENC_LIB_INTRA_SEARCH_H_
#define XVC_ENC_LIB_INTRA_SEARCH_H_

#include "xvc_common_lib/intra_prediction.h"
#include "xvc_common_lib/picture_data.h"
#include "xvc_enc_lib/syntax_writer.h"
#include "xvc_enc_lib/cu_writer.h"
#include "xvc_enc_lib/speed_settings.h"
#include "xvc_enc_lib/transform_encoder.h"

namespace xvc {

class IntraSearch : public IntraPrediction {
public:
  IntraSearch(int bitdepth, const PictureData &pic_data,
              const YuvPicture &orig_pic, const SpeedSettings &speed_settings);

  IntraMode SearchIntraLuma(CodingUnit *cu, YuvComponent comp, const QP &qp,
                            const SyntaxWriter &bitstream_writer,
                            TransformEncoder *encoder, YuvPicture *rec_pic);
  IntraChromaMode SearchIntraChroma(CodingUnit *cu, const QP &qp,
                                    const SyntaxWriter &bitstream_writer,
                                    TransformEncoder *encoder,
                                    YuvPicture *rec_pic);

private:
  Distortion CompressIntra(CodingUnit *cu, YuvComponent comp, const QP &qp,
                           TransformEncoder *encoder, YuvPicture *rec_pic);

  const PictureData &pic_data_;
  const YuvPicture &orig_pic_;
  const SpeedSettings &speed_settings_;
  CuWriter cu_writer_;
};

}   // namespace xvc

#endif  // XVC_ENC_LIB_INTRA_SEARCH_H_
