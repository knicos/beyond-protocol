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
    /* Video Channels */
    kNone           = -1,
    kColour         = 0,    // 8UC3 or 8UC4
    kLeft           = 0,
    kDepth          = 1,    // 32S or 32F
    kRight          = 2,    // 8UC3 or 8UC4
    kColour2        = 2,
    kDepth2         = 3,
    kDeviation      = 4,
    kScreen         = 4,    // 16SC2
    kNormals        = 5,    // 16FC4
    kWeights        = 6,    // short
    kConfidence     = 7,    // 32F
    kContribution   = 7,    // 32F
    kEnergyVector   = 8,    // 32FC4
    kFlow           = 9,    // 16SC2
    kFlow2          = 10,   // 16SC2
    kEnergy         = 10,   // 32F
    kMask           = 11,   // 32U
    kDensity        = 12,   // 32F
    kSupport1       = 13,   // 8UC4 (currently)
    kSupport2       = 14,   // 8UC4 (currently)
    kSegmentation   = 15,   // 32S?
    kNormals2       = 16,   // 16FC4
    kUNUSED1        = 17,
    kDisparity      = 18,
    kSmoothing      = 19,   // 32F
    kUNUSED2        = 20,
    kOverlay        = 21,   // 8UC4
    kGroundTruth    = 22,   // 32F

    /* Audio Channels */
    kAudioMono      = 32,   // Deprecated, will always be stereo
    kAudioStereo    = 33,
    kAudio          = 33,

    /* Special data channels */
    kConfiguration    = 64,   // JSON Data
    kSettings1        = 65,
    kCalibration      = 65,   // Camera Parameters Object
    kPose             = 66,   // Eigen::Matrix4d, camera transform
    kSettings2        = 67,
    kCalibration2     = 67,   // Right camera parameters
    kIndex            = 68,
    kControl          = 69,   // For stream and encoder control
    kSettings3        = 70,
    kMetaData         = 71,   // Map of string pairs (key, value)
    kCapabilities     = 72,   // Unordered set of int capabilities
    kCalibrationData  = 73,  // Just for stereo intrinsics/extrinsics etc
    kThumbnail        = 74,   // Small JPG thumbnail, sometimes updated
    kOverlaySelect    = 75,   // Choose what to have in the overlay channel
    kStartTime        = 76,   // Stream start timestamp
    kUser             = 77,   // User currently controlling the stream

    kAccelerometer    = 90,   // Eigen::Vector3f
    kGyroscope        = 91,   // Eigen::Vector3f

    /* Camera Options */
    kBrightness          = 100,
    kContrast            = 101,
    kExposure            = 102,
    kGain                = 103,
    kWhiteBalance        = 104,
    kAutoExposure        = 105,
    kAutoWhiteBalance    = 106,
    kCameraTemperature   = 107,

    /* Realsense Options */
    kRS2_LaserPower       = 150,
    kRS2_MinDistance      = 151,
    kRS2_MaxDistance      = 152,
    kRS2_InterCamSync     = 153,
    kRS2_PostSharpening   = 154,

    /* Pylon Options 200 */

    /* Audio Settings 300 */

    /* Renderer Settings 400 */
    kRenderer_CameraType          = 400,  // stereo, normal, tile
    kRenderer_Visualisation       = 401,  // Pointcloud, mesh, other
    kRenderer_Engine              = 402,  // OpenGL, CUDA, other
    kRenderer_FPS                 = 403,  // Frames per second
    kRenderer_View                = 404,  // Fixed viewpoint to one source
    kRenderer_Channel             = 405,  // Select overlay channel,
    kRenderer_Opacity             = 406,  // Opacity of overlay channel
    kRenderer_Sources             = 407,  // Which source devices to use
    kRenderer_Projection          = 408,  // 0 = normal, 1 = ortho, 2 = equirect
    kRenderer_Background          = 409,  // Background colour
    kRenderer_ShowBadColour       = 420,
    kRenderer_CoolEffect          = 421,
    kRenderer_EffectColour        = 422,
    kRenderer_ShowColourWeights   = 423,
    kRenderer_TriangleLimit       = 424,
    kRenderer_DisconDisparities   = 425,
    kRenderer_NormalWeightColour  = 426,
    kRenderer_ChannelWeights      = 427,
    kRenderer_AccumFunc           = 428,

    /* Pipeline Settings */
    kPipeline_Enable          = 500,
    kPipeline_EnableMVMLS     = 501,
    kPipeline_EnableAruco     = 502,

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
