/**
 * @file channels.hpp
 * @copyright Copyright (c) 2020 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <bitset>

namespace ftl {
namespace protocol {

/** Frame channel identifier. */
enum struct Channel : int {
    /* Meta Channels */
    kMultiData      = -7,   /// Pack many channels into a single packet
    kChannelMeta    = -6,   /// Codec information
    kFrameStart     = -5,   /// Timestamp and frame meta data
    kFrameEnd       = -4,   /// Expected packet counts, statistics
    kRequest        = -3,   /// Frame and channel requests
    kStreamMeta     = -2,   /// Name, description etc.
    kNone           = -1,

    /* Video Channels */
    kColour         = 0,    /// Left-eye colour video
    kLeft           = 0,
    kDepth          = 1,    /// Left-eye depth
    kRight          = 2,    /// Right-eye colour video
    kColour2        = 2,
    kDepth2         = 3,    /// Right-eye depth
    kDeviation      = 4,
    kScreen         = 4,
    kNormals        = 5,    /// Normals for left-eye
    kWeights        = 6,    /// Per pixel weighting
    kConfidence     = 7,    /// Depth or disparity confidence
    kContribution   = 7,
    kEnergyVector   = 8,
    kFlow           = 9,    /// Optical flow
    kFlow2          = 10,   /// Right optical flow
    kEnergy         = 10,   /// Some energy measure
    kMask           = 11,   /// Pixel masking
    kDensity        = 12,   /// Point density
    kSupport1       = 13,   /// Cross support coordinates
    kSupport2       = 14,
    kSegmentation   = 15,   /// Segment identifiers
    kNormals2       = 16,   /// Right-eye normals
    kUNUSED1        = 17,
    kDisparity      = 18,   /// Original disparity
    kSmoothing      = 19,   /// Smoothing magnitude
    kUNUSED2        = 20,
    kOverlay        = 21,   /// Colour overlay image
    kGroundTruth    = 22,   /// Ground truth

    /* Audio Channels */
    kAudioMono      = 32,
    kAudioStereo    = 33,
    kAudio          = 33,   /// Stereo audio data

    /* Special data channels */
    kConfiguration    = 64,   /// Device URI list
    kSettings1        = 65,
    kCalibration      = 65,   /// Camera Parameters Object
    kPose             = 66,   /// Eigen::Matrix4d, camera transform
    kSettings2        = 67,
    kCalibration2     = 67,   /// Right camera parameters
    kIndex            = 68,
    kControl          = 69,   /// For stream and encoder control
    kSettings3        = 70,
    kMetaData         = 71,   /// Map of string pairs (key, value)
    kCapabilities     = 72,   /// Unordered set of int capabilities
    kCalibrationData  = 73,   /// Just for stereo intrinsics/extrinsics etc
    kThumbnail        = 74,   /// Small JPG thumbnail, sometimes updated
    kOverlaySelect    = 75,   /// Choose what to have in the overlay channel
    kStartTime        = 76,   /// Stream start timestamp
    kUser             = 77,   /// User currently controlling the stream
    kName             = 78,   /// Alternative to meta data channel
    kTags             = 79,   /// Array of string tag names
    KDescription      = 80,
    kSelectPoint      = 81,   /// A selected X,Y screen point
    kStereoPose       = 82,   /// A pair of poses for stereo rendering

    kAccelerometer    = 90,   /// Eigen::Vector3f
    kGyroscope        = 91,   /// Eigen::Vector3f

    /* Camera Options */
    kBrightness          = 100,     /// Camera brightness setting
    kContrast            = 101,     /// Camera contrast setting
    kExposure            = 102,     /// Camera exposure setting
    kGain                = 103,     /// Camera gain setting
    kWhiteBalance        = 104,     /// Camera white balance setting
    kAutoExposure        = 105,     /// Camera auto exposure enabled
    kAutoWhiteBalance    = 106,     /// Camera auto white balance enabled
    kCameraTemperature   = 107,     /// Current camera temperature reading

    /* Realsense Options */
    kRS2_LaserPower       = 150,    /// RealSense laser power
    kRS2_MinDistance      = 151,    /// RealSense minimum depth
    kRS2_MaxDistance      = 152,    /// RealSense maximum depth
    kRS2_InterCamSync     = 153,    /// RealSense inter-camera sync mode
    kRS2_PostSharpening   = 154,    /// RealSense post sharpening filter

    /* Pylon Options 200 */

    /* Audio Settings 300 */

    /* Renderer Settings 400 */
    kRenderer_CameraType            = 400,  // stereo, normal, tile
    kRenderer_Visualisation         = 401,  // Pointcloud, mesh, other
    kRenderer_Engine                = 402,  // OpenGL, CUDA, other
    kRenderer_FPS                   = 403,  // Frames per second
    kRenderer_View                  = 404,  // Fixed viewpoint to one source
    kRenderer_Channel               = 405,  // Select overlay channel,
    kRenderer_Opacity               = 406,  // Opacity of overlay channel
    kRenderer_Sources               = 407,  // Which source devices to use
    kRenderer_Projection            = 408,  // 0 = normal, 1 = ortho, 2 = equirect
    kRenderer_Background            = 409,  // Background colour
    kRenderer_ShowBadColour         = 420,
    kRenderer_CoolEffect            = 421,
    kRenderer_EffectColour          = 422,
    kRenderer_ShowColourWeights     = 423,
    kRenderer_TriangleLimit         = 424,
    kRenderer_DisconDisparities     = 425,
    kRenderer_NormalWeightColour    = 426,
    kRenderer_ChannelWeights        = 427,
    kRenderer_AccumFunc             = 428,
    kRenderer_Lights                = 429,
    kRenderer_Debug                 = 430,  // Depends on the renderer engine

    /* Pipeline Settings */
    kOperators                      = 500,

    /* Clipping */
    kClip_Box                       = 510,
    kClip_Enabled                   = 511,
    kClip_Colour                    = 512,

    /* Fusion operator */
    kFusion_Smoothing               = 520,
    kFusion_Iterations              = 521,
    kFusion_Carving                 = 522,
    kFusion_ShowChanges             = 523,

    /* MVMLS Operator */
    kMLS_DisconPixels               = 540,
    kMLS_ColourSmoothing            = 541,
    kMLS_Iterations                 = 542,
    kMLS_Radius                     = 543,
    kMLS_WindowSize                 = 544,
    kMLS_MergeCorresponding         = 545,
    kMLS_Merge                      = 546,
    kMLS_ConfidenceCull             = 547,
    kMLS_ColourSmooth2              = 548,
    kMLS_SpatialSmooth              = 549,
    kMLS_SubPixel                   = 550,
    kMLS_P1                         = 551,
    kMLS_P2                         = 552,
    kMLS_ShowConsistency            = 553,
    kMLS_ShowAdjustment             = 554,

    /* Masking operator */
    kMask_DisconPixels              = 560,
    kMask_DisconThreshold           = 561,
    kMask_NoiseThreshold            = 562,
    kMask_AreaMax                   = 563,
    kMask_BorderRect                = 564,
    kMask_MaskID                    = 565,
    kMask_Radius                    = 566,
    kMask_Invert                    = 570,

    /* Aruco Operator */
    kAruco_Dictionary               = 580,
    kAruco_EstimatePose             = 581,
    kAruco_MarkerSize               = 582,

    /* Poser operator */
    kPoser_Identity                 = 590,
    kPoser_Locked                   = 591,
    kPoser_Inverse                  = 592,

    /* Stereo video settings */
    kSGMResolution                  = 600,
    kStereoRectify                  = 601,
    kStereoRightPose                = 602,
    kStereoRectifyCubic             = 603,
    kVideoOffsetZ                   = 604,
    kVideoSize                      = 605,

    /* Pixel weights operator */
    kWeights_DisconPixels           = 610,
    kWeights_DisconThreshold        = 611,
    kWeights_NoiseThreshold         = 612,
    kWeights_AreaMax                = 613,
    kWeights_UseDepth               = 614,
    kWeights_UseColour              = 615,
    kWeights_UseNoise               = 616,
    kWeights_UseNormals             = 617,

    /* Cross support operator */
    kCross_UseDiscontinuity         = 620,
    kCross_VMax                     = 621,
    kCross_HMax                     = 622,
    kCross_Symmetric                = 623,
    kCross_Tau                      = 624,


    /* Custom / user data channels */
    kData           = 2048,  // Do not use
    kEndFrame       = 2048,  // Signify the last packet
    kFaces          = 2049,  // Data about detected faces
    kTransforms     = 2050,  // Transformation matrices for framesets
    kShapes3D       = 2051,  // Labeled 3D shapes
    kMessages       = 2052,  // Vector of Strings
    kTouch          = 2053,  // List of touch data type (each touch point)
    kPipelines      = 2054,  // List of pipline URIs that have been applied
};

}  // namespace protocol
}  // namespace ftl
