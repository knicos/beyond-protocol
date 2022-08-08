/**
 * @file channelUtils.cpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#include <ftl/protocol/config.h>
#include <string>
#include <unordered_map>
#include <ftl/protocol/channelUtils.hpp>

using ftl::protocol::Channel;

#ifndef CV_8U
#define CV_CN_SHIFT   3
#define CV_DEPTH_MAX  (1 << CV_CN_SHIFT)

#define CV_8U   0
#define CV_8S   1
#define CV_16U  2
#define CV_16S  3
#define CV_32S  4
#define CV_32F  5
#define CV_64F  6
#define CV_16F  7

#define CV_MAT_DEPTH_MASK       (CV_DEPTH_MAX - 1)
#define CV_MAT_DEPTH(flags)     ((flags) & CV_MAT_DEPTH_MASK)

#define CV_MAKETYPE(depth,cn) (CV_MAT_DEPTH(depth) + (((cn)-1) << CV_CN_SHIFT))
#define CV_MAKE_TYPE CV_MAKETYPE

#define CV_8UC1 CV_MAKETYPE(CV_8U,1)
#define CV_8UC2 CV_MAKETYPE(CV_8U,2)
#define CV_8UC3 CV_MAKETYPE(CV_8U,3)
#define CV_8UC4 CV_MAKETYPE(CV_8U,4)
#define CV_8UC(n) CV_MAKETYPE(CV_8U,(n))

#define CV_8SC1 CV_MAKETYPE(CV_8S,1)
#define CV_8SC2 CV_MAKETYPE(CV_8S,2)
#define CV_8SC3 CV_MAKETYPE(CV_8S,3)
#define CV_8SC4 CV_MAKETYPE(CV_8S,4)
#define CV_8SC(n) CV_MAKETYPE(CV_8S,(n))

#define CV_16UC1 CV_MAKETYPE(CV_16U,1)
#define CV_16UC2 CV_MAKETYPE(CV_16U,2)
#define CV_16UC3 CV_MAKETYPE(CV_16U,3)
#define CV_16UC4 CV_MAKETYPE(CV_16U,4)
#define CV_16UC(n) CV_MAKETYPE(CV_16U,(n))

#define CV_16SC1 CV_MAKETYPE(CV_16S,1)
#define CV_16SC2 CV_MAKETYPE(CV_16S,2)
#define CV_16SC3 CV_MAKETYPE(CV_16S,3)
#define CV_16SC4 CV_MAKETYPE(CV_16S,4)
#define CV_16SC(n) CV_MAKETYPE(CV_16S,(n))

#define CV_32SC1 CV_MAKETYPE(CV_32S,1)
#define CV_32SC2 CV_MAKETYPE(CV_32S,2)
#define CV_32SC3 CV_MAKETYPE(CV_32S,3)
#define CV_32SC4 CV_MAKETYPE(CV_32S,4)
#define CV_32SC(n) CV_MAKETYPE(CV_32S,(n))

#define CV_32FC1 CV_MAKETYPE(CV_32F,1)
#define CV_32FC2 CV_MAKETYPE(CV_32F,2)
#define CV_32FC3 CV_MAKETYPE(CV_32F,3)
#define CV_32FC4 CV_MAKETYPE(CV_32F,4)
#define CV_32FC(n) CV_MAKETYPE(CV_32F,(n))

#define CV_64FC1 CV_MAKETYPE(CV_64F,1)
#define CV_64FC2 CV_MAKETYPE(CV_64F,2)
#define CV_64FC3 CV_MAKETYPE(CV_64F,3)
#define CV_64FC4 CV_MAKETYPE(CV_64F,4)
#define CV_64FC(n) CV_MAKETYPE(CV_64F,(n))

#define CV_16FC1 CV_MAKETYPE(CV_16F,1)
#define CV_16FC2 CV_MAKETYPE(CV_16F,2)
#define CV_16FC3 CV_MAKETYPE(CV_16F,3)
#define CV_16FC4 CV_MAKETYPE(CV_16F,4)
#define CV_16FC(n) CV_MAKETYPE(CV_16F,(n))
#endif

struct ChannelInfo {
    const char *name;
    int cvtype = -1;
};

/* Name and type lookup table for channels */
static const std::unordered_map<Channel,ChannelInfo> info = {
    {Channel::kColour,              {"Left",                CV_8UC4}},
    {Channel::kDepth,               {"Depth",               CV_32F}},
    {Channel::kRight,               {"Right",               CV_8UC4}},
    {Channel::kDepth2,              {"Depth Right",         CV_32F}},
    {Channel::kDeviation,           {"Deviation",           CV_32F}},
    {Channel::kNormals,             {"Normals",             CV_32FC4}},
    {Channel::kWeights,             {"Weights",             CV_32F}},
    {Channel::kConfidence,          {"Confidence",          CV_32F}},
    {Channel::kEnergyVector,        {"Energy Vector",       CV_32FC4}},
    {Channel::kFlow,                {"Flow",                CV_32F}},
    {Channel::kEnergy,              {"Energy",              CV_32F}},
    {Channel::kMask,                {"Mask",                CV_8U}},
    {Channel::kDensity,             {"Density",             CV_32F}},
    {Channel::kSupport1,            {"Support1",            CV_8UC4}},
    {Channel::kSupport2,            {"Support2",            CV_8UC4}},
    {Channel::kSegmentation,        {"Segmentation",        CV_8U}},
    {Channel::kNormals2,            {"Normals Right",       CV_32FC4}},
    {Channel::kUNUSED1,             {"Unused",              CV_8UC4}},
    {Channel::kDisparity,           {"Disparity",           CV_16S}},
    {Channel::kSmoothing,           {"Smoothing",           CV_32F}},
    {Channel::kUNUSED2,             {"Unused",              CV_8UC4}},
    {Channel::kOverlay,             {"Overlay",             CV_8UC4}},
    {Channel::kGroundTruth,         {"Ground Truth",        CV_32F}},

    {Channel::kAudioMono,           {"Audio (Mono)",        -1}},
    {Channel::kAudioStereo,         {"Audio (Stereo)",      -1}},

    {Channel::kConfiguration,       {"Configuration",       -1}},
    {Channel::kCalibration,         {"Calibration",         -1}},
    {Channel::kPose,                {"Pose",                -1}},
    {Channel::kCalibration2,        {"CalibrationHR",       -1}},
    {Channel::kMetaData,            {"Meta Data",           -1}},
    {Channel::kCapabilities,        {"Capabilities",        -1}},
    {Channel::kCalibrationData,     {"CalibrationData",     -1}},
    {Channel::kThumbnail,           {"Thumbnail",           -1}},
    {Channel::kOverlaySelect,       {"OverlaySelect",       -1}},
    {Channel::kStartTime,           {"StartTime",           -1}},
    {Channel::kUser,                {"User",                -1}},
    {Channel::kName,                {"Name",                -1}},
    {Channel::kTags,                {"Tags",                -1}},
    {Channel::KDescription,         {"Decription",          -1}},
    {Channel::kSelectPoint,         {"SelectPoint",         -1}},

    {Channel::kBrightness,          {"Brightness",          -1}},
    {Channel::kContrast,            {"Contrast",            -1}},
    {Channel::kExposure,            {"Exposure",            -1}},
    {Channel::kGain,                {"Gain",                -1}},
    {Channel::kWhiteBalance,        {"WhiteBalance",        -1}},
    {Channel::kAutoExposure,        {"AutoExposure",        -1}},
    {Channel::kAutoWhiteBalance,    {"AutoWhiteBalance",    -1}},
    {Channel::kCameraTemperature,   {"CameraTemperature",   -1}},

    {Channel::kRS2_LaserPower,      {"RS2LaserPower",       -1}},
    {Channel::kRS2_MinDistance,     {"RS2MinDistance",      -1}},
    {Channel::kRS2_MaxDistance,     {"RS2MaxDistance",      -1}},
    {Channel::kRS2_InterCamSync,    {"RS2InterCamSync",     -1}},
    {Channel::kRS2_PostSharpening,  {"RS2PostSharpening",   -1}},

    {Channel::kRenderer_CameraType,             {"RenderCameraType",            -1}},
    {Channel::kRenderer_Visualisation,          {"RenderVisualisation",         -1}},
    {Channel::kRenderer_Engine,                 {"RenderEngine",                -1}},
    {Channel::kRenderer_FPS,                    {"RenderFPS",                   -1}},
    {Channel::kRenderer_View,                   {"RenderView",                  -1}},
    {Channel::kRenderer_Channel,                {"RenderChannel",               -1}},
    {Channel::kRenderer_Opacity,                {"RenderOpacity",               -1}},
    {Channel::kRenderer_Sources,                {"RenderSources",               -1}},
    {Channel::kRenderer_Projection,             {"RenderProjection",            -1}},
    {Channel::kRenderer_Background,             {"RenderBackground",            -1}},
    {Channel::kRenderer_ShowBadColour,          {"RenderShowBadColour",         -1}},
    {Channel::kRenderer_CoolEffect,             {"RenderCoolEffect",            -1}},
    {Channel::kRenderer_EffectColour,           {"RenderEffectColour",          -1}},
    {Channel::kRenderer_ShowColourWeights,      {"RenderShowColourWeights",     -1}},
    {Channel::kRenderer_TriangleLimit,          {"RenderTriangleLimit",         -1}},
    {Channel::kRenderer_DisconDisparities,      {"RenderDisconDisparities",     -1}},
    {Channel::kRenderer_NormalWeightColour,     {"RenderNormalWeightColour",    -1}},
    {Channel::kRenderer_ChannelWeights,         {"RenderChannelWeights",        -1}},
    {Channel::kRenderer_AccumFunc,              {"RenderAccumFunc",             -1}},
    {Channel::kRenderer_Lights,                 {"RenderLights",                -1}},
    {Channel::kRenderer_Debug,                  {"RenderDebug",                 -1}},

    {Channel::kOperators,                   {"Operators",               -1}},

    {Channel::kClip_Box,                    {"ClipBox",                 -1}},
    {Channel::kClip_Enabled,                {"ClipEnabled",             -1}},
    {Channel::kClip_Colour,                 {"ClipColour",              -1}},

    {Channel::kFusion_Smoothing,            {"FusionSmoothing",         -1}},
    {Channel::kFusion_Iterations,           {"FusionIterations",        -1}},
    {Channel::kFusion_Carving,              {"FusionCarving",           -1}},
    {Channel::kFusion_ShowChanges,          {"FusionShowChanges",       -1}},

    {Channel::kMLS_DisconPixels,            {"MLSDisconPixels",         -1}},
    {Channel::kMLS_ColourSmoothing,         {"MLSColourSmoothing",      -1}},
    {Channel::kMLS_Iterations,              {"MLSIterations",           -1}},
    {Channel::kMLS_Radius,                  {"MLSRadius",               -1}},
    {Channel::kMLS_WindowSize,              {"MLSWindowSize",           -1}},
    {Channel::kMLS_MergeCorresponding,      {"MLSMergeCorresponding",   -1}},
    {Channel::kMLS_Merge,                   {"MLSMerge",                -1}},
    {Channel::kMLS_ConfidenceCull,          {"MLSConfidenceCull",       -1}},
    {Channel::kMLS_ColourSmooth2,           {"MLSColourSmooth2",        -1}},
    {Channel::kMLS_SpatialSmooth,           {"MLSSpatialSmooth",        -1}},
    {Channel::kMLS_SubPixel,                {"MLSSubPixel",             -1}},
    {Channel::kMLS_P1,                      {"MLSP1",                   -1}},
    {Channel::kMLS_P2,                      {"MLSP2",                   -1}},
    {Channel::kMLS_ShowConsistency,         {"MLSShowConsistency",      -1}},
    {Channel::kMLS_ShowAdjustment,          {"MLSShowAdjustment",       -1}},

    {Channel::kMask_DisconPixels,           {"MaskDisconPixels",        -1}},
    {Channel::kMask_DisconThreshold,        {"MaskDisconThreshold",     -1}},
    {Channel::kMask_NoiseThreshold,         {"MaskNoiseThreshold",      -1}},
    {Channel::kMask_AreaMax,                {"MaskAreaMax",             -1}},
    {Channel::kMask_BorderRect,             {"MaskBorderRectangle",     -1}},
    {Channel::kMask_MaskID,                 {"MaskID",                  -1}},
    {Channel::kMask_Radius,                 {"MaskRadius",              -1}},
    {Channel::kMask_Invert,                 {"MaskInvert",              -1}},

    {Channel::kAruco_Dictionary,            {"ArucoDictionary",         -1}},
    {Channel::kAruco_EstimatePose,          {"ArucoEstimatePose",       -1}},
    {Channel::kAruco_MarkerSize,            {"ArucoMarkerSize",         -1}},

    {Channel::kPoser_Identity,              {"PoserIdentity",           -1}},
    {Channel::kPoser_Locked,                {"PoserLocked",             -1}},
    {Channel::kPoser_Inverse,               {"PoserInverse",            -1}},

    {Channel::kSGMResolution,               {"SGMResolution",           -1}},
    {Channel::kStereoRectify,               {"StereoRectify",           -1}},
    {Channel::kStereoRightPose,             {"StereoRightPose",         -1}},
    {Channel::kStereoRectifyCubic,          {"StereoRectifyCubic",      -1}},
    {Channel::kVideoOffsetZ,                {"VideoOffsetZ",            -1}},
    {Channel::kVideoSize,                   {"VideoSize",               -1}},

    {Channel::kWeights_DisconPixels,        {"WeightsDisconPixels",     -1}},
    {Channel::kWeights_DisconThreshold,     {"WeightsDisconThreshold",  -1}},
    {Channel::kWeights_NoiseThreshold,      {"WeightsNoiseThreshold",   -1}},
    {Channel::kWeights_AreaMax,             {"WeightsAreaMax",          -1}},
    {Channel::kWeights_UseDepth,            {"WeightsUseDepth",         -1}},
    {Channel::kWeights_UseColour,           {"WeightsUseColour",        -1}},
    {Channel::kWeights_UseNoise,            {"WeightsUseNoise",         -1}},
    {Channel::kWeights_UseNormals,          {"WeightsUseNormals",       -1}},

    {Channel::kCross_UseDiscontinuity,      {"CrossUseDiscontinuity",   -1}},
    {Channel::kCross_VMax,                  {"CrossVMax",               -1}},
    {Channel::kCross_HMax,                  {"CrossHMax",               -1}},
    {Channel::kCross_Symmetric,             {"CrossSymmetric",          -1}},
    {Channel::kCross_Tau,                   {"CrossTau",                -1}},

    {Channel::kData,                {"Generic Data",        -1}},
    {Channel::kFaces,               {"Faces",               -1}},
    {Channel::kShapes3D,            {"Shapes 3D",           -1}},
    {Channel::kMessages,            {"Messages",            -1}},
    {Channel::kTouch,               {"Touch",               -1}}
};

static std::unordered_map<std::string, Channel> indexNames() {
    std::unordered_map<std::string, Channel> result;
    for (const auto &i : info) {
        result[std::string(i.second.name)] = i.first;
    }
    return result;
}

static const std::unordered_map<std::string, Channel> nameIndex = indexNames();

std::string ftl::protocol::name(Channel c) {
    if (c == Channel::kNone) return "None";
    auto i = info.find(c);
    if (i != info.end()) {
        return i->second.name;
    } else {
        return "Unknown(" + std::to_string(static_cast<int>(c)) + ")";
    }
}

Channel ftl::protocol::fromName(const std::string &name) {
    auto i = nameIndex.find(name);
    if (i != nameIndex.end()) {
        return i->second;
    } else {
        return Channel::kNone;
    }
}

int ftl::protocol::type(Channel c) {
    if (c == Channel::kNone) return -1;
    auto i = info.find(c);
    if (i != info.end()) {
        return i->second.cvtype;
    } else {
        return -1;
    }
}
