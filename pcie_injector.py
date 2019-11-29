#!/usr/bin/env python3
import argparse

from migen import *
from migen.genlib.resetsync import AsyncResetSynchronizer

from litex.build.generic_platform import *

from litex.soc.interconnect.csr import *
from litex.soc.integration.soc_sdram import *
from litex.soc.integration.soc_core import *
from litex.soc.integration.builder import *
from litex.soc.interconnect import stream
from litex.soc.cores.uart import UARTWishboneBridge

from litepcie.phy.s7pciephy import S7PCIEPHY
from litex.soc.cores.usb_fifo import phy_description

from gateware.usb import USBCore
from gateware.etherbone import Etherbone
from gateware.tlp import TLP
from gateware.msi import MSI
from gateware.ft601 import FT601Sync

from litescope import LiteScopeAnalyzer


class _CRG(Module, AutoCSR):
    def __init__(self, platform):
        self.clock_domains.cd_sys = ClockDomain("sys")
        self.clock_domains.cd_sys4x = ClockDomain(reset_less=True)
        self.clock_domains.cd_sys4x_dqs = ClockDomain(reset_less=True)
        self.clock_domains.cd_clk200 = ClockDomain()

        self.clock_domains.cd_usb = ClockDomain()

        # usb clock domain (100MHz from usb)
        self.comb += self.cd_usb.clk.eq(platform.request("usb_fifo_clock"))
        self.comb += self.cd_usb.rst.eq(ResetSignal("pcie"))

        clk100 = platform.request("clk100")

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
                     i_CLKIN1=clk100, i_CLKFBIN=pll_fb, o_CLKFBOUT=pll_fb,

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
            AsyncResetSynchronizer(self.cd_sys, ~pll_locked),
            AsyncResetSynchronizer(self.cd_clk200, ~pll_locked)
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


class PCIeInjectorSoC(SoCCore):
    csr_map = {
        "ddrphy":   16,
        "pciephy":  17,
        "msi":      18,
        "analyzer": 19
    }
    csr_map.update(SoCCore.csr_map)

    usb_map = {
        "wishbone": 0,
        "tlp":      1
    }

    def __init__(self, platform, with_cpu=False, with_analyzer=True, with_loopback=False):
        clk_freq = int(100e6)
        SoCCore.__init__(self, platform, clk_freq,
            cpu_type="lm32" if with_cpu else None,
            integrated_rom_size=0x8000 if with_cpu else 0,
            integrated_sram_size=0x8000,
            with_uart=with_cpu,
            ident="PCIe Injector example design",
            with_timer=with_cpu
        )
        self.submodules.crg = _CRG(platform)

        if not with_cpu:
            # use serial as wishbone bridge when no cpu
            self.submodules.bridge = UARTWishboneBridge(platform.request("serial"), clk_freq, baudrate=3000000)
            self.add_wb_master(self.bridge.wishbone)

        try:
            pcie_x = "pcie_x4"
            pcie_pads = platform.request(pcie_x)
        except ConstraintError:
            pcie_x = "pcie_x1"
            pcie_pads = platform.request(pcie_x)

        # pcie endpoint
        self.submodules.pciephy = S7PCIEPHY(platform, pcie_pads, cd="sys")
        if pcie_x == "pcie_x4":
            self.pciephy.use_external_hard_ip(os.path.join("pcie", "xilinx", "7-series"))
        self.pciephy.cd_pcie.clk.attr.add("keep")
        platform.add_platform_command("create_clock -name pcie_clk -period 8 [get_nets pcie_clk]")
        platform.add_false_path_constraints(
            self.crg.cd_sys.clk,
            self.pciephy.cd_pcie.clk)

        # usb core
        usb_pads = platform.request("usb_fifo")
        self.submodules.usb_phy = FT601Sync(usb_pads, dw=32, timeout=1024)

        if with_loopback:
            self.submodules.usb_loopback_fifo = stream.SyncFIFO(phy_description(32), 2048)
            self.comb += [
                self.usb_phy.source.connect(self.usb_loopback_fifo.sink),
                self.usb_loopback_fifo.source.connect(self.usb_phy.sink)
            ]
        else:
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
        self.comb += self.msi.source.connect(self.pciephy.msi)

        # led blink
        usb_counter = Signal(32)
        self.sync.usb += usb_counter.eq(usb_counter + 1)
        self.comb += platform.request("user_led", 0).eq(usb_counter[26])

        pcie_counter = Signal(32)
        self.sync.pcie += pcie_counter.eq(pcie_counter + 1)
        self.comb += platform.request("user_led", 1).eq(pcie_counter[26])

        # timing constraints
        self.crg.cd_sys.clk.attr.add("keep")
        self.crg.cd_usb.clk.attr.add("keep")
        self.platform.add_period_constraint(self.crg.cd_sys.clk, 10.0)
        self.platform.add_period_constraint(self.crg.cd_usb.clk, 10.0)
        self.platform.add_period_constraint(self.platform.lookup_request(pcie_x).clk_p, 10.0)

        if with_analyzer:
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
    platform_names = ["pciescreamer", "screamerm2"]

    parser = argparse.ArgumentParser(description="PCIe Injector Test Gateware")
    parser.add_argument("--platform", choices=platform_names, required=True)
    args = parser.parse_args()

    if args.platform == "pciescreamer":
        from platforms import pciescreamer_r02 as target
    elif args.platform == "screamerm2":
        from platforms import screamerm2_r03 as target

    platform = target.Platform()
    soc = PCIeInjectorSoC(platform, with_loopback=True)
    builder = Builder(soc, output_dir="build", csr_csv="test/csr.csv")
    vns = builder.build()
    soc.do_exit(vns)


if __name__ == "__main__":
    main()
