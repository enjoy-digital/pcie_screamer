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

from gateware.ft245 import phy_description, FT245PHYSynchronous

from litescope import LiteScopeAnalyzer


class BaseSoC(SoCCore):
    csr_map = {
        "analyzer": 16
    }
    csr_map.update(SoCCore.csr_map)
    def __init__(self, platform):
        clk_freq = 100*1000000
        SoCCore.__init__(self, platform, clk_freq,
            cpu_type=None,
            csr_data_width=32,
            with_uart=False,
            ident="USB loopback example design",
            with_timer=False
        )
        self.add_cpu_or_bridge(UARTWishboneBridge(platform.request("serial"), clk_freq, baudrate=115200))
        self.add_wb_master(self.cpu_or_bridge.wishbone)
        clk100 = platform.request("usb_fifo_clock")
        self.submodules.crg = CRG(clk100)

        # led blink
        counter = Signal(32)
        self.sync += counter.eq(counter + 1)
        self.comb += [
            platform.request("user_led", 0).eq(counter[26] & platform.request("user_btn", 0)),
            platform.request("user_led", 1).eq(counter[27] & platform.request("user_btn", 1))
        ]

        # usb
        self.comb += platform.request("usb_fifo_rst").eq(1)
        usb_pads = platform.request("usb_fifo")
        self.comb += usb_pads.be.eq(0xf)

        self.submodules.usb_phy = FT245PHYSynchronous(usb_pads, 100*1000000)
        self.submodules.usb_loopback_fifo = stream.SyncFIFO(phy_description(32), 8192)
        self.comb += [
            self.usb_phy.source.connect(self.usb_loopback_fifo.sink),
            self.usb_loopback_fifo.source.connect(self.usb_phy.sink)
        ]

        self.platform.add_period_constraint(clk100, 10.0)

        # analyzer
        analyzer_signals = [
            self.usb_phy.source.valid,
            self.usb_phy.source.ready,
            self.usb_phy.source.data,

            self.usb_phy.sink.valid,
            self.usb_phy.sink.ready,
            self.usb_phy.sink.data,

            usb_pads.data,
            usb_pads.be,
            usb_pads.rxf_n,
            usb_pads.txe_n,
            usb_pads.rd_n,
            usb_pads.wr_n,
            usb_pads.oe_n,
            usb_pads.siwua,
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
