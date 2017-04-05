#!/usr/bin/env python3
import argparse
import os

from litex.gen import *
from litex.gen.genlib.io import CRG
from litex.gen.genlib.resetsync import AsyncResetSynchronizer
from litex.gen.genlib.misc import timeline
from litex.build.tools import write_to_file

import platform as pcie_injector

from litex.soc.interconnect.csr import *
from litex.soc.interconnect import wishbone

from litex.soc.integration.soc_core import *
from litex.soc.cores.uart.bridge import UARTWishboneBridge
from litex.soc.integration.builder import *

from litepcie.phy.s7pciephy import S7PCIEPHY

from liteusb.phy.ft245 import phy_description, FT245PHYSynchronous
from gateware.usb import USBCore
from gateware.etherbone import Etherbone
from gateware.tlp import TLP
from gateware.msi import MSI

from litescope import LiteScopeAnalyzer

import cpu_interface


class _CRG(Module, AutoCSR):
    def __init__(self, platform):
        self.clock_domains.cd_sys = ClockDomain("sys")
        self.clock_domains.cd_clk125 = ClockDomain("clk125")
        self.clock_domains.cd_usb = ClockDomain()

        # soft reset generaton
        self._soft_rst = CSR()
        soft_rst = Signal()
        # trigger soft reset 1us after CSR access to terminate
        # Wishbone access when reseting from PCIe
        self.sync += [
            timeline(self._soft_rst.re & self._soft_rst.r, [(125, [soft_rst.eq(1)])]),
        ]

        # sys clock domain (125MHz from PCIe)
        self.comb += self.cd_sys.clk.eq(self.cd_clk125.clk)
        self.specials += AsyncResetSynchronizer(self.cd_sys, self.cd_clk125.rst | soft_rst)

        # usb clock domain (100MHz from fifo interface)
        self.comb += self.cd_usb.clk.eq(platform.request("usb_fifo_clock"))
        self.specials += AsyncResetSynchronizer(self.cd_usb, self.cd_clk125.rst | soft_rst)

class PCIeInjectorSoC(SoCCore):
    csr_map = {
        "crg":      16,
        "pcie_phy": 17,
        "msi":      18,
        "analyzer": 19
    }
    csr_map.update(SoCCore.csr_map)

    mem_map = SoCCore.mem_map
    mem_map["csr"] = 0x00000000

    usb_map = {
        "wishbone": 0,
        "tlp":      1
    }

    def __init__(self, platform):
        clk_freq = 125*1000000
        SoCCore.__init__(self, platform, clk_freq,
            cpu_type=None,
            shadow_base=0x00000000,
            csr_data_width=32,
            with_uart=False,
            ident="PCIe Injector example design",
            with_timer=False
        )
        self.submodules.crg = _CRG(platform)

        # pcie endpoint
        self.submodules.pcie_phy = S7PCIEPHY(platform, link_width=2)

        # uart bridge
        self.add_cpu_or_bridge(UARTWishboneBridge(platform.request("serial"), clk_freq, baudrate=115200))
        self.add_wb_master(self.cpu_or_bridge.wishbone)

        # usb core
        usb_pads = platform.request("usb_fifo")
        self.comb += [
            usb_pads.rst.eq(1),
            usb_pads.be.eq(0xf)
        ]
        self.submodules.usb_phy = FT245PHYSynchronous(usb_pads, 100*1000000)
        self.submodules.usb_core = USBCore(self.usb_phy, clk_freq)

        # usb <--> wishbone
        self.submodules.etherbone = Etherbone(self.usb_core, self.usb_map["wishbone"])
        self.add_wb_master(self.etherbone.master.bus)

        # usb <--> tlp
        self.submodules.tlp = TLP(self.usb_core, self.usb_map["tlp"])
        self.comb += [
            self.pcie_phy.source.connect(self.tlp.sender.sink),
            self.tlp.receiver.source.connect(self.pcie_phy.sink)
        ]

        # wishbone --> msi
        self.submodules.msi = MSI()
        self.comb += self.msi.source.connect(self.pcie_phy.interrupt)


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

        analyzer_signals = [
            self.pcie_phy.sink.valid,
            self.pcie_phy.sink.ready,
            self.pcie_phy.sink.last,
            self.pcie_phy.sink.dat,
            self.pcie_phy.sink.be
        ]

        self.submodules.analyzer = LiteScopeAnalyzer(analyzer_signals, 1024)

    def do_exit(self, vns):
        self.analyzer.export_csv(vns, "test/analyzer.csv")

def main():
    parser = argparse.ArgumentParser(description="PCIe Injector LiteX SoC")
    builder_args(parser)
    soc_core_args(parser)
    args = parser.parse_args()

    platform = pcie_injector.Platform()
    soc = PCIeInjectorSoC(platform, **soc_core_argdict(args))
    builder = Builder(soc, output_dir="build", csr_csv="test/csr.csv")
    vns = builder.build()
    soc.do_exit(vns)

    csr_header = cpu_interface.get_csr_header(soc.get_csr_regions(), soc.get_constants())
    write_to_file(os.path.join("software", "pcie", "kernel", "csr.h"), csr_header)

if __name__ == "__main__":
    main()
