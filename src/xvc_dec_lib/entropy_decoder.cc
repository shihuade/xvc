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

#include "xvc_dec_lib/entropy_decoder.h"

#include "xvc_common_lib/cabac.h"

namespace xvc {

EntropyDecoder::EntropyDecoder(BitReader *bit_reader)
  : bit_reader_(bit_reader) {
  range_ = 510;
  bits_needed_ = -24;
  value_ = 0;
}

uint32_t EntropyDecoder::DecodeBin(ContextModel *ctx) {
  uint32_t ctxmps = ctx->GetMps();
  uint32_t ctxstate = ctx->GetState();
  uint32_t qrange = (range_ >> 6) & 3;
  uint32_t lps = Cabac::RangeTable(ctxstate, qrange);

  range_ -= lps;
  uint32_t scaled_range = range_ << 7;

  uint32_t binval;
  int num_bits;
  if (value_ < scaled_range) {
    binval = ctxmps;
    ctx->UpdateMPS();
    num_bits = (scaled_range < (256 << 7)) ? 1 : 0;
  } else {
    binval = 1 - ctxmps;
    value_ -= scaled_range;
    range_ = lps;
    ctx->UpdateLPS();
    num_bits = Cabac::RenormTable(lps >> 3);
  }

  value_ <<= num_bits;
  range_ <<= num_bits;
  bits_needed_ += num_bits;

  if (bits_needed_ >= 0) {
    value_ |= bit_reader_->ReadByte() << bits_needed_;
    bits_needed_ -= 8;
  }
  return binval;
}

uint32_t EntropyDecoder::DecodeBypass() {
  value_ += value_;

  if (++bits_needed_ >= 0) {
    bits_needed_ = -8;
    value_ += bit_reader_->ReadByte();
  }

  uint32_t binval = 0;
  uint32_t scaled_range = range_ << 7;
  if (value_ >= scaled_range) {
    binval = 1;
    value_ -= scaled_range;
  }
  return binval;
}

uint32_t EntropyDecoder::DecodeBypassBins(int num_bins) {
  uint32_t bins = 0;
  while (num_bins > 8) {
    value_ = (value_ << 8) + (bit_reader_->ReadByte() << (8 + bits_needed_));
    uint32_t scaled_range = range_ << 15;
    for (int i = 0; i < 8; i++) {
      bins += bins;
      scaled_range >>= 1;
      if (value_ >= scaled_range) {
        bins++;
        value_ -= scaled_range;
      }
    }
    num_bins -= 8;
  }
  bits_needed_ += num_bins;
  value_ <<= num_bins;

  if (bits_needed_ >= 0) {
    value_ += bit_reader_->ReadByte() << bits_needed_;
    bits_needed_ -= 8;
  }

  uint32_t scaled_range = range_ << (num_bins + 7);
  for (int i = 0; i < num_bins; i++) {
    bins += bins;
    scaled_range >>= 1;
    if (value_ >= scaled_range) {
      bins++;
      value_ -= scaled_range;
    }
  }
  return bins;
}

uint32_t EntropyDecoder::DecodeBinTrm() {
  range_ -= 2;
  uint32_t scaled_range = range_ << 7;
  if (value_ >= scaled_range) {
    bit_reader_->Rewind(-bits_needed_);
    return 1;
  }
  if (scaled_range < (256 << 7)) {
    range_ = scaled_range >> 6;
    value_ <<= 1;
    if (++bits_needed_ == 0) {
      bits_needed_ = -8;
      value_ += bit_reader_->ReadByte();
    }
  }
  return 0;
}

void EntropyDecoder::Start() {
  range_ = 510;
  bits_needed_ = -8;
  value_ = (bit_reader_->ReadByte() << 8);
  value_ |= bit_reader_->ReadByte();
}

void EntropyDecoder::Finish() {
  bit_reader_->ReadBits(1);
  bit_reader_->SkipBits();
}

}   // namespace xvc
