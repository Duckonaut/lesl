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
        Clockwise, // or CW
        CounterClockwise, // or CCW
    };
};
