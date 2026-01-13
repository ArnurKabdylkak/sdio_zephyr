#!/usr/bin/env python3

#
# This file is part of LiteX-Boards.
#
# Copyright (c) 2022 Icenowy Zheng <icenowy@aosc.io>
# Copyright (c) 2022 Florent Kermarrec <florent@enjoy-digital.fr>
# SPDX-License-Identifier: BSD-2-Clause

# Patch for Python 3.11+ compatibility
import sys
import re

def _patch_litex_csr():
    """Monkey-patch LiteX CSR to work with Python 3.11+"""
    from litex.soc.interconnect import csr

    original_init = csr._CSRBase.__init__

    def patched_init(self, size=1, name=None, n=1):
        if name is None:
            # Try to extract name from calling frame
            frame = sys._getframe(2)
            try:
                # Try bytecode analysis
                import dis
                instructions = list(dis.get_instructions(frame.f_code))

                for i, instr in enumerate(instructions):
                    if instr.opname == 'STORE_ATTR':
                        attr_name = instr.argval
                        if attr_name and attr_name.startswith('_'):
                            name = attr_name[1:]
                            break
                        elif attr_name:
                            name = attr_name
                            break

                # Fallback: parse source
                if name is None:
                    import inspect
                    try:
                        source_lines = inspect.getsource(frame.f_code).split('\n')
                        lineno = frame.f_lineno - frame.f_code.co_firstlineno
                        if 0 <= lineno < len(source_lines):
                            line = source_lines[lineno]
                            match = re.search(r'self\.(_?\w+)\s*=', line)
                            if match:
                                attr_name = match.group(1)
                                name = attr_name[1:] if attr_name.startswith('_') else attr_name
                    except:
                        pass
            except:
                pass

            # Final fallback
            if name is None:
                name = f"csr_{id(self) & 0xFFFF:04x}"

        original_init(self, size, name, n)

    csr._CSRBase.__init__ = patched_init

_patch_litex_csr()

from migen import *
from migen.genlib.resetsync import AsyncResetSynchronizer

from litex.gen import *

from litex.soc.cores.clock.gowin_gw2a import GW2APLL
from litex.soc.integration.soc_core import *
from litex.soc.integration.soc import SoCRegion
from litex.soc.integration.builder import *
from litex.soc.cores.led import LedChaser, WS2812
from litex.soc.cores.gpio import GPIOIn
from litex.soc.cores.video import *

from liteeth.phy.rmii import LiteEthPHYRMII

from litex_boards.platforms import sipeed_tang_primer_20k

from litedram.common import PHYPadsReducer
from litedram.modules import MT41K64M16
from litedram.phy import GW2DDRPHY
from litex.soc.cores.gpio import *
from litex.soc.interconnect import wishbone
from litex.build.generic_platform import Pins, IOStandard
from litex.build.generic_platform import Subsignal


class SDWishboneController(Module):
    def __init__(self, platform):
        # Создаем Wishbone slave interface
        self.bus = wishbone.Interface(data_width=32, address_width=32)
        print(self.bus.address_width)
        print(self.bus.adr_width)
        print(self.bus.sel)

        # Добавляем Verilog файл в список источников
        platform.add_source("hdl/WishboneController.sv", language="systemverilog")
        platform.add_source("hdl/SDDataController.sv", language="systemverilog")
        platform.add_source("hdl/SDCommandController.sv", language="systemverilog")
        # sdio = platform.request("sdcard", 0)
      
        
        platform.add_extension([("sd_debug", 0, Pins("P6 T6 T7 R8"), IOStandard("LVCMOS33"))])
        platform.add_extension([("wlan_wake_host", 0, Pins("P11"), IOStandard("LVCMOS33"))])
        platform.add_extension([("wlan_enable", 0, Pins("T11"), IOStandard("LVCMOS33"))])
        platform.add_extension([("sdio", 0,
        	Subsignal("clk", Pins("M14")),
        	Subsignal("cmd", Pins("M15")),
        	Subsignal("data", Pins("J16 J14 R11 T12")),  # 4 линии
        	IOStandard("LVCMOS33"))])
        sdio = platform.request("sdio", 0)
        
        platform.add_period_constraint(sdio.clk, 27.0)
        # Инстанциируем Verilog модуль
        self.specials += Instance("WishboneController",            
            o_sd_clock   	= sdio.clk,
            io_sd_command	= sdio.cmd,
            io_sd_data	 	= sdio.data,
            i_wb_clk     	= ClockSignal(),
            i_wb_rst     	= ResetSignal(),
            i_wb_cyc_i   	= self.bus.cyc,
            i_wb_stb_i   	= self.bus.stb,
            i_wb_we_i    	= self.bus.we,
            i_wb_adr_i   	= self.bus.adr,
            i_wb_dat_w_i 	= self.bus.dat_w,
            i_wb_sel_i  	= self.bus.sel,
            o_wb_dat_o   	= self.bus.dat_r,
            o_wb_ack_o   	= self.bus.ack,
            o_wb_err_o   	= self.bus.err,
            o_sd_debug 		= platform.request("sd_debug", 0),
            o_wlan_wake_host = platform.request("wlan_wake_host", 0),
            o_wlan_enable   = platform.request("wlan_enable", 0),
        )




# CRG ----------------------------------------------------------------------------------------------

class _CRG(LiteXModule):
    def __init__(self, platform, sys_clk_freq, with_video_pll=False, with_dram=False):
        self.rst        = Signal()
        self.cd_sys     = ClockDomain("sys")
        self.cd_por     = ClockDomain("por")
        if with_dram:
            self.cd_init    = ClockDomain("init")
            self.cd_sys2x   = ClockDomain("sys2x")
            self.cd_sys2x_i = ClockDomain("sys2x_i")

        # # #

        self.stop  = Signal()
        self.reset = Signal()

        # Clk
        clk27 = platform.request("clk27")

        # Power on reset (the onboard POR is not aware of reprogramming)
        por_count = Signal(16, reset=2**16-1)
        por_done  = Signal()
        self.comb += self.cd_por.clk.eq(clk27)
        self.comb += por_done.eq(por_count == 0)
        self.sync.por += If(~por_done, por_count.eq(por_count - 1))

        # PLL
        self.pll = pll = GW2APLL(devicename=platform.devicename, device=platform.device)
        self.comb += pll.reset.eq(~por_done)
        pll.register_clkin(clk27, 27e6)
        if with_dram:
            # 2:1 clock needed for DDR
            pll.create_clkout(self.cd_sys2x_i, 2*sys_clk_freq)
            self.specials += [
                Instance("DHCEN",
                    i_CLKIN  = self.cd_sys2x_i.clk,
                    i_CE     = self.stop,
                    o_CLKOUT = self.cd_sys2x.clk),
                Instance("CLKDIV",
                    p_DIV_MODE = "2",
                    i_CALIB    = 0,
                    i_HCLKIN   = self.cd_sys2x.clk,
                    i_RESETN   = ~self.reset,
                    o_CLKOUT   = self.cd_sys.clk),
            ]

            # Init clock domain
            self.comb += self.cd_init.clk.eq(clk27)
            self.comb += self.cd_init.rst.eq(pll.reset)
        else:
            pll.create_clkout(self.cd_sys, sys_clk_freq)
        
        self.specials += AsyncResetSynchronizer(self.cd_sys, ~pll.locked | self.rst | self.reset)

        # Video PLL
        if with_video_pll:
            self.video_pll = video_pll = GW2APLL(devicename=platform.devicename, device=platform.device)
            video_pll.register_clkin(clk27, 27e6)
            self.cd_hdmi   = ClockDomain("hdmi")
            self.cd_hdmi5x = ClockDomain("hdmi5x")
            video_pll.create_clkout(self.cd_hdmi5x, 125e6, margin=1e-3)
            self.specials += Instance("CLKDIV",
                p_DIV_MODE = "5",
                i_RESETN   = 1, # Disable reset signal.
                i_CALIB    = 0, # No calibration.
                i_HCLKIN   = self.cd_hdmi5x.clk,
                o_CLKOUT   = self.cd_hdmi.clk
            )

# BaseSoC ------------------------------------------------------------------------------------------

class BaseSoC(SoCCore):
    def __init__(self, toolchain="gowin", sys_clk_freq=48e6,
        with_spi_flash      = False,
        with_led_chaser     = True,
        with_rgb_led        = False,
        with_buttons        = False,  # Disabled to avoid SSPI pin conflict
        with_video_terminal = False,
        with_ethernet       = False,
        with_etherbone      = False,
        eth_ip              = "192.168.1.50",
        remote_ip           = None,
        eth_dynamic_ip      = False,
        dock                = "standard",
        **kwargs):

        assert dock in ["standard", "lite"]

        platform = sipeed_tang_primer_20k.Platform(dock, toolchain=toolchain)

        if dock == "lite":
            with_led_chaser = False # No leds on core board nor on dock lite.

        # CRG --------------------------------------------------------------------------------------
        with_dram = (kwargs.get("integrated_main_ram_size", 0) == 0)
        assert not (toolchain == "apicula" and with_dram)
        self.crg  = _CRG(platform, sys_clk_freq, with_video_pll=with_video_terminal, with_dram=with_dram)

        # SoCCore ----------------------------------------------------------------------------------
        SoCCore.__init__(self, platform, sys_clk_freq, ident="LiteX SoC on Tang Primer 20K", **kwargs)
        # My custom slave
        self.my_slave = SDWishboneController(platform, )
        self.bus.add_slave("my_slave", self.my_slave.bus,
                           region=SoCRegion(
                               origin = 0x8000_0000,
                               size = 0x10000,
                               cached = False
                           ))
        # DDR3 SDRAM -------------------------------------------------------------------------------
        if with_dram:
            self.ddrphy = GW2DDRPHY(
                pads         = platform.request("ddram"),
                sys_clk_freq = sys_clk_freq
            )
            self.ddrphy.settings.rtt_nom = "disabled"
            self.comb += self.crg.stop.eq(self.ddrphy.init.stop)
            self.comb += self.crg.reset.eq(self.ddrphy.init.reset)
            self.add_sdram("sdram",
                phy           = self.ddrphy,
                module        = MT41K64M16(sys_clk_freq, "1:2"),
                l2_cache_size = kwargs.get("l2_size", 8192)
            )

        # SPI Flash --------------------------------------------------------------------------------
        if with_spi_flash:
            from litespi.modules import W25Q32JV as SpiFlashModule
            from litespi.opcodes import SpiNorFlashOpCodes as Codes
            self.add_spi_flash(mode="1x", module=SpiFlashModule(Codes.READ_1_1_1))

        # Ethernet / Etherbone ---------------------------------------------------------------------
        if with_ethernet or with_etherbone:
            from liteeth.phy.rmii import LiteEthPHYRMII
            self.ethphy = LiteEthPHYRMII(
                clock_pads = self.platform.request("eth_clocks"),
                pads       = self.platform.request("eth"),
                refclk_cd  = None
            )
            if with_ethernet:
                self.add_ethernet(phy=self.ethphy,
                    dynamic_ip              = eth_dynamic_ip,
                    local_ip                = eth_ip,
                    remote_ip               = remote_ip,
                    with_timing_constraints = False
                )
            if with_etherbone:
                self.add_etherbone(phy=self.ethphy, ip_address=eth_ip, with_timing_constraints=False)

        # Video ------------------------------------------------------------------------------------
        if with_video_terminal:
            hdmi_pads = platform.request("hdmi")
            self.comb += hdmi_pads.hdp.eq(1)
            self.videophy = VideoHDMIPHY(hdmi_pads, clock_domain="hdmi")
            #self.add_video_colorbars(phy=self.videophy, timings="640x480@60Hz", clock_domain="hdmi")
            self.add_video_terminal(phy=self.videophy, timings="640x480@75Hz", clock_domain="hdmi")

        # Leds -------------------------------------------------------------------------------------
        #if with_led_chaser:
        #    self.leds = GPIOOut(pads = platform.request_all("led"))
            #LedChaser(
            #    pads         = platform.request_all("led"),
            #    sys_clk_freq = sys_clk_freq
            #)

        # RGB Led ----------------------------------------------------------------------------------
        if with_rgb_led:
            self.rgb_led = WS2812(
                pad          = platform.request("rgb_led"),
                nleds        = 1,
                sys_clk_freq = sys_clk_freq
            )
            self.bus.add_slave(name="rgb_led", slave=self.rgb_led.bus, region=SoCRegion(
                origin = 0x2000_0000,
                size   = 4,
            ))

        # Buttons ----------------------------------------------------------------------------------
        if with_buttons:
            self.buttons = GPIOIn(pads=~platform.request_all("btn_n"))


# Build --------------------------------------------------------------------------------------------

def main():
    from litex.build.parser import LiteXArgumentParser
    parser = LiteXArgumentParser(platform=sipeed_tang_primer_20k.Platform, description="LiteX SoC on Tang Primer 20K.")
    parser.add_target_argument("--dock",         default="standard",       help="Dock version (standard (default) or lite.")
    parser.add_target_argument("--flash",        action="store_true",      help="Flash Bitstream.")
    parser.add_target_argument("--sys-clk-freq", default=48e6, type=float, help="System clock frequency.")
    sdopts = parser.target_group.add_mutually_exclusive_group()
    sdopts.add_argument("--with-spi-sdcard",            action="store_true", help="Enable SPI-mode SDCard support.")
    sdopts.add_argument("--with-sdcard",                action="store_true", help="Enable SDCard support.")
    parser.add_target_argument("--with-spi-flash",      action="store_true", help="Enable SPI Flash (MMAPed).")
    parser.add_target_argument("--with-video-terminal", action="store_true", help="Enable Video Terminal (HDMI).")
    ethopts = parser.target_group.add_mutually_exclusive_group()
    ethopts.add_argument("--with-ethernet",        action="store_true",     help="Add Ethernet.")
    ethopts.add_argument("--with-etherbone",       action="store_true",     help="Add EtherBone.")
    parser.add_target_argument("--eth-ip",         default="192.168.1.50",  help="Etherbone IP address.")
    parser.add_target_argument("--remote-ip",      default="192.168.1.100", help="Remote IP address of TFTP server.")
    parser.add_target_argument("--eth-dynamic-ip", action="store_true",     help="Enable dynamic Ethernet IP addresses setting.")
    args = parser.parse_args()

    soc = BaseSoC(
        toolchain           = args.toolchain,
        sys_clk_freq        = args.sys_clk_freq,
        with_spi_flash      = args.with_spi_flash,
        with_video_terminal = args.with_video_terminal,
        with_ethernet       = args.with_ethernet,
        with_etherbone      = args.with_etherbone,
        eth_ip              = args.eth_ip,
        remote_ip           = args.remote_ip,
        eth_dynamic_ip      = args.eth_dynamic_ip,
        dock                = args.dock,
        **parser.soc_argdict
    )
    if args.with_spi_sdcard:
        soc.add_spi_sdcard()
    if args.with_sdcard:
        soc.add_sdcard()

    builder = Builder(soc, **parser.builder_argdict)
    if args.build:
        builder.build(**parser.toolchain_argdict)

    if args.load:
        prog = soc.platform.create_programmer()
        prog.load_bitstream(builder.get_bitstream_filename(mode="sram"))

    if args.flash:
        prog = soc.platform.create_programmer()
        prog.flash(0, builder.get_bitstream_filename(mode="flash", ext=".fs"), external=True)

if __name__ == "__main__":
    main()
