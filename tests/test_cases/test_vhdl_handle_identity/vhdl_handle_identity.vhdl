-- Copyright cocotb contributors
-- Licensed under the Revised BSD License, see LICENSE for details.
-- SPDX-License-Identifier: BSD-3-Clause

-- Objects reachable through every way the VHDL implementations build a full
-- name: ports and signals, a component instance, a generate block and a record.
-- Basic identifiers are written in mixed case on purpose; the extended
-- identifiers differ only in case and are distinct objects.

library ieee;
use ieee.std_logic_1164.all;

package vhdl_handle_identity_pkg is
    type rec_t is record
        field_a : std_logic;
        field_b : std_logic_vector(3 downto 0);
    end record rec_t;
end package vhdl_handle_identity_pkg;

library ieee;
use ieee.std_logic_1164.all;

entity vhdl_handle_identity_sub is
    port (
        a : in  std_logic;
        y : out std_logic
    );
end entity vhdl_handle_identity_sub;

architecture rtl of vhdl_handle_identity_sub is
    signal sub_sig : std_logic;
begin
    sub_sig <= a;
    y       <= not sub_sig;
end architecture rtl;

library ieee;
use ieee.std_logic_1164.all;
use work.vhdl_handle_identity_pkg.all;

entity vhdl_handle_identity is
    port (
        data_in : in  std_logic_vector(7 downto 0);
        q       : out std_logic
    );
end entity vhdl_handle_identity;

architecture rtl of vhdl_handle_identity is
    component vhdl_handle_identity_sub is
        port (
            a : in  std_logic;
            y : out std_logic
        );
    end component vhdl_handle_identity_sub;

    signal Mixed_Case : std_logic;
    signal rec_sig    : rec_t;
    signal \Ext_Sig\  : std_logic := '0';
    signal \ext_sig\  : std_logic := '0';
    -- extended identifiers with a backslash (written twice) in them
    signal \Back\\Slash\ : std_logic := '0';
    signal \back\\slash\ : std_logic := '0';
    signal \Back\\slash\ : std_logic := '0';  -- differs only after the backslash
    -- only looked up in other spellings than the source one
    signal First_Other : std_logic;
begin
    Mixed_Case    <= data_in(1);
    First_Other   <= data_in(3);
    rec_sig.field_a <= data_in(2);
    rec_sig.field_b <= data_in(7 downto 4);

    u_sub : component vhdl_handle_identity_sub
        port map (
            a => data_in(0),
            y => q
        );

    -- only looked up in other spellings than the source one
    u_other : component vhdl_handle_identity_sub
        port map (
            a => data_in(1),
            y => open
        );

    gen : for i in 0 to 1 generate
        signal g_sig : std_logic;
    begin
        g_sig <= data_in(i);
    end generate gen;

    -- only looked up in other spellings than the source one
    gen_b : for i in 0 to 1 generate
        signal g_sig : std_logic;
    begin
        g_sig <= data_in(i + 2);
    end generate gen_b;
end architecture rtl;
