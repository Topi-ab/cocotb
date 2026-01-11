/* Auto-generated C++ interface header from Yosys scan-chain JSON description 
 * Do not edit manually! 

 * Source DUT: \sv_pipeline_2x2 
 * Generated on: Sun Jan 11 20:00:45 2026 
 */ 

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "fields.h"

class WrapperInterface {
public:
    static constexpr uint64_t dut_hash = 0xffe3f722bffe252a;
    static constexpr size_t dut_hash_bits = 64;
    static constexpr std::string_view dut_hash_out_name = "DUT_HASH_OUT";

    enum class clk_fields: std::size_t {
        CLK_IN
    };

    enum class wr_fields: std::size_t {
        A_0_IN,
        A_1_IN
    };

    enum class rd_fields: std::size_t {
        B_0_OUT,
        B_1_OUT
    };

    consteval static
    auto get_clk_names()
    {
        return std::to_array<std::string_view>({
            "CLK_IN"
        });
    }

    consteval static
    auto get_wr_names()
    {
        return std::to_array<std::string_view>({
            "A_0_IN",
            "A_1_IN"
        });
    }

    consteval static
    auto get_rd_names()
    {
        return std::to_array<std::string_view>({
            "B_0_OUT",
            "B_1_OUT"
        });
    }

    static constexpr std::string_view clk_name(clk_fields f) {
        return get_clk_names()[static_cast<size_t>(f)];
    }

    static constexpr std::string_view wr_name(wr_fields f) {
        return get_wr_names()[static_cast<size_t>(f)];
    }

    static constexpr std::string_view rd_name(rd_fields f) {
        return get_rd_names()[static_cast<size_t>(f)];
    }

    consteval static
    auto get_clk_specs()
    {
        return std::to_array<FieldSpec<clk_fields>>({
            { clk_fields::CLK_IN, 1 }
        });
    }

    consteval static
    auto get_wr_specs()
    {
        return std::to_array<FieldSpec<wr_fields>>({
            { wr_fields::A_0_IN, 1 },
            { wr_fields::A_1_IN, 1 }
        });
    }

    consteval static
    auto get_rd_specs()
    {
        return std::to_array<FieldSpec<rd_fields>>({
            { rd_fields::B_0_OUT, 1 },
            { rd_fields::B_1_OUT, 1 }
        });
    }
};
