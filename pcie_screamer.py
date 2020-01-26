#!/usr/bin/env python3

# This file is Copyright (c) 2016-2020 Florent Kermarrec <florent@enjoy-digital.fr>
# This file is Copyright (c) 2018-2019 Pierre-Olivier Vauboin <po@lambdaconcept.com>
# License: BSD

import argparse

from migen import *
from migen.genlib.resetsync import AsyncResetSynchronizer

from litex.build.generic_platform import *

from litex.soc.cores.clock import *
from litex.soc.integration.soc_core import *
from litex.soc.integration.builder import *
from litex.soc.interconnect import stream
from litex.soc.cores.uart import UARTWishboneBridge
from litex.soc.cores.usb_fifo import phy_description

from litepcie.phy.s7pciephy import S7PCIEPHY

from gateware.usb import USBCore
from gateware.etherbone import Etherbone
from gateware.tlp import TLP
from gateware.msi import MSI
from gateware.ft601 import FT601Sync

from litescope import LiteScopeAnalyzer

# CRG ----------------------------------------------------------------------------------------------

class _CRG(Module):
    def __init__(self, platform, sys_clk_freq):
        self.clock_domains.cd_sys = ClockDomain()
        self.clock_domains.cd_usb = ClockDomain()

        # # #

        # sys
        sys_clk_100 = platform.request("clk100")
        platform.add_period_constraint(sys_clk_100, 1e9/100e6)
        self.submodules.pll = pll = S7PLL(speedgrade=-1)
        pll.register_clkin(sys_clk_100, 100e6)
        pll.create_clkout(self.cd_sys, sys_clk_freq)

        # usb
        usb_clk100 = platform.request("usb_fifo_clock")
        platform.add_period_constraint(usb_clk100, 1e9/100e6)
        self.comb += self.cd_usb.clk.eq(usb_clk100)
        self.specials += AsyncResetSynchronizer(self.cd_usb, ResetSignal("pcie"))

# PCIeScreamer -------------------------------------------------------------------------------------

class PCIeScreamer(SoCMini):
    usb_map = {
        "wishbone": 0,
        "tlp":      1
    }

    def __init__(self, platform, with_analyzer=True, with_loopback=False):
        sys_clk_freq = int(100e6)

        # SoCMini ----------------------------------------------------------------------------------
        SoCMini.__init__(self, platform, sys_clk_freq, ident="PCIe Screamer", ident_version=True)

        # CRG --------------------------------------------------------------------------------------
        self.submodules.crg = _CRG(platform, sys_clk_freq)

        # Serial Wishbone Bridge -------------------------------------------------------------------
        self.submodules.bridge = UARTWishboneBridge(platform.request("serial"), sys_clk_freq, baudrate=3e6)
        self.add_wb_master(self.bridge.wishbone)

        # PCIe PHY ---------------------------------------------------------------------------------
        self.submodules.pcie_phy = S7PCIEPHY(platform, platform.request("pcie_x1"))
        self.add_csr("pcie_phy")

        # USB FT601 PHY ----------------------------------------------------------------------------
        self.submodules.usb_phy = FT601Sync(platform.request("usb_fifo"), dw=32, timeout=1024)

        # USB Loopback -----------------------------------------------------------------------------
        if with_loopback:
            self.submodules.usb_loopback_fifo = stream.SyncFIFO(phy_description(32), 2048)
            self.comb += [
                self.usb_phy.source.connect(self.usb_loopback_fifo.sink),
                self.usb_loopback_fifo.source.connect(self.usb_phy.sink)
            ]
        # USB Core ---------------------------------------------------------------------------------
        else:
            self.submodules.usb_core = USBCore(self.usb_phy, sys_clk_freq)

            # USB <--> Wishbone --------------------------------------------------------------------
            self.submodules.etherbone = Etherbone(self.usb_core, self.usb_map["wishbone"])
            self.add_wb_master(self.etherbone.master.bus)

            # USB <--> TLP -------------------------------------------------------------------------
            self.submodules.tlp = TLP(self.usb_core, self.usb_map["tlp"])
            self.comb += [
                self.pcie_phy.source.connect(self.tlp.sender.sink),
                self.tlp.receiver.source.connect(self.pcie_phy.sink)
            ]

        # Wishbone --> MSI -------------------------------------------------------------------------
        self.submodules.msi = MSI()
        self.comb += self.msi.source.connect(self.pcie_phy.msi)
        self.add_csr("msi")

        # Led blink --------------------------------------------------------------------------------
        usb_counter = Signal(32)
        self.sync.usb += usb_counter.eq(usb_counter + 1)
        self.comb += platform.request("user_led", 0).eq(usb_counter[26])

        pcie_counter = Signal(32)
        self.sync.pcie += pcie_counter.eq(pcie_counter + 1)
        self.comb += platform.request("user_led", 1).eq(pcie_counter[26])

        # Analyzer ---------------------------------------------------------------------------------
        if with_analyzer:
            analyzer_signals = [
                self.pcie_phy.sink,
                self.pcie_phy.source,
            ]
            self.submodules.analyzer = LiteScopeAnalyzer(analyzer_signals, 1024, csr_csv="test/analyzer.csv")
            self.add_csr("analyzer")

# Build --------------------------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="PCIe Screamer Test Gateware")
    parser.add_argument("--m2",            action="store_true", help="use M2 variant of PCIe Screamer")
    parser.add_argument("--with-analyzer", action="store_true", help="enable Analyzer")
    parser.add_argument("--with-loopback", action="store_true", help="enable USB Loopback")
    parser.add_argument("--build", action="store_true", help="Build bitstream")
    parser.add_argument("--load",  action="store_true", help="Load bitstream")
    parser.add_argument("--flash", action="store_true", help="Flash bitstream")
    args = parser.parse_args()

    if args.m2:
        from platforms.pcie_screamer_m2 import Platform
    else:
        from platforms.pcie_screamer import Platform
    platform = Platform()
    soc      = PCIeScreamer(platform, args.with_analyzer, args.with_loopback)
    builder  = Builder(soc, output_dir="build", csr_csv="test/csr.csv")
    builder.build(run=args.build)

    if args.load:
        from litex.build.openocd import OpenOCD
        prog = OpenOCD("openocd/openocd.cfg")
        prog.load_bitstream("build/gateware/top.bit")

    if args.flash:
        from litex.build.openocd import OpenOCD
        prog = OpenOCD("openocd/openocd.cfg",
            flash_proxy_basename="openocd/bscan_spi_xc7a35t.bit")
        prog.set_flash_proxy_dir(".")
        prog.flash(0, "build/gateware/top.bin")

if __name__ == "__main__":
    main()
