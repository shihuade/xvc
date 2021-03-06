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

#include "xvc_dec_app/y4m_writer.h"

#include <cassert>
#include <sstream>

namespace xvc_app {

void Y4mWriter::WriteHeader(const xvc_dec_pic_stats &stats,
                            std::ostream *output) {
  if (!global_header_written_) {
    WriteGlobalHeader(stats, output);
    global_header_written_ = true;
  }
  (*output) << "FRAME\n";
}

void Y4mWriter::WriteGlobalHeader(const xvc_dec_pic_stats &stats,
                                  std::ostream * output) {
  int fps_num, fps_denom;
  if (static_cast<int>(stats.framerate) == stats.framerate) {
    fps_num = static_cast<int>(stats.framerate);
    fps_denom = 1;
  } else {
    fps_num = static_cast<int>(1000 * stats.framerate);
    fps_denom = 1000;
  }
  const char *chroma_format = nullptr;
  if (stats.chroma_format == XVC_DEC_CHROMA_FORMAT_420) {
    chroma_format = "420";
  } else if (stats.chroma_format == XVC_DEC_CHROMA_FORMAT_444) {
    chroma_format = "444";
  } else if (stats.chroma_format == XVC_DEC_CHROMA_FORMAT_422) {
    chroma_format = "422";
  } else if (stats.chroma_format == XVC_DEC_CHROMA_FORMAT_MONOCHROME) {
    chroma_format = "mono";
  } else {
    assert(0);
  }
  (*output)
    << "YUV4MPEG2 "
    << "W" << stats.width << " H" << stats.height << " "
    << "F" << fps_num << ":" << fps_denom << " "
    << "Ip";
  if (chroma_format) {
    (*output) << " C" << chroma_format;
    if (stats.bitdepth > 8) {
      (*output) << "p" << stats.bitdepth;
    }
    (*output) << " ";
  }
  (*output) << "\n";
}

}   // namespace xvc_app
