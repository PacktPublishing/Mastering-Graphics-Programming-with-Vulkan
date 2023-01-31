#pragma once

#include "foundation/platform.hpp"

#if !defined(_MSC_VER)
// NOTE(marco): avoid conflicts with X header...
#ifdef True
#undef True
#endif
#ifdef False
#undef False
#endif
#ifdef Always
#undef Always
#endif
#ifdef None
#undef None
#endif
#endif

namespace raptor {

// !!! WARNING !!!
// THIS CODE IS GENERATED WITH HYDRA DATA FORMAT CODE GENERATOR.

/////////////////////////////////////////////////////////////////////////////////

namespace Blend {
    enum Enum {
        Zero, One, SrcColor, InvSrcColor, SrcAlpha, InvSrcAlpha, DestAlpha, InvDestAlpha, DestColor, InvDestColor, SrcAlphasat, Src1Color, InvSrc1Color, Src1Alpha, InvSrc1Alpha, Count
    };

    static const char* s_value_names[] = {
        "Zero", "One", "SrcColor", "InvSrcColor", "SrcAlpha", "InvSrcAlpha", "DestAlpha", "InvDestAlpha", "DestColor", "InvDestColor", "SrcAlphaSat", "Src1Color", "InvSrc1Color", "Src1Alpha", "InvSrc1Alpha", "Count"
    };

    static const char* ToString( Enum e ) {
        return ((u32)e < Enum::Count ? s_value_names[(int)e] : "unsupported" );
    }
} // namespace Blend

namespace BlendOperation {
    enum Enum {
        Add, Subtract, RevSubtract, Min, Max, Count
    };

    enum Mask {
        Add_mask = 1 << 0, Subtract_mask = 1 << 1, RevSubtract_mask = 1 << 2, Min_mask = 1 << 3, Max_mask = 1 << 4, Count_mask = 1 << 5
    };

    static const char* s_value_names[] = {
        "Add", "Subtract", "RevSubtract", "Min", "Max", "Count"
    };

    static const char* ToString( Enum e ) {
        return ((u32)e < Enum::Count ? s_value_names[(int)e] : "unsupported" );
    }
} // namespace BlendOperation

namespace ColorWriteEnabled {
    enum Enum {
        Red, Green, Blue, Alpha, All, Count
    };

    enum Mask {
        Red_mask = 1 << 0, Green_mask = 1 << 1, Blue_mask = 1 << 2, Alpha_mask = 1 << 3, All_mask = Red_mask | Green_mask | Blue_mask | Alpha_mask
    };

    static const char* s_value_names[] = {
        "Red", "Green", "Blue", "Alpha", "All", "Count"
    };

    static const char* ToString( Enum e ) {
        return ((u32)e < Enum::Count ? s_value_names[(int)e] : "unsupported" );
    }
} // namespace ColorWriteEnabled

namespace ComparisonFunction {
    enum Enum {
        Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always, Count
    };

    enum Mask {
        Never_mask = 1 << 0, Less_mask = 1 << 1, Equal_mask = 1 << 2, LessEqual_mask = 1 << 3, Greater_mask = 1 << 4, NotEqual_mask = 1 << 5, GreaterEqual_mask = 1 << 6, Always_mask = 1 << 7, Count_mask = 1 << 8
    };

    static const char* s_value_names[] = {
        "Never", "Less", "Equal", "LessEqual", "Greater", "NotEqual", "GreaterEqual", "Always", "Count"
    };

    static const char* ToString( Enum e ) {
        return ((u32)e < Enum::Count ? s_value_names[(int)e] : "unsupported" );
    }
} // namespace ComparisonFunction

namespace CullMode {
    enum Enum {
        None, Front, Back, Count
    };

    enum Mask {
        None_mask = 1 << 0, Front_mask = 1 << 1, Back_mask = 1 << 2, Count_mask = 1 << 3
    };

    static const char* s_value_names[] = {
        "None", "Front", "Back", "Count"
    };

    static const char* ToString( Enum e ) {
        return ((u32)e < Enum::Count ? s_value_names[(int)e] : "unsupported" );
    }
} // namespace CullMode

namespace DepthWriteMask {
    enum Enum {
        Zero, All, Count
    };

    enum Mask {
        Zero_mask = 1 << 0, All_mask = 1 << 1, Count_mask = 1 << 2
    };

    static const char* s_value_names[] = {
        "Zero", "All", "Count"
    };

    static const char* ToString( Enum e ) {
        return ((u32)e < Enum::Count ? s_value_names[(int)e] : "unsupported" );
    }
} // namespace DepthWriteMask

namespace FillMode {
    enum Enum {
        Wireframe, Solid, Point, Count
    };

    enum Mask {
        Wireframe_mask = 1 << 0, Solid_mask = 1 << 1, Point_mask = 1 << 2, Count_mask = 1 << 3
    };

    static const char* s_value_names[] = {
        "Wireframe", "Solid", "Point", "Count"
    };

    static const char* ToString( Enum e ) {
        return ((u32)e < Enum::Count ? s_value_names[(int)e] : "unsupported" );
    }
} // namespace FillMode

namespace FrontClockwise {
    enum Enum {
        True, False, Count
    };

    enum Mask {
        True_mask = 1 << 0, False_mask = 1 << 1, Count_mask = 1 << 2
    };

    static const char* s_value_names[] = {
        "True", "False", "Count"
    };

    static const char* ToString( Enum e ) {
        return ((u32)e < Enum::Count ? s_value_names[(int)e] : "unsupported" );
    }
} // namespace FrontClockwise

namespace StencilOperation {
    enum Enum {
        Keep, Zero, Replace, IncrSat, DecrSat, Invert, Incr, Decr, Count
    };

    enum Mask {
        Keep_mask = 1 << 0, Zero_mask = 1 << 1, Replace_mask = 1 << 2, IncrSat_mask = 1 << 3, DecrSat_mask = 1 << 4, Invert_mask = 1 << 5, Incr_mask = 1 << 6, Decr_mask = 1 << 7, Count_mask = 1 << 8
    };

    static const char* s_value_names[] = {
        "Keep", "Zero", "Replace", "IncrSat", "DecrSat", "Invert", "Incr", "Decr", "Count"
    };

    static const char* ToString( Enum e ) {
        return ((u32)e < Enum::Count ? s_value_names[(int)e] : "unsupported" );
    }
} // namespace StencilOperation

namespace TextureFormat {
    enum Enum {
        UNKNOWN, R32G32B32A32_TYPELESS, R32G32B32A32_FLOAT, R32G32B32A32_UINT, R32G32B32A32_SINT, R32G32B32_TYPELESS, R32G32B32_FLOAT, R32G32B32_UINT, R32G32B32_SINT, R16G16B16A16_TYPELESS, R16G16B16A16_FLOAT, R16G16B16A16_UNORM, R16G16B16A16_UINT, R16G16B16A16_SNORM, R16G16B16A16_SINT, R32G32_TYPELESS, R32G32_FLOAT, R32G32_UINT, R32G32_SINT, R10G10B10A2_TYPELESS, R10G10B10A2_UNORM, R10G10B10A2_UINT, R11G11B10_FLOAT, R8G8B8A8_TYPELESS, R8G8B8A8_UNORM, R8G8B8A8_UNORM_SRGB, R8G8B8A8_UINT, R8G8B8A8_SNORM, R8G8B8A8_SINT, R16G16_TYPELESS, R16G16_FLOAT, R16G16_UNORM, R16G16_UINT, R16G16_SNORM, R16G16_SINT, R32_TYPELESS, R32_FLOAT, R32_UINT, R32_SINT, R8G8_TYPELESS, R8G8_UNORM, R8G8_UINT, R8G8_SNORM, R8G8_SINT, R16_TYPELESS, R16_FLOAT, R16_UNORM, R16_UINT, R16_SNORM, R16_SINT, R8_TYPELESS, R8_UNORM, R8_UINT, R8_SNORM, R8_SINT, R9G9B9E5_SHAREDEXP, D32_FLOAT_S8X24_UINT, D24_UNORM_S8_UINT, D32_FLOAT, D24_UNORM_X8_UINT, D16_UNORM, S8_UINT, BC1_TYPELESS, BC1_UNORM, BC1_UNORM_SRGB, BC2_TYPELESS, BC2_UNORM, BC2_UNORM_SRGB, BC3_TYPELESS, BC3_UNORM, BC3_UNORM_SRGB, BC4_TYPELESS, BC4_UNORM, BC4_SNORM, BC5_TYPELESS, BC5_UNORM, BC5_SNORM, B5G6R5_UNORM, B5G5R5A1_UNORM, B8G8R8A8_UNORM, B8G8R8X8_UNORM, R10G10B10_XR_BIAS_A2_UNORM, B8G8R8A8_TYPELESS, B8G8R8A8_UNORM_SRGB, B8G8R8X8_TYPELESS, B8G8R8X8_UNORM_SRGB, BC6H_TYPELESS, BC6H_UF16, BC6H_SF16, BC7_TYPELESS, BC7_UNORM, BC7_UNORM_SRGB, FORCE_UINT, Count
    };

    static const char* s_value_names[] = {
        "UNKNOWN", "R32G32B32A32_TYPELESS", "R32G32B32A32_FLOAT", "R32G32B32A32_UINT", "R32G32B32A32_SINT", "R32G32B32_TYPELESS", "R32G32B32_FLOAT", "R32G32B32_UINT", "R32G32B32_SINT", "R16G16B16A16_TYPELESS", "R16G16B16A16_FLOAT", "R16G16B16A16_UNORM", "R16G16B16A16_UINT", "R16G16B16A16_SNORM", "R16G16B16A16_SINT", "R32G32_TYPELESS", "R32G32_FLOAT", "R32G32_UINT", "R32G32_SINT", "R10G10B10A2_TYPELESS", "R10G10B10A2_UNORM", "R10G10B10A2_UINT", "R11G11B10_FLOAT", "R8G8B8A8_TYPELESS", "R8G8B8A8_UNORM", "R8G8B8A8_UNORM_SRGB", "R8G8B8A8_UINT", "R8G8B8A8_SNORM", "R8G8B8A8_SINT", "R16G16_TYPELESS", "R16G16_FLOAT", "R16G16_UNORM", "R16G16_UINT", "R16G16_SNORM", "R16G16_SINT", "R32_TYPELESS", "R32_FLOAT", "R32_UINT", "R32_SINT", "R8G8_TYPELESS", "R8G8_UNORM", "R8G8_UINT", "R8G8_SNORM", "R8G8_SINT", "R16_TYPELESS", "R16_FLOAT", "R16_UNORM", "R16_UINT", "R16_SNORM", "R16_SINT", "R8_TYPELESS", "R8_UNORM", "R8_UINT", "R8_SNORM", "R8_SINT", "R9G9B9E5_SHAREDEXP", "D32_FLOAT_S8X24_UINT", "D24_UNORM_S8_UINT", "D32_FLOAT", "D24_UNORM_X8_UINT", "D16_UNORM", "S8_UINT", "BC1_TYPELESS", "BC1_UNORM", "BC1_UNORM_SRGB", "BC2_TYPELESS", "BC2_UNORM", "BC2_UNORM_SRGB", "BC3_TYPELESS", "BC3_UNORM", "BC3_UNORM_SRGB", "BC4_TYPELESS", "BC4_UNORM", "BC4_SNORM", "BC5_TYPELESS", "BC5_UNORM", "BC5_SNORM", "B5G6R5_UNORM", "B5G5R5A1_UNORM", "B8G8R8A8_UNORM", "B8G8R8X8_UNORM", "R10G10B10_XR_BIAS_A2_UNORM", "B8G8R8A8_TYPELESS", "B8G8R8A8_UNORM_SRGB", "B8G8R8X8_TYPELESS", "B8G8R8X8_UNORM_SRGB", "BC6H_TYPELESS", "BC6H_UF16", "BC6H_SF16", "BC7_TYPELESS", "BC7_UNORM", "BC7_UNORM_SRGB", "FORCE_UINT", "Count"
    };

    static const char* ToString( Enum e ) {
        return ((u32)e < Enum::Count ? s_value_names[(int)e] : "unsupported" );
    }
} // namespace TextureFormat

namespace TopologyType {
    enum Enum {
        Unknown, Point, Line, Triangle, Patch, Count
    };

    enum Mask {
        Unknown_mask = 1 << 0, Point_mask = 1 << 1, Line_mask = 1 << 2, Triangle_mask = 1 << 3, Patch_mask = 1 << 4, Count_mask = 1 << 5
    };

    static const char* s_value_names[] = {
        "Unknown", "Point", "Line", "Triangle", "Patch", "Count"
    };

    static const char* ToString( Enum e ) {
        return ((u32)e < Enum::Count ? s_value_names[(int)e] : "unsupported" );
    }
} // namespace TopologyType

namespace BufferType {
    enum Enum {
        Vertex, Index, Constant, Indirect, Structured, Count
    };

    enum Mask {
        Vertex_mask = 1 << 0, Index_mask = 1 << 1, Constant_mask = 1 << 2, Indirect_mask = 1 << 3, Structured_mask = 1 << 4, Count_mask = 1 << 5
    };

    static const char* s_value_names[] = {
        "Vertex", "Index", "Constant", "Indirect", "Structured", "Count"
    };

    static const char* ToString( Enum e ) {
        return ((u32)e < Enum::Count ? s_value_names[(int)e] : "unsupported" );
    }
} // namespace BufferType

namespace ResourceUsageType {
    enum Enum {
        Immutable, Dynamic, Stream, Staging, Readback, Count
    };

    enum Mask {
        Immutable_mask = 1 << 0, Dynamic_mask = 1 << 1, Stream_mask = 1 << 2, Staging_mask = 1 << 3, Readback_mask = 1 << 4, Count_mask = 1 << 5
    };

    static const char* s_value_names[] = {
        "Immutable", "Dynamic", "Stream", "Staging", "Count"
    };

    static const char* ToString( Enum e ) {
        return ((u32)e < Enum::Count ? s_value_names[(int)e] : "unsupported" );
    }
} // namespace ResourceUsageType

namespace IndexType {
    enum Enum {
        Uint16, Uint32, Count
    };

    enum Mask {
        Uint16_mask = 1 << 0, Uint32_mask = 1 << 1, Count_mask = 1 << 2
    };

    static const char* s_value_names[] = {
        "Uint16", "Uint32", "Count"
    };

    static const char* ToString( Enum e ) {
        return ((u32)e < Enum::Count ? s_value_names[(int)e] : "unsupported" );
    }
} // namespace IndexType

namespace TextureType {
    enum Enum {
        Texture1D, Texture2D, Texture3D, TextureCube, Texture_1D_Array, Texture_2D_Array, Texture_Cube_Array, Count
    };

    enum Mask {
        Texture1D_mask = 1 << 0, Texture2D_mask = 1 << 1, Texture3D_mask = 1 << 2, TextureCube_mask = 1 << 3, Texture_1D_Array_mask = 1 << 4, Texture_2D_Array_mask = 1 << 5, Texture_Cube_Array_mask = 1 << 6, Count_mask = 1 << 7
    };

    static const char* s_value_names[] = {
        "Texture1D", "Texture2D", "Texture3D", "TextureCube", "Texture_1D_Array", "Texture_2D_Array", "Texture_Cube_Array", "Count"
    };

    static const char* ToString( Enum e ) {
        return ((u32)e < Enum::Count ? s_value_names[(int)e] : "unsupported" );
    }
} // namespace TextureType

//namespace ShaderStage {
//    enum Enum {
//        Vertex, Fragment, Geometry, Compute, Hull, Domain, Count
//    };
//
//    enum Mask {
//        Vertex_mask = 1 << 0, Fragment_mask = 1 << 1, Geometry_mask = 1 << 2, Compute_mask = 1 << 3, Hull_mask = 1 << 4, Domain_mask = 1 << 5, Count_mask = 1 << 6
//    };
//
//    static const char* s_value_names[] = {
//        "Vertex", "Fragment", "Geometry", "Compute", "Hull", "Domain", "Count"
//    };
//
//    static const char* ToString( Enum e ) {
//        return ((u32)e < Enum::Count ? s_value_names[(int)e] : "unsupported" );
//    }
//} // namespace ShaderStage

namespace TextureFilter {
    enum Enum {
        Nearest, Linear, Count
    };

    enum Mask {
        Nearest_mask = 1 << 0, Linear_mask = 1 << 1, Count_mask = 1 << 2
    };

    static const char* s_value_names[] = {
        "Nearest", "Linear", "Count"
    };

    static const char* ToString( Enum e ) {
        return ((u32)e < Enum::Count ? s_value_names[(int)e] : "unsupported" );
    }
} // namespace TextureFilter

namespace TextureMipFilter {
    enum Enum {
        Nearest, Linear, Count
    };

    enum Mask {
        Nearest_mask = 1 << 0, Linear_mask = 1 << 1, Count_mask = 1 << 2
    };

    static const char* s_value_names[] = {
        "Nearest", "Linear", "Count"
    };

    static const char* ToString( Enum e ) {
        return ((u32)e < Enum::Count ? s_value_names[(int)e] : "unsupported" );
    }
} // namespace TextureMipFilter

namespace TextureAddressMode {
    enum Enum {
        Repeat, Mirrored_Repeat, Clamp_Edge, Clamp_Border, Count
    };

    enum Mask {
        Repeat_mask = 1 << 0, Mirrored_Repeat_mask = 1 << 1, Clamp_Edge_mask = 1 << 2, Clamp_Border_mask = 1 << 3, Count_mask = 1 << 4
    };

    static const char* s_value_names[] = {
        "Repeat", "Mirrored_Repeat", "Clamp_Edge", "Clamp_Border", "Count"
    };

    static const char* ToString( Enum e ) {
        return ((u32)e < Enum::Count ? s_value_names[(int)e] : "unsupported" );
    }
} // namespace TextureAddressMode

namespace VertexComponentFormat {
    enum Enum {
        Float, Float2, Float3, Float4, Mat4, Byte, Byte4N, UByte, UByte4N, Short2, Short2N, Short4, Short4N, Uint, Uint2, Uint4, Count
    };

    static const char* s_value_names[] = {
        "Float", "Float2", "Float3", "Float4", "Mat4", "Byte", "Byte4N", "UByte", "UByte4N", "Short2", "Short2N", "Short4", "Short4N", "Uint", "Uint2", "Uint4", "Count"
    };

    static const char* ToString( Enum e ) {
        return ((u32)e < Enum::Count ? s_value_names[(int)e] : "unsupported" );
    }
} // namespace VertexComponentFormat

namespace VertexInputRate {
    enum Enum {
        PerVertex, PerInstance, Count
    };

    enum Mask {
        PerVertex_mask = 1 << 0, PerInstance_mask = 1 << 1, Count_mask = 1 << 2
    };

    static const char* s_value_names[] = {
        "PerVertex", "PerInstance", "Count"
    };

    static const char* ToString( Enum e ) {
        return ((u32)e < Enum::Count ? s_value_names[(int)e] : "unsupported" );
    }
} // namespace VertexInputRate

namespace LogicOperation {
    enum Enum {
        Clear, Set, Copy, CopyInverted, Noop, Invert, And, Nand, Or, Nor, Xor, Equiv, AndReverse, AndInverted, OrReverse, OrInverted, Count
    };

    enum Mask {
        Clear_mask = 1 << 0, Set_mask = 1 << 1, Copy_mask = 1 << 2, CopyInverted_mask = 1 << 3, Noop_mask = 1 << 4, Invert_mask = 1 << 5, And_mask = 1 << 6, Nand_mask = 1 << 7, Or_mask = 1 << 8, Nor_mask = 1 << 9, Xor_mask = 1 << 10, Equiv_mask = 1 << 11, AndReverse_mask = 1 << 12, AndInverted_mask = 1 << 13, OrReverse_mask = 1 << 14, OrInverted_mask = 1 << 15, Count_mask = 1 << 16
    };

    static const char* s_value_names[] = {
        "Clear", "Set", "Copy", "CopyInverted", "Noop", "Invert", "And", "Nand", "Or", "Nor", "Xor", "Equiv", "AndReverse", "AndInverted", "OrReverse", "OrInverted", "Count"
    };

    static const char* ToString( Enum e ) {
        return ((u32)e < Enum::Count ? s_value_names[(int)e] : "unsupported" );
    }
} // namespace LogicOperation

namespace QueueType {
    enum Enum {
        Graphics, Compute, CopyTransfer, Count
    };

    enum Mask {
        Graphics_mask = 1 << 0, Compute_mask = 1 << 1, CopyTransfer_mask = 1 << 2, Count_mask = 1 << 3
    };

    static const char* s_value_names[] = {
        "Graphics", "Compute", "CopyTransfer", "Count"
    };

    static const char* ToString( Enum e ) {
        return ((u32)e < Enum::Count ? s_value_names[(int)e] : "unsupported" );
    }
} // namespace QueueType

namespace CommandType {
    enum Enum {
        BindPipeline, BindResourceTable, BindVertexBuffer, BindIndexBuffer, BindResourceSet, Draw, DrawIndexed, DrawInstanced, DrawIndexedInstanced, Dispatch, CopyResource, SetScissor, SetViewport, Clear, ClearDepth, ClearStencil, BeginPass, EndPass, Count
    };

    static const char* s_value_names[] = {
        "BindPipeline", "BindResourceTable", "BindVertexBuffer", "BindIndexBuffer", "BindResourceSet", "Draw", "DrawIndexed", "DrawInstanced", "DrawIndexedInstanced", "Dispatch", "CopyResource", "SetScissor", "SetViewport", "Clear", "ClearDepth", "ClearStencil", "BeginPass", "EndPass", "Count"
    };

    static const char* ToString( Enum e ) {
        return ((u32)e < Enum::Count ? s_value_names[(int)e] : "unsupported" );
    }
} // namespace CommandType

// Manually typed enums /////////////////////////////////////////////////////////

enum DeviceExtensions {
    DeviceExtensions_DebugCallback                      = 1 << 0,
};

namespace TextureFlags {
    enum Enum {
        Default, RenderTarget, Compute, Count
    };

    enum Mask {
        Default_mask = 1 << 0, RenderTarget_mask = 1 << 1, Compute_mask = 1 << 2, Sparse_mask = 1 << 3
    };

    static const char* s_value_names[] = {
        "Default", "RenderTarget", "Compute", "Count"
    };

    static const char* ToString( Enum e ) {
        return ((u32)e < Enum::Count ? s_value_names[(int)e] : "unsupported" );
    }

} // namespace TextureFlags


namespace PipelineStage {

    enum Enum {
        DrawIndirect = 0, VertexInput = 1, VertexShader = 2, FragmentShader = 3, RenderTarget = 4, ComputeShader = 5, Transfer = 6
    };

    enum Mask {
        DrawIndirect_mask = 1 << 0, VertexInput_mask = 1 << 1, VertexShader_mask = 1 << 2, FragmentShader_mask = 1 << 3, RenderTarget_mask = 1 << 4, ComputeShader_mask = 1 << 5, Transfer_mask = 1 << 6
    };

} // namespace PipelineStage

namespace ResourceUpdateType {

    enum Enum {
        Buffer, Texture, Pipeline, Sampler, DescriptorSetLayout, DescriptorSet, RenderPass, Framebuffer, ShaderState, TextureView, PagePool, Count
    };

    static const char* s_value_names[] = {
        "Buffer", "Texture", "Pipeline", "Sampler", "DescriptorSetLayout", "DescriptorSet", "RenderPass", "Framebuffer", "ShaderState", "TextureView", "PagePool"
    };

    static const char* ToString( Enum e ) {
        return ( ( u32 )e < Enum::Count ? s_value_names[ ( int )e ] : "unsupported" );
    }
} // namespace ResourceUpdateType

namespace PresentMode {

    enum Enum {
        Immediate, VSync, VSyncFast, VSyncRelaxed, Count
    }; // enum Enum

    static const char* s_value_names[] = {
        "Immediate", "VSync", "VSyncFast", "VSyncRelaxed", "Count"
    };

    static const char* ToString( Enum e ) {
        return ( ( u32 )e < Enum::Count ? s_value_names[ ( int )e ] : "unsupported" );
    }

} // namespace PresentMode

namespace RenderPassOperation {

    enum Enum {
        DontCare, Load, Clear, Count
    }; // enum Enum
} // namespace RenderPassOperation

// TODO: taken from the Forge
typedef enum ResourceState {
    RESOURCE_STATE_UNDEFINED = 0,
    RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER = 0x1,
    RESOURCE_STATE_INDEX_BUFFER = 0x2,
    RESOURCE_STATE_RENDER_TARGET = 0x4,
    RESOURCE_STATE_UNORDERED_ACCESS = 0x8,
    RESOURCE_STATE_DEPTH_WRITE = 0x10,
    RESOURCE_STATE_DEPTH_READ = 0x20,
    RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE = 0x40,
    RESOURCE_STATE_PIXEL_SHADER_RESOURCE = 0x80,
    RESOURCE_STATE_SHADER_RESOURCE = 0x40 | 0x80,
    RESOURCE_STATE_STREAM_OUT = 0x100,
    RESOURCE_STATE_INDIRECT_ARGUMENT = 0x200,
    RESOURCE_STATE_COPY_DEST = 0x400,
    RESOURCE_STATE_COPY_SOURCE = 0x800,
    RESOURCE_STATE_GENERIC_READ = ( ( ( ( ( 0x1 | 0x2 ) | 0x40 ) | 0x80 ) | 0x200 ) | 0x800 ),
    RESOURCE_STATE_PRESENT = 0x1000,
    RESOURCE_STATE_COMMON = 0x2000,
    RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE = 0x4000,
    RESOURCE_STATE_SHADING_RATE_SOURCE = 0x8000,
} ResourceState;

static cstring ResourceStateName( ResourceState value );

// TODO: Error enum?


} // namespace raptor
