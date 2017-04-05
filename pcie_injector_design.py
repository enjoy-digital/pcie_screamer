#!/usr/bin/env python3
from litex.gen import *
from litex.gen.genlib.resetsync import AsyncResetSynchronizer

import platform as pcie_injector

from litex.soc.interconnect.csr import *
from litex.soc.interconnect import wishbone

from litex.soc.integration.soc_sdram import *
from litex.soc.integration.builder import *

from litedram.modules import MT41K256M16
from litedram.phy import a7ddrphy

from litepcie.phy.s7pciephy import S7PCIEPHY

from liteusb.phy.ft245 import phy_description, FT245PHYSynchronous
from gateware.usb import USBCore
from gateware.etherbone import Etherbone
from gateware.tlp import TLP
from gateware.msi import MSI

from litescope import LiteScopeAnalyzer


class _CRG(Module, AutoCSR):
    def __init__(self, platform):
        self.clock_domains.cd_sys = ClockDomain("sys")
        self.clock_domains.cd_sys4x = ClockDomain(reset_less=True)
        self.clock_domains.cd_sys4x_dqs = ClockDomain(reset_less=True)
        self.clock_domains.cd_clk200 = ClockDomain()

        self.clock_domains.cd_usb = ClockDomain()

        self.clock_domains.cd_clk125 = ClockDomain("clk125")

        # usb clock domain (100MHz from usb)
        self.comb += self.cd_usb.clk.eq(platform.request("usb_fifo_clock"))
        self.specials += AsyncResetSynchronizer(self.cd_usb, self.cd_clk125.rst)

        # sys & ddr clock domains
        pll_locked = Signal()
        pll_fb = Signal()
        pll_sys = Signal()
        pll_sys4x = Signal()
        pll_sys4x_dqs = Signal()
        pll_clk200 = Signal()
        self.specials += [
            Instance("PLLE2_BASE",
                     p_STARTUP_WAIT="FALSE", o_LOCKED=pll_locked,

                     # VCO @ 1600 MHz
                     p_REF_JITTER1=0.01, p_CLKIN1_PERIOD=10.0,
                     p_CLKFBOUT_MULT=16, p_DIVCLK_DIVIDE=1,
                     i_CLKIN1=ClockSignal("usb"), i_CLKFBIN=pll_fb, o_CLKFBOUT=pll_fb,

                     # 100 MHz
                     p_CLKOUT0_DIVIDE=16, p_CLKOUT0_PHASE=0.0,
                     o_CLKOUT0=pll_sys,

                     # 400 MHz
                     p_CLKOUT1_DIVIDE=4, p_CLKOUT1_PHASE=0.0,
                     o_CLKOUT1=pll_sys4x,

                     # 400 MHz dqs
                     p_CLKOUT2_DIVIDE=4, p_CLKOUT2_PHASE=90.0,
                     o_CLKOUT2=pll_sys4x_dqs,

                     # 200 MHz
                     p_CLKOUT3_DIVIDE=8, p_CLKOUT3_PHASE=0.0,
                     o_CLKOUT3=pll_clk200,
            ),
            Instance("BUFG", i_I=pll_sys, o_O=self.cd_sys.clk),
            Instance("BUFG", i_I=pll_sys4x, o_O=self.cd_sys4x.clk),
            Instance("BUFG", i_I=pll_sys4x_dqs, o_O=self.cd_sys4x_dqs.clk),
            Instance("BUFG", i_I=pll_clk200, o_O=self.cd_clk200.clk),
            AsyncResetSynchronizer(self.cd_sys, ~pll_locked | ~ResetSignal("usb")),
            AsyncResetSynchronizer(self.cd_clk200, ~pll_locked | ~ResetSignal("usb"))
        ]

        reset_counter = Signal(4, reset=15)
        ic_reset = Signal(reset=1)
        self.sync.clk200 += \
            If(reset_counter != 0,
                reset_counter.eq(reset_counter - 1)
            ).Else(
                ic_reset.eq(0)
            )
        self.specials += Instance("IDELAYCTRL", i_REFCLK=ClockSignal("clk200"), i_RST=ic_reset)


class PCIeInjectorSoC(SoCSDRAM):
    csr_map = {
        "ddrphy":   16,
        "pciephy":  17,
        "msi":      18,
        "analyzer": 19
    }
    csr_map.update(SoCSDRAM.csr_map)

    usb_map = {
        "wishbone": 0,
        "tlp":      1
    }

    def __init__(self, platform, with_pcie_analyzer=False):
        clk_freq = int(100e6)
        SoCSDRAM.__init__(self, platform, clk_freq,
            integrated_rom_size=0x8000,
            integrated_sram_size=0x8000,
            ident="PCIe Injector example design"
        )
        self.submodules.crg = _CRG(platform)

        # sdram
        self.submodules.ddrphy = a7ddrphy.A7DDRPHY(platform.request("ddram"))
        self.add_constant("A7DDRPHY_BITSLIP", 2)
        self.add_constant("A7DDRPHY_DELAY", 8)
        sdram_module = MT41K256M16(self.clk_freq, "1:4")
        self.register_sdram(self.ddrphy,
                            sdram_module.geom_settings,
                            sdram_module.timing_settings)

        # pcie endpoint
        self.submodules.pciephy = S7PCIEPHY(platform, link_width=2, cd="sys")

        # usb core
        usb_pads = platform.request("usb_fifo")
        self.comb += [
            usb_pads.rst.eq(1),
            usb_pads.be.eq(0xf)
        ]
        self.submodules.usb_phy = FT245PHYSynchronous(usb_pads, clk_freq)
        self.submodules.usb_core = USBCore(self.usb_phy, clk_freq)

        # usb <--> wishbone
        self.submodules.etherbone = Etherbone(self.usb_core, self.usb_map["wishbone"])
        self.add_wb_master(self.etherbone.master.bus)

        # usb <--> tlp
        self.submodules.tlp = TLP(self.usb_core, self.usb_map["tlp"])
        self.comb += [
            self.pciephy.source.connect(self.tlp.sender.sink),
            self.tlp.receiver.source.connect(self.pciephy.sink)
        ]

        # wishbone --> msi
        self.submodules.msi = MSI()
        self.comb += self.msi.source.connect(self.pciephy.interrupt)


        # led blink
        sys_counter = Signal(32)
        self.sync.sys += sys_counter.eq(sys_counter + 1)
        self.comb += platform.request("user_led", 0).eq(sys_counter[26])

        usb_counter = Signal(32)
        self.sync.usb += usb_counter.eq(usb_counter + 1)
        self.comb += platform.request("user_led", 1).eq(usb_counter[26])

        # timing constraints
        self.crg.cd_sys.clk.attr.add("keep")
        self.crg.cd_usb.clk.attr.add("keep")
        self.platform.add_period_constraint(self.crg.cd_sys.clk, 8.0)
        self.platform.add_period_constraint(self.crg.cd_usb.clk, 10.0)
        self.platform.add_period_constraint(self.platform.lookup_request("pcie_x2").clk_p, 10)

        if with_pcie_analyzer:
            analyzer_signals = [
                self.pciephy.sink.valid,
                self.pciephy.sink.ready,
                self.pciephy.sink.last,
                self.pciephy.sink.dat,
                self.pciephy.sink.be,

                self.pciephy.source.valid,
                self.pciephy.source.ready,
                self.pciephy.source.last,
                self.pciephy.source.dat,
                self.pciephy.source.be
            ]
            self.submodules.analyzer = LiteScopeAnalyzer(analyzer_signals, 1024)

    def do_exit(self, vns):
        if hasattr(self, "analyzer"):
            self.analyzer.export_csv(vns, "test/analyzer.csv")


def main():
    platform = pcie_injector.Platform()
    soc = PCIeInjectorSoC(platform)
    builder = Builder(soc, output_dir="build", csr_csv="test/csr.csv")
    vns = builder.build()
    soc.do_exit(vns)


if __name__ == "__main__":
    main()
