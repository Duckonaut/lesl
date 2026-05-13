#pragma once

namespace lesl {
constexpr const char* CONVENTION_PRIMITIVE_TYPE_KEY = "Primitive";
enum class PipelinePrimitiveInput {
    TriangleList, // DEFAULT
    TriangleStrip,
    LineList,
    LineStrip,
    PointList,
};
constexpr const char* CONVENTION_CULL_MODE_KEY = "CullMode";
enum class PipelineCullMode {
    None, // or Off
    Front,
    Back,
};
constexpr const char* CONVENTION_FILL_MODE_KEY = "FillMode";
enum class PipelineFillMode {
    Fill,
    Line,
};
constexpr const char* CONVENTION_FRONT_FACE_KEY = "FrontFace";
enum class PipelineFrontFace {
    Clockwise,        // or CW
    CounterClockwise, // or CCW
};
constexpr const char* CONVENTION_DEPTH_TEST_KEY = "DepthTest";
constexpr const char* CONVENTION_DEPTH_WRITE_KEY = "DepthWrite";
constexpr const char* CONVENTION_STENCIL_TEST_KEY = "StencilTest";
constexpr const char* CONVENTION_DEPTH_OP_KEY = "DepthOp";
enum class PipelineCompareOp {
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    GreaterEqual,
    NotEqual,
    Always,
};
constexpr const char* CONVENTION_STENCIL_COMPARE_MASK_KEY = "StencilCompareMask";
constexpr const char* CONVENTION_STENCIL_WRITE_MASK_KEY = "StencilWriteMask";
constexpr const char* CONVENTION_STENCIL_FRONT_OP = "StencilCompareOp";
constexpr const char* CONVENTION_STENCIL_BACK_OP = "StencilCompareOpBack";
constexpr const char* CONVENTION_STENCIL_PASS_FRONT_OP = "StencilPassOp";
constexpr const char* CONVENTION_STENCIL_PASS_BACK_OP = "StencilPassOpBack";
constexpr const char* CONVENTION_STENCIL_FAIL_FRONT_OP = "StencilFailOp";
constexpr const char* CONVENTION_STENCIL_FAIL_BACK_OP = "StencilFailOpBack";
constexpr const char* CONVENTION_STENCIL_DEPTH_FAIL_FRONT_OP = "StencilDepthFailOp";
constexpr const char* CONVENTION_STENCIL_DEPTH_FAIL_BACK_OP = "StencilDepthFailOpBack";
enum class PipelineStencilOp {
    Keep,
    Zero,
    Replace,
    IncrementAndClamp,
    DecrementAndClamp,
    Invert,
    IncrementAndWrap,
    DecrementAndWrap,
};
constexpr const char* CONVENTION_MSAA_SAMPLE_COUNT = "MSAASampleCount";
constexpr const char* CONVENTION_MSAA_ENABLE_MASK = "MSAAEnableMask";
constexpr const char* CONVENTION_MSAA_SAMPLE_MASK = "MSAASampleMask";
constexpr const char* CONVENTION_MSAA_ALPHA_TO_COVERAGE = "MSAAAlphaToCoverage";

constexpr const char* CONVENTION_BLEND = "Blend";
constexpr const char* CONVENTION_BLEND_OP = "BlendOp";
constexpr const char* CONVENTION_BLEND_ALPHA_OP = "BlendAlphaOp";
enum class PipelineBlendOp {
    Add,
    Subtract,
    Min,
    Max,
    ReverseSubstract,
};
constexpr const char* CONVENTION_BLEND_FACTOR_SRC_COLOR = "BlendSrcColor";
constexpr const char* CONVENTION_BLEND_FACTOR_SRC_ALPHA = "BlendSrcAlpha";
constexpr const char* CONVENTION_BLEND_FACTOR_DST_COLOR = "BlendDstColor";
constexpr const char* CONVENTION_BLEND_FACTOR_DST_ALPHA = "BlendDstAlpha";
enum class PipelineBlendFactor {
    One,
    Zero,
    SrcAlpha,
    DstAlpha,
    SrcColor,
    DstColor,
    ConstColor,
    OneMinusSrcAlpha,
    OneMinusDstAlpha,
    OneMinusSrcColor,
    OneMinusDstColor,
    OneMinusConstColor,
};

}; // namespace lesl
