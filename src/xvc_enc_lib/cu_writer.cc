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

#include "xvc_enc_lib/cu_writer.h"

#include "xvc_common_lib/restrictions.h"
#include "xvc_common_lib/utils.h"

namespace xvc {

bool CuWriter::WriteCtu(CodingUnit *ctu, PictureData *cu_map,
                        SyntaxWriter *writer) {
  ctu_has_coeffs_ = false;
  cu_map->ClearMarkCuInPic(ctu);
  WriteCu(ctu, SplitRestriction::kNone, cu_map, writer);
  return ctu_has_coeffs_;
}

void CuWriter::WriteCu(CodingUnit *cu, SplitRestriction split_restriction,
                       PictureData *cu_map, SyntaxWriter *writer) {
  WriteSplit(*cu, split_restriction, writer);
  if (cu->GetSplit() != SplitType::kNone) {
    SplitRestriction sub_split_restriction = SplitRestriction::kNone;
    for (int i = 0; i < constants::kQuadSplit; i++) {
      CodingUnit *sub_cu = cu->GetSubCu(i);
      if (sub_cu) {
        WriteCu(sub_cu, sub_split_restriction, cu_map, writer);
        sub_split_restriction =
          sub_cu->DeriveSiblingSplitRestriction(cu->GetSplit());
      }
    }
  } else {
    cu_map->MarkUsedInPic(cu);
    for (YuvComponent comp : pic_data_.GetComponents(cu->GetCuTree())) {
      WriteComponent(*cu, comp, writer);
    }
  }
}

void CuWriter::WriteSplit(const CodingUnit &cu,
                          SplitRestriction split_restriction,
                          SyntaxWriter *writer) {
  SplitType split_type = cu.GetSplit();
  int binary_depth = cu.GetBinaryDepth();
  int max_depth = pic_data_.GetMaxDepth(cu.GetCuTree());
  if (cu.GetDepth() < max_depth && binary_depth == 0) {
    if (cu.IsFullyWithinPicture()) {
      writer->WriteSplitQuad(cu, max_depth, split_type);
    } else {
      assert(split_type != SplitType::kNone);
    }
  }
  if (split_type != SplitType::kQuad) {
    if (cu.IsBinarySplitValid()) {
      writer->WriteSplitBinary(cu, split_restriction, split_type);
    }
  }
}

void CuWriter::WriteComponent(const CodingUnit &cu, YuvComponent comp,
                              SyntaxWriter *writer) {
  if (util::IsLuma(comp)) {
    if (!pic_data_.IsIntraPic()) {
      writer->WriteSkipFlag(cu, cu.GetSkipFlag());
      if (cu.GetSkipFlag() && !Restrictions::Get().disable_inter_skip_mode) {
        assert(cu.GetMergeIdx() >= 0);
        writer->WriteMergeIdx(cu.GetMergeIdx());
        return;
      }
      writer->WritePredMode(cu.GetPredMode());
    } else {
      assert(cu.GetPredMode() == PredictionMode::kIntra);
    }
    if (Restrictions::Get().disable_ext_implicit_partition_type) {
      writer->WritePartitionType(cu, cu.GetPartitionType());
    }
  } else if (cu.GetSkipFlag()) {
    return;
  }

  if (cu.IsIntra()) {
    WriteIntraPrediction(cu, comp, writer);
  } else {
    WriteInterPrediction(cu, comp, writer);
  }
  WriteResidualData(cu, comp, writer);
}

void CuWriter::WriteIntraPrediction(const CodingUnit &cu, YuvComponent comp,
                                    SyntaxWriter *writer) {
  const CodingUnit *luma_cu = pic_data_.GetLumaCu(&cu);
  IntraMode luma_mode = luma_cu->GetIntraMode(YuvComponent::kY);
  if (util::IsLuma(comp)) {
    IntraPredictorLuma mpm = intra_pred_->GetPredictorLuma(cu);
    writer->WriteIntraMode(luma_mode, mpm);
  } else if (util::IsFirstChroma(comp)) {
    IntraPredictorChroma
      chroma_preds = intra_pred_->GetPredictorsChroma(luma_mode);
    if (!Restrictions::Get().disable_intra_chroma_predictor) {
      writer->WriteIntraChromaMode(cu.GetIntraChromaMode(), chroma_preds);
    }
  }
}

void CuWriter::WriteInterPrediction(const CodingUnit &cu, YuvComponent comp,
                                    SyntaxWriter *writer) {
  if (util::IsLuma(comp)) {
    writer->WriteMergeFlag(cu.GetMergeFlag());
    if (cu.GetMergeFlag()) {
      assert(cu.GetHasAnyCbf() || Restrictions::Get().disable_inter_skip_mode);
      writer->WriteMergeIdx(cu.GetMergeIdx());
      return;
    }
    if (pic_data_.GetPredictionType() == PicturePredictionType::kBi) {
      writer->WriteInterDir(cu, cu.GetInterDir());
    } else {
      assert(cu.GetInterDir() == InterDir::kL0);
    }
    for (int i = 0; i < static_cast<int>(RefPicList::kTotalNumber); i++) {
      RefPicList ref_pic_list = static_cast<RefPicList>(i);
      if (!ReferencePictureLists::IsRefPicListUsed(ref_pic_list,
                                                   cu.GetInterDir())) {
        continue;
      }
      int num_refs_available =
        pic_data_.GetRefPicLists()->GetNumRefPics(ref_pic_list);
      assert(num_refs_available > 0);
      writer->WriteInterRefIdx(cu.GetRefIdx(ref_pic_list), num_refs_available);
      if (!cu.GetForceMvdZero(ref_pic_list)) {
        writer->WriteInterMvd(cu.GetMvDelta(ref_pic_list));
      } else {
        assert(cu.GetMvDelta(ref_pic_list) == MotionVector(0, 0));
      }
      writer->WriteInterMvpIdx(cu.GetMvpIdx(ref_pic_list));
    }
    if (!cu.HasZeroMvd()) {
      writer->WriteInterFullpelMvFlag(cu, cu.GetFullpelMv());
    }
    if (pic_data_.GetUseLocalIlluminationCompensation()) {
      writer->WriteLicFlag(cu.GetUseLic());
    } else {
      assert(!cu.GetUseLic());
    }
  }
}

void CuWriter::WriteResidualData(const CodingUnit &cu, YuvComponent comp,
                                 SyntaxWriter *writer) {
  bool cbf = WriteCbfInvariant(cu, comp, writer);
  if (cbf) {
    ctu_has_coeffs_ = true;
    WriteResidualDataInternal(cu, comp, writer);
  }
}

void CuWriter::WriteResidualDataRdoCbf(const CodingUnit &cu, YuvComponent comp,
                                       SyntaxWriter *writer) const {
  bool cbf = cu.GetCbf(comp);
  // Encoder rdo only, because normally cbf flags are written for all
  // components during invocation of the luma component
  writer->WriteCbf(cu, comp, cbf);
  if (cbf) {
    WriteResidualDataInternal(cu, comp, writer);
  }
}

void
CuWriter::WriteResidualDataInternal(const CodingUnit &cu, YuvComponent comp,
                                    SyntaxWriter *writer) const {
  CoeffBufferConst cu_coeff = cu.GetCoeff(comp);
  bool use_transform_select = false;
  if (util::IsLuma(comp)) {
    use_transform_select = cu.HasTransformSelectIdx();
    writer->WriteTransformSelectEnable(cu, use_transform_select);
  }
  writer->WriteTransformSkip(cu, comp, cu.GetTransformSkip(comp));
  int num_coeff =
    writer->WriteCoefficients(cu, comp, cu_coeff.GetDataPtr(),
                              cu_coeff.GetStride());
  if (util::IsLuma(comp) && use_transform_select) {
    if (!cu.GetTransformSkip(comp) &&
      (cu.IsInter() || num_coeff >= constants::kTransformSelectMinSigCoeffs)) {
      writer->WriteTransformSelectIdx(cu, cu.GetTransformSelectIdx());
    } else {
      assert(cu.GetTransformSelectIdx() == 0);
    }
  }
}

bool CuWriter::WriteCbfInvariant(const CodingUnit &cu, YuvComponent comp,
                                 SyntaxWriter *writer) const {
  bool signal_root_cbf = cu.IsInter() &&
    !Restrictions::Get().disable_transform_root_cbf &&
    (!cu.GetMergeFlag() || Restrictions::Get().disable_inter_skip_mode);
  if (signal_root_cbf) {
    bool root_cbf = cu.GetRootCbf();
    if (util::IsLuma(comp)) {
      writer->WriteRootCbf(root_cbf);
    }
    if (!root_cbf) {
      return false;
    }
  }

  bool cbf = cu.GetCbf(comp);
  if (Restrictions::Get().disable_transform_cbf) {
    assert(cbf);
  } else if (cu.IsIntra()) {
    writer->WriteCbf(cu, comp, cbf);
  } else if (util::IsLuma(comp)) {
    // Inter luma comp will write all cbf flags since chroma is written first
    // TODO(PH) Check for monochrome chroma format
    writer->WriteCbf(cu, YuvComponent::kU, cu.GetCbf(YuvComponent::kU));
    writer->WriteCbf(cu, YuvComponent::kV, cu.GetCbf(YuvComponent::kV));
    if (cu.GetCbf(YuvComponent::kU) || cu.GetCbf(YuvComponent::kV) ||
        Restrictions::Get().disable_transform_root_cbf) {
      writer->WriteCbf(cu, YuvComponent::kY, cbf);
    } else {
      assert(cbf);  // implicit signaling through root cbf
    }
  } else {
    // signaled by luma
  }
  return cbf;
}

}   // namespace xvc
