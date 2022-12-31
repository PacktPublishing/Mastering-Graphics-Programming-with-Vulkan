#pragma once

#include "foundation/array.hpp"
#include "graphics/gpu_resources.hpp"

#if defined(_MSC_VER)
#include <spirv-headers/spirv.h>
#else
#include <spirv_cross/spirv.h>
#endif

namespace raptor {

    struct StringBuffer;

namespace spirv {

    static const u32                k_max_count     = 8;
    static const u32                k_max_specialization_constants = 4;

    
    struct ConstantValue {

        enum class Type : u8 {
            Type_i32 = 0,
            Type_u32,
            Type_f32,
            Type_count
        }; // enum Type

        union Value {

            i32                         value_i;
            u32                         value_u;
            f32                         value_f;
        }; // union Value

        Value                           value;
        Type                            type;
    }; // struct ConstantValue

    struct SpecializationConstant {

        u16                         binding         = 0;
        u16                         byte_stride     = 0;

        ConstantValue               default_value;

    }; // struct SpecializationConstant

    struct SpecializationName {
        char                        name[ 32 ];
    }; // struct SpecializationName

    struct ParseResult {
        u32                         set_count                       = 0;
        u32                         specialization_constants_count  = 0;
        u32                         push_constants_stride           = 0;

        DescriptorSetLayoutCreation sets[k_max_count];
        SpecializationConstant      specialization_constants[ k_max_specialization_constants ];
        SpecializationName          specialization_names[ k_max_specialization_constants ];

        ComputeLocalSize            compute_local_size;
    }; // struct ParseResult

    void                            parse_binary( const u32* data, size_t data_size, StringBuffer& name_buffer, ParseResult* parse_result );

} // namespace spirv
} // namespace raptor
