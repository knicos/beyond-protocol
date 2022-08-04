/**
 * @file h264.cpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#include <sstream>
#include <ftl/codec/h264.hpp>
#include <ftl/exception.hpp>
#include <loguru.hpp>

using ftl::codec::detail::ParseContext;
using ftl::codec::h264::PPS;
using ftl::codec::h264::SPS;
using ftl::codec::h264::Slice;
using ftl::codec::h264::NALType;
using ftl::codec::h264::NALHeader;
using ftl::codec::h264::NALSliceType;
using ftl::codec::h264::ProfileIDC;
using ftl::codec::h264::POCType;
using ftl::codec::h264::LevelIDC;
using ftl::codec::h264::ChromaFormatIDC;
using ftl::codec::detail::golombUnsigned;
using ftl::codec::detail::golombSigned;
using ftl::codec::detail::getBits1;

static NALHeader extractNALHeader(ParseContext *ctx) {
    auto t = *reinterpret_cast<const NALHeader*>(&ctx->ptr[ctx->index >> 3]);
    ctx->index += 8;
    return t;
}

bool ftl::codec::h264::Parser::_skipToNAL(ParseContext *ctx) {
    uint32_t code = 0xFFFFFFFF;

    while (ctx->index < ctx->length && (code & 0xFFFFFF) != 1) {
        code = (code << 8) | ctx->ptr[ctx->index >> 3];
        ctx->index += 8;
    }

    return ((code & 0xFFFFFF) == 1);
}

static void decodeScalingList(ParseContext *ctx, uint8_t *factors, int size) {
    if (!getBits1(ctx)) {
        // TODO(Nick): Fallback
    } else {
        int next = 8;
        int last = 8;

        for (int i = 0; i < size; i++) {
            if (next) {
                // TODO(Nick): Actually save the result...
                next = ((last + golombSigned(ctx)) & 0xff);
            }
            if (!i && !next) {
                // TODO(Nick): Fallback
                break;
            }
            last = next ? next : last;
        }
    }
}

ftl::codec::h264::Parser::Parser() {}

ftl::codec::h264::Parser::~Parser() {}

void ftl::codec::h264::Parser::_parseSPS(ParseContext *ctx, size_t length) {
    int profile_idc = getBits(ctx, 8);
    getBits1(ctx);
    getBits1(ctx);
    getBits1(ctx);
    getBits1(ctx);
    getBits(ctx, 4);

    int level_idc = getBits(ctx, 8);
    unsigned int sps_id = golombUnsigned31(ctx);
    sps_.id = sps_id;

    sps_.profile_idc = static_cast<ProfileIDC>(profile_idc);
    sps_.level_idc = static_cast<LevelIDC>(level_idc);

    // memset scaling matrix 4 and 8 to 16
    sps_.scaling_matrix_present = 0;

    if (static_cast<int>(sps_.profile_idc) >= 100) {  // high profile
        sps_.chroma_format_idc = static_cast<ChromaFormatIDC>(golombUnsigned31(ctx));
        if (static_cast<int>(sps_.chroma_format_idc) > 3) {
            throw FTL_Error("Invalid chroma format");
        }
        if (sps_.chroma_format_idc == ChromaFormatIDC::k444) {
            sps_.residual_color_transform_flag = getBits1(ctx);
        }
        sps_.bit_depth_luma = golombUnsigned(ctx) + 8;
        sps_.bit_depth_chroma = golombUnsigned(ctx) + 8;
        sps_.transform_bypass = getBits1(ctx);
        // scaling matrices?
        if (getBits1(ctx)) {
            sps_.scaling_matrix_present = 1;
            decodeScalingList(ctx, nullptr, 16);
            decodeScalingList(ctx, nullptr, 16);
            decodeScalingList(ctx, nullptr, 16);
            decodeScalingList(ctx, nullptr, 16);
            decodeScalingList(ctx, nullptr, 16);
            decodeScalingList(ctx, nullptr, 16);

            decodeScalingList(ctx, nullptr, 16);
            decodeScalingList(ctx, nullptr, 16);
        }
    } else {
        sps_.chroma_format_idc = ChromaFormatIDC::k420;
        sps_.bit_depth_luma = 8;
        sps_.bit_depth_chroma = 8;
    }

    sps_.log2_max_frame_num = golombUnsigned(ctx) + 4;
    sps_.maxFrameNum = 1 << sps_.log2_max_frame_num;
    sps_.poc_type = static_cast<POCType>(golombUnsigned31(ctx));
    if (sps_.poc_type == POCType::kType0) {
        sps_.log2_max_poc_lsb = golombUnsigned(ctx) + 4;
    } else if (sps_.poc_type == POCType::kType1) {
        sps_.delta_pic_order_always_zero_flag = getBits1(ctx);
        sps_.offset_for_non_ref_pic = golombSigned(ctx);
        sps_.offset_for_top_to_bottom_field = golombSigned(ctx);
        sps_.poc_cycle_length = golombUnsigned(ctx);

        for (int i = 0; i < sps_.poc_cycle_length; i++) {
            sps_.offset_for_ref_frame[i] = golombSigned(ctx);
        }
    } else {
        // fail
    }

    sps_.ref_frame_count = golombUnsigned31(ctx);
    sps_.gaps_in_frame_num_allowed_flag = getBits1(ctx);
    sps_.mb_width = golombUnsigned(ctx) + 1;
    sps_.mb_height = golombUnsigned(ctx) + 1;
    sps_.frame_mbs_only_flag = getBits1(ctx);
    if (!sps_.frame_mbs_only_flag) {
        sps_.mb_aff = getBits1(ctx);
    } else {
        sps_.mb_aff = 0;
    }

    sps_.direct_8x8_inference_flag = getBits1(ctx);
    sps_.crop = getBits1(ctx);
    if (sps_.crop) {
        sps_.crop_left = golombUnsigned(ctx);
        sps_.crop_right = golombUnsigned(ctx);
        sps_.crop_top = golombUnsigned(ctx);
        sps_.crop_bottom = golombUnsigned(ctx);
    } else {
        sps_.crop_left = 0;
        sps_.crop_right = 0;
        sps_.crop_top = 0;
        sps_.crop_bottom = 0;
    }

    sps_.vui_parameters_present_flag = getBits1(ctx);
    if (sps_.vui_parameters_present_flag) {
        if (getBits1(ctx)) {
            // Aspect ratio info
            int ratio_idc = getBits(ctx, 8);
            if (ratio_idc == 255) {
                LOG(WARNING) << "Extended SAR";
            }
        }
        if (getBits1(ctx)) {
            getBits1(ctx);
        }
        sps_.video_signal_type_present_flag = getBits1(ctx);
        if (sps_.video_signal_type_present_flag) {
            LOG(WARNING) << "Video signal info present";
        }
        if (getBits1(ctx)) {
            LOG(WARNING) << "Chromo location info";
        }
        sps_.timing_info_present_flag = getBits1(ctx);
        if (sps_.timing_info_present_flag) {
            sps_.num_units_in_tick = getBits(ctx, 32);
            sps_.time_scale = getBits(ctx, 32);
            sps_.fixed_frame_rate_flag = getBits1(ctx);
        }
        sps_.nal_hrd_parameters_present_flag = getBits1(ctx);
        if (sps_.nal_hrd_parameters_present_flag) {
            LOG(WARNING) << "NAL HRD present";
        }
        sps_.vcl_hrd_parameters_present_flag = getBits1(ctx);
        if (sps_.vcl_hrd_parameters_present_flag) {
            LOG(WARNING) << "VCL HRD present";
        }
        sps_.pic_struct_present_flag = getBits1(ctx);
        sps_.bitstream_restriction_flag = getBits1(ctx);
        if (sps_.bitstream_restriction_flag) {
            LOG(WARNING) << "Bitstream restriction";
        }
    }

    _checkEnding(ctx, length);
}

void ftl::codec::h264::Parser::_parsePPS(ParseContext *ctx, size_t length) {
    pps_.id = golombUnsigned(ctx);
    pps_.sps_id = golombUnsigned31(ctx);

    pps_.cabac = getBits1(ctx);  // Entropy encoding mode
    pps_.pic_order_present = getBits1(ctx);
    pps_.slice_group_count = golombUnsigned(ctx) + 1;
    if (pps_.slice_group_count > 1) {
        pps_.mb_slice_group_map_type = golombUnsigned(ctx);
        LOG(WARNING) << "Slice group parsing";
    }
    pps_.ref_count[0] = golombUnsigned(ctx) + 1;
    pps_.ref_count[1] = golombUnsigned(ctx) + 1;
    pps_.weighted_pred = getBits1(ctx);
    pps_.weighted_bipred_idc = getBits(ctx, 2);
    pps_.init_qp = golombSigned(ctx) + 26;
    pps_.init_qs = golombSigned(ctx) + 26;
    pps_.chroma_qp_index_offset[0] = golombSigned(ctx);
    pps_.deblocking_filter_parameters_present = getBits1(ctx);
    pps_.constrained_intra_pred = getBits1(ctx);
    pps_.redundant_pic_cnt_present = getBits1(ctx);
    pps_.transform_8x8_mode = 0;

    // Copy scaling matrix 4 and 8 from SPS

    if (ctx->index < length) {
        // Read some other stuff
        pps_.transform_8x8_mode = getBits1(ctx);
        // Decode scaling matrices
        if (getBits1(ctx)) {
            LOG(WARNING) << "HAS SCALING MATRIX";
        }
        pps_.chroma_qp_index_offset[1] = golombSigned(ctx);
    } else {
        pps_.chroma_qp_index_offset[1] = pps_.chroma_qp_index_offset[0];
    }

    // TODO: Build QP table.

    if (pps_.chroma_qp_index_offset[0] != pps_.chroma_qp_index_offset[1]) {
        pps_.chroma_qp_diff = 1;
    }

    _checkEnding(ctx, length);
}

void ftl::codec::h264::Parser::_checkEnding(ParseContext *ctx, size_t length) {
    if (!getBits1(ctx)) {
        throw FTL_Error("Missing NAL stop bit");
    }
    int remainingBits = 8 - (ctx->index % 8);
    if (remainingBits != 8) {
        if (getBits(ctx, remainingBits) != 0) {
            throw FTL_Error("Non-zero terminating bits");
        }
    }
    if (length - ctx->index != 16) {
        throw FTL_Error("No trailing zero word");
    }
    if (getBits(ctx, 16) != 0) {
        throw FTL_Error("Trailing bits not zero");
    }
}

Slice ftl::codec::h264::Parser::_createSlice(ParseContext *ctx, const NALHeader &header, size_t length) {
    Slice s;
    s.type = static_cast<NALType>(header.type);
    s.ref_idc = header.ref_idc;

    golombUnsigned(ctx);  // skip first_mb_in_slice
    s.slice_type = static_cast<NALSliceType>(golombUnsigned31(ctx));
    if (s.type == NALType::CODED_SLICE_IDR) {
        s.keyFrame = true;
    } else {
        s.keyFrame = false;
    }
    int ppsId = golombUnsigned(ctx);
    if (pps_.id != ppsId) {
        throw FTL_Error("Unknown PPS");
    }
    if (sps_.id != pps_.sps_id) {
        throw FTL_Error("Unknown SPS: " << sps_.id << " " << pps_.sps_id);
    }
    s.pps = &pps_;
    s.sps = &sps_;
    s.frame_number = getBits(ctx, s.sps->log2_max_frame_num);

    if (!s.sps->frame_mbs_only_flag) {
        s.fieldPicFlag = getBits1(ctx);
        if (s.fieldPicFlag) {
            s.bottomFieldFlag = getBits1(ctx);
        }
    }
    if (s.type == NALType::CODED_SLICE_IDR) {
        s.idr_pic_id = golombUnsigned(ctx);
        s.prevRefFrameNum = 0;
        prevRefFrame_ = s.frame_number;
    } else {
        s.prevRefFrameNum = prevRefFrame_;
        if (s.ref_idc > 0) {
            prevRefFrame_ = s.frame_number;
        }
    }

    if (s.sps->poc_type == POCType::kType0) {
        s.pic_order_cnt_lsb = getBits(ctx, s.sps->log2_max_poc_lsb);
        if (s.pps->pic_order_present && !s.fieldPicFlag) {
            s.delta_pic_order_cnt_bottom = golombSigned(ctx);
        }
    }
    if (s.sps->poc_type == POCType::kType1 && !s.sps->delta_pic_order_always_zero_flag) {
        s.delta_pic_order_cnt[0] = golombSigned(ctx);
        if (s.pps->pic_order_present && !s.fieldPicFlag) {
            s.delta_pic_order_cnt[1] = golombSigned(ctx);
        }
    }

    if (s.pps->redundant_pic_cnt_present) {
        s.redundant_pic_cnt = golombUnsigned(ctx);
    }

    if (s.slice_type == NALSliceType::kPType || s.slice_type == NALSliceType::kSPType) {
        s.num_ref_idx_active_override_flag = getBits1(ctx);
        if (s.num_ref_idx_active_override_flag) {
            s.num_ref_idx_10_active_minus1 = golombUnsigned(ctx);
        }
    }

    if (s.slice_type != NALSliceType::kIType && s.slice_type != NALSliceType::kSIType) {
        s.ref_pic_list_reordering_flag_10 = getBits1(ctx);
        if (s.ref_pic_list_reordering_flag_10) {
            LOG(ERROR) << "Need to parse pic list";
        }
    }

    if (s.pps->weighted_pred) {
        LOG(ERROR) << "Need to parse weight table";
    }

    if (s.ref_idc != 0) {
        if (s.type == NALType::CODED_SLICE_IDR) {
            s.no_output_of_prior_pics_flag = getBits1(ctx);
            s.long_term_reference_flag = getBits1(ctx);
            s.usedForShortTermRef = !s.long_term_reference_flag;
        } else {
            s.usedForShortTermRef = true;
            s.adaptive_ref_pic_marking_mode_flag = getBits1(ctx);
            if (s.adaptive_ref_pic_marking_mode_flag) {
                LOG(ERROR) << "Parse adaptive ref";
            }
        }
    }

    s.picNum = s.frame_number % s.sps->maxFrameNum;

    if (s.type != NALType::CODED_SLICE_IDR) {
        int numRefFrames = (s.num_ref_idx_active_override_flag)
            ? s.num_ref_idx_10_active_minus1 + 1
            : s.sps->ref_frame_count;
        s.refPicList.resize(numRefFrames);
        int fn = s.frame_number - 1;
        for (size_t i = 0; i < s.refPicList.size(); i++) {
            s.refPicList[i] = fn--;
        }
    }

    return s;
}

std::list<Slice> ftl::codec::h264::Parser::parse(const std::vector<uint8_t> &data) {
    std::list<Slice> slices;
    Slice slice;
    size_t offset = 0;
    size_t length = 0;

    ParseContext parseCtx = {
        data.data(), 0, 0
    };
    parseCtx.length = data.size() * 8;
    _skipToNAL(&parseCtx);

    ParseContext nextCtx = parseCtx;

    while (true) {
        bool hasNext = _skipToNAL(&nextCtx);
        offset = parseCtx.index;
        length = (hasNext) ? nextCtx.index - parseCtx.index - 24 : data.size() * 8 - parseCtx.index;
        // auto type = ftl::codecs::h264::extractNALType(&parseCtx);
        auto header = extractNALHeader(&parseCtx);
        auto type = static_cast<NALType>(header.type);

        switch (type) {
        case NALType::SPS:
            _parseSPS(&parseCtx, length + parseCtx.index);
            if (parseCtx.index > nextCtx.index) {
                throw FTL_Error("Bad SPS parse");
            }
            break;
        case NALType::PPS:
            _parsePPS(&parseCtx, length + parseCtx.index);
            if (parseCtx.index > nextCtx.index) {
                throw FTL_Error("Bad PPS parse");
            }
            break;
        case NALType::CODED_SLICE_IDR:
        case NALType::CODED_SLICE_NON_IDR:
            slice = _createSlice(&parseCtx, header, 0);
            slice.offset = offset / 8;
            slice.size = length / 8;
            slices.push_back(slice);
            break;
        default:
            LOG(ERROR) << "Unrecognised NAL type: " << int(header.type);
        }

        parseCtx = nextCtx;

        if (!hasNext) break;
    }

    return slices;
}

std::string ftl::codec::h264::prettySlice(const Slice &s) {
    std::stringstream stream;
    stream << "  - Type: " << std::to_string(static_cast<int>(s.type)) << std::endl;
    stream << "  - size: " << std::to_string(s.size) << " bytes" << std::endl;
    stream << "  - offset: " << std::to_string(s.offset) << " bytes" << std::endl;
    stream << "  - ref_idc: " << std::to_string(s.ref_idc) << std::endl;
    stream << "  - frame_num: " << std::to_string(s.frame_number) << std::endl;
    stream << "  - field_pic_flag: " << std::to_string(s.fieldPicFlag) << std::endl;
    stream << "  - usedForShortRef: " << std::to_string(s.usedForShortTermRef) << std::endl;
    stream << "  - slice_type: " << std::to_string(static_cast<int>(s.slice_type)) << std::endl;
    stream << "  - bottom_field_flag: " << std::to_string(s.bottomFieldFlag) << std::endl;
    stream << "  - idr_pic_id: " << std::to_string(s.idr_pic_id) << std::endl;
    stream << "  - redundant_pic_cnt: " << std::to_string(s.redundant_pic_cnt) << std::endl;
    stream << "  - num_ref_idx_active_override_flag: "
        << std::to_string(s.num_ref_idx_active_override_flag) << std::endl;
    stream << "  - num_ref_idx_10_active_minus1: "
        << std::to_string(s.num_ref_idx_10_active_minus1) << std::endl;
    stream << "  - ref_pic_list_reordering_flag: " << std::to_string(s.ref_pic_list_reordering_flag_10) << std::endl;
    stream << "  - long_term_reference_flag: " << std::to_string(s.long_term_reference_flag) << std::endl;
    stream << "  - adaptive_ref_pic_marking_mode_flag: "
        << std::to_string(s.adaptive_ref_pic_marking_mode_flag) << std::endl;
    stream << "  - picNum: " << std::to_string(s.picNum) << std::endl;
    stream << "  - refPicList (" << std::to_string(s.refPicList.size()) << "): ";
    for (int r : s.refPicList) {
        stream << std::to_string(r) << ", ";
    }
    stream << std::endl;
    stream << "PPS:" << std::endl << prettyPPS(*s.pps);
    stream << "SPS:" << std::endl << prettySPS(*s.sps);
    return stream.str();
}

std::string ftl::codec::h264::prettyPPS(const PPS &pps) {
    std::stringstream stream;
    stream << "  - id: " << std::to_string(pps.id) << std::endl;
    stream << "  - sps_id: " << std::to_string(pps.sps_id) << std::endl;
    stream << "  - pic_order_present: " << std::to_string(pps.pic_order_present) << std::endl;
    stream << "  - ref_count_0: " << std::to_string(pps.ref_count[0]) << std::endl;
    stream << "  - ref_count_1: " << std::to_string(pps.ref_count[1]) << std::endl;
    stream << "  - weighted_pred: " << std::to_string(pps.weighted_pred) << std::endl;
    stream << "  - init_qp: " << std::to_string(pps.init_qp) << std::endl;
    stream << "  - init_qs: " << std::to_string(pps.init_qs) << std::endl;
    stream << "  - transform_8x8_mode: " << std::to_string(pps.transform_8x8_mode) << std::endl;
    return stream.str();
}

std::string ftl::codec::h264::prettySPS(const SPS &sps) {
    std::stringstream stream;
    stream << "  - id: " << std::to_string(sps.id) << std::endl;
    stream << "  - profile_idc: " << std::to_string(static_cast<int>(sps.profile_idc)) << std::endl;
    stream << "  - level_idc: " << std::to_string(static_cast<int>(sps.level_idc)) << std::endl;
    stream << "  - chroma_format_idc: " << std::to_string(static_cast<int>(sps.chroma_format_idc)) << std::endl;
    stream << "  - transform_bypass: " << std::to_string(sps.transform_bypass) << std::endl;
    stream << "  - scaling_matrix_present: " << std::to_string(sps.scaling_matrix_present) << std::endl;
    stream << "  - maxFrameNum: " << std::to_string(sps.maxFrameNum) << std::endl;
    stream << "  - poc_type: " << std::to_string(static_cast<int>(sps.poc_type)) << std::endl;
    stream << "  - offset_for_non_ref_pic: " << std::to_string(sps.offset_for_non_ref_pic) << std::endl;
    stream << "  - ref_frame_count: " << std::to_string(sps.ref_frame_count) << std::endl;
    stream << "  - gaps_in_frame_num_allowed_flag: " << std::to_string(sps.gaps_in_frame_num_allowed_flag) << std::endl;
    stream << "  - width: " << std::to_string(sps.mb_width * 16) << std::endl;
    stream << "  - height: " << std::to_string(sps.mb_height * 16) << std::endl;
    return stream.str();
}
