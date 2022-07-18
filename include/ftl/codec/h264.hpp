/**
 * @file h264.hpp
 * @copyright Copyright (c) 2020 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <vector>
#include <list>
#include <string>
#include <ftl/codec/golomb.hpp>

namespace ftl {
namespace codec {

/**
 * H.264 codec utility functions.
 */
namespace h264 {

struct NALHeader {
    uint8_t type : 5;
    uint8_t ref_idc : 2;
    uint8_t forbidden : 1;
};

enum class ProfileIDC {
    kInvalid = 0,
    kBaseline = 66,
    kExtended = 88,
    kMain = 77,
    kHigh = 100,
    kHigh10 = 110
};

enum class LevelIDC {
    kInvalid = 0,
    kLevel1 = 10,
    kLevel1_1 = 11,
    kLevel1_2 = 12,
    kLevel1_3 = 13,
    kLevel2 = 20,
    kLevel2_1 = 21,
    kLevel2_2 = 22,
    kLevel3 = 30,
    kLevel3_1 = 31,
    kLevel3_2 = 32,
    kLevel4 = 40,
    kLevel4_1 = 41,
    kLevel4_2 = 42,
    kLevel5 = 50,
    kLevel5_1 = 51,
    kLevel5_2 = 52,
    kLevel6 = 60,
    kLevel6_1 = 61,
    kLevel6_2 = 62
};

enum class POCType {
    kType0 = 0,
    kType1 = 1,
    kType2 = 2
};

enum class ChromaFormatIDC {
    kMonochrome = 0,
    k420 = 1,
    k422 = 2,
    k444 = 3
};

struct PPS {
    int id = -1;
    int sps_id = 0;
    bool cabac = false;
    bool pic_order_present = false;
    int slice_group_count = 0;
    int mb_slice_group_map_type = 0;
    unsigned int ref_count[2];
    bool weighted_pred = false;
    int weighted_bipred_idc = 0;
    int init_qp = 0;
    int init_qs = 0;
    int chroma_qp_index_offset[2];
    bool deblocking_filter_parameters_present = false;
    bool constrained_intra_pred = false;
    bool redundant_pic_cnt_present = false;
    int transform_8x8_mode = 0;
    uint8_t scaling_matrix4[6][16];
    uint8_t scaling_matrix8[2][64];
    uint8_t chroma_qp_table[2][64];
    int chroma_qp_diff = 0;
};

struct SPS{
    int id = -1;
    ProfileIDC profile_idc = ProfileIDC::kInvalid;
    LevelIDC level_idc = LevelIDC::kInvalid;
    ChromaFormatIDC chroma_format_idc = ChromaFormatIDC::k420;
    int transform_bypass = 0;
    int log2_max_frame_num = 0;
    int maxFrameNum = 0;
    POCType poc_type = POCType::kType0;
    int log2_max_poc_lsb = 0;
    bool delta_pic_order_always_zero_flag = false;
    int offset_for_non_ref_pic = 0;
    int offset_for_top_to_bottom_field = 0;
    int poc_cycle_length = 0;
    int ref_frame_count = 0;
    bool gaps_in_frame_num_allowed_flag = false;
    int mb_width = 0;
    int mb_height = 0;
    bool frame_mbs_only_flag = false;
    int mb_aff = 0;
    bool direct_8x8_inference_flag = false;
    int crop = 0;
    unsigned int crop_left;
    unsigned int crop_right;
    unsigned int crop_top;
    unsigned int crop_bottom;
    bool vui_parameters_present_flag = false;
    // AVRational sar;
    int video_signal_type_present_flag = 0;
    int full_range = 0;
    int colour_description_present_flag = 0;
    // enum AVColorPrimaries color_primaries;
    // enum AVColorTransferCharacteristic color_trc;
    // enum AVColorSpace colorspace;
    int color_primaries = 0;
    int color_trc = 0;
    int colorspace = 0;
    int timing_info_present_flag = 0;
    uint32_t num_units_in_tick = 0;
    uint32_t time_scale = 0;
    int fixed_frame_rate_flag = 0;
    short offset_for_ref_frame[256];
    int bitstream_restriction_flag = 0;
    int num_reorder_frames = 0;
    int scaling_matrix_present = 0;
    uint8_t scaling_matrix4[6][16];
    uint8_t scaling_matrix8[2][64];
    int nal_hrd_parameters_present_flag = 0;
    int vcl_hrd_parameters_present_flag = 0;
    int pic_struct_present_flag = 0;
    int time_offset_length = 0;
    int cpb_cnt = 0;
    int initial_cpb_removal_delay_length = 0;
    int cpb_removal_delay_length = 0;
    int dpb_output_delay_length = 0;
    int bit_depth_luma = 0;
    int bit_depth_chroma = 0;
    int residual_color_transform_flag = 0;
};

enum class NALSliceType {
    kPType,
    kBType,
    kIType,
    kSPType,
    kSIType
};

/**
 * H264 Network Abstraction Layer Unit types.
 */
enum class NALType : int {
    UNSPECIFIED_0 = 0,
    CODED_SLICE_NON_IDR = 1,
    CODED_SLICE_PART_A = 2,
    CODED_SLICE_PART_B = 3,
    CODED_SLICE_PART_C = 4,
    CODED_SLICE_IDR = 5,
    SEI = 6,
    SPS = 7,
    PPS = 8,
    ACCESS_DELIMITER = 9,
    EO_SEQ = 10,
    EO_STREAM = 11,
    FILTER_DATA = 12,
    SPS_EXT = 13,
    PREFIX_NAL_UNIT = 14,
    SUBSET_SPS = 15,
    RESERVED_16 = 16,
    RESERVED_17 = 17,
    RESERVED_18 = 18,
    CODED_SLICE_AUX = 19,
    CODED_SLICE_EXT = 20,
    CODED_SLICE_DEPTH = 21,
    RESERVED_22 = 22,
    RESERVED_23 = 23,
    UNSPECIFIED_24 = 24,
    UNSPECIFIED_25,
    UNSPECIFIED_26,
    UNSPECIFIED_27,
    UNSPECIFIED_28,
    UNSPECIFIED_29,
    UNSPECIFIED_30,
    UNSPECIFIED_31
};

struct Slice {
    NALType type;
    int ref_idc = 0;
    int frame_number = 9;
    bool fieldPicFlag = false;
    bool usedForShortTermRef = false;
    bool bottomFieldFlag = false;
    int idr_pic_id = 0;
    int pic_order_cnt_lsb = 0;
    int delta_pic_order_cnt_bottom = 0;
    int delta_pic_order_cnt[2];
    int redundant_pic_cnt = 0;
    bool num_ref_idx_active_override_flag = false;
    int num_ref_idx_10_active_minus1 = 0;
    bool ref_pic_list_reordering_flag_10 = false;
    bool no_output_of_prior_pics_flag = false;
    bool long_term_reference_flag = false;
    bool adaptive_ref_pic_marking_mode_flag = false;
    int prevRefFrameNum = 0;
    int picNum = 0;
    size_t offset;
    size_t size;
    bool keyFrame = false;
    NALSliceType slice_type;
    int repeat_pic;
    int pictureStructure;
    const PPS *pps;
    const SPS *sps;
    std::vector<int> refPicList;
};

std::string prettySlice(const Slice &s);
std::string prettyPPS(const PPS &pps);
std::string prettySPS(const SPS &sps);

class Parser {
 public:
    Parser();
    ~Parser();

    std::list<Slice> parse(const std::vector<uint8_t> &data);

 private:
    PPS pps_;
    SPS sps_;
    int prevRefFrame_ = 0;

    void _parsePPS(ftl::codec::detail::ParseContext *ctx, size_t length);
    void _parseSPS(ftl::codec::detail::ParseContext *ctx, size_t length);
    bool _skipToNAL(ftl::codec::detail::ParseContext *ctx);
    Slice _createSlice(ftl::codec::detail::ParseContext *ctx, const NALHeader &header, size_t length);
};

inline NALType extractNALType(ftl::codec::detail::ParseContext *ctx) {
    auto t = static_cast<NALType>(ctx->ptr[ctx->index >> 3] & 0x1F);
    ctx->index += 8;
    return t;
}

/**
 * Extract the NAL unit type from the first NAL header.
 * With NvPipe, the 5th byte contains the NAL Unit header.
 */
inline NALType getNALType(const unsigned char *data, size_t size) {
    return (size > 4) ? static_cast<NALType>(data[4] & 0x1F) : NALType::UNSPECIFIED_0;
}

inline bool validNAL(const unsigned char *data, size_t size) {
    return size > 4 && data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1;
}

/**
 * Check the H264 bitstream for an I-Frame. With NvPipe, all I-Frames start
 * with a SPS NAL unit so just check for this.
 */
inline bool isIFrame(const unsigned char *data, size_t size) {
    return getNALType(data, size) == NALType::SPS;
}

}  // namespace h264
}  // namespace codec
}  // namespace ftl
