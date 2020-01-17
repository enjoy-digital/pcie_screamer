#!/usr/bin/env python3

import argparse

from migen import *
from migen.genlib.resetsync import AsyncResetSynchronizer

from litex.build.generic_platform import *

from litex.soc.cores.clock import *
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


class _CRG(Module):
    def __init__(self, platform, sys_clk_freq):
        self.clock_domains.cd_sys       = ClockDomain()
        self.clock_domains.cd_usb       = ClockDomain()

        # # #

        # sys
        self.submodules.pll = pll = S7PLL(speedgrade=-1)
        pll.register_clkin(platform.request("clk100"), 100e6)
        pll.create_clkout(self.cd_sys,       sys_clk_freq)

        # usb 100MHz
        self.comb += self.cd_usb.clk.eq(platform.request("usb_fifo_clock"))
        self.specials += AsyncResetSynchronizer(self.cd_usb, ResetSignal("pcie"))


class PCIeScreamerSoC(SoCCore):
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
            ident="PCIe Screamer example design",
            with_timer=with_cpu
        )
        self.submodules.crg = _CRG(platform, clk_freq)

        if not with_cpu:
            # use serial as wishbone bridge when no cpu
            self.submodules.bridge = UARTWishboneBridge(platform.request("serial"), clk_freq, baudrate=3000000)
            self.add_wb_master(self.bridge.wishbone)

        try:
            # pcie_x = "pcie_x4"
            pcie_x = "pcie_x1"
            pcie_pads = platform.request(pcie_x)
        except ConstraintError:
            pcie_x = "pcie_x1"
            pcie_pads = platform.request(pcie_x)

        # pcie endpoint
        self.submodules.pciephy = S7PCIEPHY(platform, pcie_pads, cd="sys")
        if pcie_x == "pcie_x4":
            self.pciephy.use_external_hard_ip(os.path.join("pcie", "xilinx", "7-series"))
        platform.add_platform_command("create_clock -name pcie_clk -period 8 [get_nets pcie_clk]")
        platform.add_false_path_constraints(
            self.crg.cd_sys.clk,
            self.pciephy.cd_pcie.clk)
        self.add_csr("pciephy")

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
        self.add_csr("msi")

        # led blink
        usb_counter = Signal(32)
        self.sync.usb += usb_counter.eq(usb_counter + 1)
        self.comb += platform.request("user_led", 0).eq(usb_counter[26])

        pcie_counter = Signal(32)
        self.sync.pcie += pcie_counter.eq(pcie_counter + 1)
        self.comb += platform.request("user_led", 1).eq(pcie_counter[26])

        # timing constraints
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
            self.add_csr("analyzer")

    def do_exit(self, vns):
        if hasattr(self, "analyzer"):
            self.analyzer.export_csv(vns, "test/analyzer.csv")


def main():
    platform_names = ["pciescreamer", "screamerm2"]

    parser = argparse.ArgumentParser(description="PCIe Screamer Test Gateware")
    parser.add_argument("--platform", choices=platform_names, required=True)
    args = parser.parse_args()

    if args.platform == "pciescreamer":
        from platforms import pciescreamer_r02 as target
    elif args.platform == "screamerm2":
        from platforms import screamerm2_r03 as target

    platform = target.Platform()
    soc = PCIeScreamerSoC(platform)
    builder = Builder(soc, output_dir="build", csr_csv="test/csr.csv")
    vns = builder.build()
    soc.do_exit(vns)


if __name__ == "__main__":
    main()
