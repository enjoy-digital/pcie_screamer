#!/usr/bin/env python3
import argparse
import os

from litex.gen import *
from litex.gen.genlib.io import CRG
from litex.gen.genlib.resetsync import AsyncResetSynchronizer
from litex.gen.fhdl.specials import Keep

import platform as pcie_injector

from litex.soc.integration.soc_core import *
from litex.soc.integration.builder import *
from litex.soc.interconnect import stream
from litex.soc.cores.uart.bridge import UARTWishboneBridge

from liteusb.phy.ft245 import phy_description, FT245PHYSynchronous

from gateware.usb import USBCore
from gateware.etherbone import Etherbone
from gateware.tlp import TLP

from litescope import LiteScopeAnalyzer


class BaseSoC(SoCCore):
    csr_map = {
        "analyzer": 16
    }
    csr_map.update(SoCCore.csr_map)

    usb_map = {
        "wishbone": 0,
        "tlp":      1,
    }

    def __init__(self, platform):
        clk_freq = 100*1000000
        SoCCore.__init__(self, platform, clk_freq,
            cpu_type=None,
            csr_data_width=32,
            with_uart=False,
            ident="USB loopback example design",
            with_timer=False,
            integrated_main_ram_size=0x1000
        )
        self.add_cpu_or_bridge(UARTWishboneBridge(platform.request("serial"), clk_freq, baudrate=115200))
        self.add_wb_master(self.cpu_or_bridge.wishbone)
        self.submodules.crg = CRG(platform.request("usb_fifo_clock"))
        self.clock_domains.cd_usb = ClockDomain()
        self.comb += [
            self.cd_usb.clk.eq(ClockSignal()),
            self.cd_usb.rst.eq(ResetSignal())
        ]

        # led blink
        counter = Signal(32)
        self.sync += counter.eq(counter + 1)
        self.comb += [
            platform.request("user_led", 0).eq(counter[26] & platform.request("user_btn", 0)),
            platform.request("user_led", 1).eq(counter[27] & platform.request("user_btn", 1))
        ]

        # usb core,
        usb_pads = platform.request("usb_fifo")
        self.comb += [
            usb_pads.rst.eq(1),
            usb_pads.be.eq(0xf)
        ]
        self.submodules.usb_phy = FT245PHYSynchronous(usb_pads, 100*1000000)
        self.submodules.usb_core = USBCore(self.usb_phy, clk_freq)
        self.platform.add_period_constraint(self.cd_usb.clk, 10.0)

        # usb <--> wishbone
        self.submodules.etherbone = Etherbone(self.usb_core, self.usb_map["wishbone"])
        self.add_wb_master(self.etherbone.master.bus)

        # usb <--> tlp
        self.submodules.tlp = TLP(self.usb_core, self.usb_map["tlp"])
        self.comb += self.tlp.receiver.source.connect(self.tlp.sender.sink) # loopback

        # analyzer
        analyzer_signals = [
            self.usb_phy.source.valid,
            self.usb_phy.source.ready,
            self.usb_phy.source.data,

            self.usb_phy.sink.valid,
            self.usb_phy.sink.ready,
            self.usb_phy.sink.data,

            #self.etherbone.master.bus.adr,
            #self.etherbone.master.bus.dat_w,
            #self.etherbone.master.bus.dat_r,
            self.etherbone.master.bus.sel,
            self.etherbone.master.bus.cyc,
            self.etherbone.master.bus.stb,
            self.etherbone.master.bus.ack,
            self.etherbone.master.bus.we,
            self.etherbone.master.bus.cti,
            self.etherbone.master.bus.bte,
            self.etherbone.master.bus.err,
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
    soc = BaseSoC(platform, **soc_core_argdict(args))
    builder = Builder(soc, output_dir="build", csr_csv="test/csr.csv")
    vns = builder.build()
    soc.do_exit(vns)

if __name__ == "__main__":
    main()
