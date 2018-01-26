#!/usr/bin/env python3
from litex.gen import *
from litex.gen.genlib.resetsync import AsyncResetSynchronizer

from litex.build.generic_platform import *
from litex.build.xilinx import XilinxPlatform

from litex.soc.interconnect.csr import *
from litex.soc.integration.soc_sdram import *
from litex.soc.integration.builder import *
from litex.soc.interconnect import stream
from litex.soc.cores.uart import UARTWishboneBridge

from litedram.modules import MT41K256M16
from litedram.phy import a7ddrphy

from litepcie.phy.s7pciephy import S7PCIEPHY

from liteusb.phy.ft245 import phy_description, FT245PHYSynchronous
from gateware.usb import USBCore
from gateware.etherbone import Etherbone
from gateware.tlp import TLP
from gateware.msi import MSI

from litescope import LiteScopeAnalyzer


_io = [
    ("clk100", 0, Pins("R4"), IOStandard("LVCMOS33")),

    ("user_led", 0, Pins("AB1"), IOStandard("LVCMOS33")),
    ("user_led", 1, Pins("AB8"), IOStandard("LVCMOS33")),

    ("user_btn", 0, Pins("AA1"), IOStandard("LVCMOS33")),
    ("user_btn", 1, Pins("AB6"), IOStandard("LVCMOS33")),

    ("serial", 0,
        Subsignal("tx", Pins("T1")),
        Subsignal("rx", Pins("U1")),
        IOStandard("LVCMOS33"),
    ),

    ("ddram", 0,
        Subsignal("a", Pins(
            "M2 M5 M3 M1 L6 P1 N3 N2",
            "M6 R1 L5 N5 N4 P2 P6"),
            IOStandard("SSTL15")),
        Subsignal("ba", Pins("L3 K6 L4"), IOStandard("SSTL15")),
        Subsignal("ras_n", Pins("J4"), IOStandard("SSTL15")),
        Subsignal("cas_n", Pins("K3"), IOStandard("SSTL15")),
        Subsignal("we_n", Pins("L1"), IOStandard("SSTL15")),
        Subsignal("dm", Pins("G3 F1"), IOStandard("SSTL15")),
        Subsignal("dq", Pins(
            "G2 H4 H5 J1 K1 H3 H2 J5",
            "E3 B2 F3 D2 C2 A1 E2 B1"),
            IOStandard("SSTL15"),
            Misc("IN_TERM=UNTUNED_SPLIT_50")),
        Subsignal("dqs_p", Pins("K2 E1"), IOStandard("DIFF_SSTL15")),
        Subsignal("dqs_n", Pins("J2 D1"), IOStandard("DIFF_SSTL15")),
        Subsignal("clk_p", Pins("P5"), IOStandard("DIFF_SSTL15")),
        Subsignal("clk_n", Pins("P4"), IOStandard("DIFF_SSTL15")),
        Subsignal("cke", Pins("J6"), IOStandard("SSTL15")),
        Subsignal("odt", Pins("K4"), IOStandard("SSTL15")),
        Subsignal("reset_n", Pins("G1"), IOStandard("SSTL15")),
        Misc("SLEW=FAST"),
    ),

    ("pcie_x1", 0,
        Subsignal("rst_n", Pins("AB7"), IOStandard("LVCMOS33")),
        Subsignal("clk_p", Pins("F6")),
        Subsignal("clk_n", Pins("E6")),
        Subsignal("rx_p", Pins("B10")),
        Subsignal("rx_n", Pins("A10")),
        Subsignal("tx_p", Pins("B6")),
        Subsignal("tx_n", Pins("A6"))
    ),

    ("usb_fifo_clock", 0, Pins("D17"), IOStandard("LVCMOS33")),
    ("usb_fifo", 0,
        Subsignal("rst", Pins("K22")),
        Subsignal("data", Pins("A16 F14 A15 F13 A14 E14 A13 E13 B13 C15 C13 C14 B16 E17 B15 F16",
                               "A20 E18 B20 F18 D19 D21 E19 E21 A21 B21 A19 A18 F20 F19 B18 B17")),
        Subsignal("be", Pins("K16 L16 G20 H20")),
        Subsignal("rxf_n", Pins("M13")),
        Subsignal("txe_n", Pins("L13")),
        Subsignal("rd_n", Pins("K19")),
        Subsignal("wr_n", Pins("M15")),
        Subsignal("oe_n", Pins("K18")),
        Subsignal("siwua", Pins("M16")),
        IOStandard("LVCMOS33"), Misc("SLEW=FAST")
    ),
]


class Platform(XilinxPlatform):
    default_clk_name = "clk100"
    default_clk_period = 10.0

    def __init__(self, toolchain="vivado", programmer="vivado"):
        XilinxPlatform.__init__(self, "xc7a35t-fgg484-2", _io,
                                toolchain=toolchain)
        self.toolchain.bitstream_commands = \
            ["set_property BITSTREAM.CONFIG.SPI_BUSWIDTH 4 [current_design]",
             "set_property BITSTREAM.CONFIG.CONFIGRATE 40 [current_design]"]
        self.toolchain.additional_commands = \
            ["write_cfgmem -force -format bin -interface spix4 -size 16 "
             "-loadbit \"up 0x0 {build_name}.bit\" -file {build_name}.bin"]
        self.programmer = programmer
        self.add_platform_command("set_property INTERNAL_VREF 0.750 [get_iobanks 35]")


    def do_finalize(self, fragment):
        self.add_platform_command("""
create_clock -name pcie_clk -period 10 [get_pins {{pcie_phy/pcie_support_i/pcie_i/inst/inst/gt_top_i/pipe_wrapper_i/pipe_lane[0].gt_wrapper_i/gtp_channel.gtpe2_channel_i/TXOUTCLK}}]
""")
        XilinxPlatform.do_finalize(self, fragment)


class _CRG(Module, AutoCSR):
    def __init__(self, platform):
        self.clock_domains.cd_sys = ClockDomain("sys")
        self.clock_domains.cd_sys4x = ClockDomain(reset_less=True)
        self.clock_domains.cd_sys4x_dqs = ClockDomain(reset_less=True)
        self.clock_domains.cd_clk200 = ClockDomain()

        self.clock_domains.cd_usb = ClockDomain()

        # usb clock domain (100MHz from usb)
        self.comb += self.cd_usb.clk.eq(platform.request("usb_fifo_clock"))
        self.specials += AsyncResetSynchronizer(self.cd_usb, ResetSignal("pcie"))

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

    def __init__(self, platform, with_cpu=False, with_analyzer=True, with_loopback=False):
        clk_freq = int(100e6)
        SoCSDRAM.__init__(self, platform, clk_freq,
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
            self.add_cpu_or_bridge(UARTWishboneBridge(platform.request("serial"), clk_freq, baudrate=3000000))
            self.add_wb_master(self.cpu_or_bridge.wishbone)

        # sdram
        self.submodules.ddrphy = a7ddrphy.A7DDRPHY(platform.request("ddram"))
        self.add_constant("READ_LEVELING_BITSLIP", 2)
        self.add_constant("READ_LEVELING_DELAY", 8)
        sdram_module = MT41K256M16(self.clk_freq, "1:4")
        self.register_sdram(self.ddrphy,
                            sdram_module.geom_settings,
                            sdram_module.timing_settings)

        # pcie endpoint
        self.submodules.pciephy = S7PCIEPHY(platform, platform.request("pcie_x1"), cd="sys")

        # usb core
        usb_pads = platform.request("usb_fifo")
        self.comb += [
            usb_pads.rst.eq(1),
            usb_pads.siwua.eq(1),
            usb_pads.be.eq(0xf)
        ]
        self.submodules.usb_phy = FT245PHYSynchronous(usb_pads, clk_freq)

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
        self.platform.add_period_constraint(self.platform.lookup_request("pcie_x1").clk_p, 10.0)

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
            self.submodules.analyzer = LiteScopeAnalyzer(analyzer_signals, 1024, cd="sys")

    def do_exit(self, vns):
        if hasattr(self, "analyzer"):
            self.analyzer.export_csv(vns, "test/analyzer.csv")


def main():
    platform = Platform()
    soc = PCIeInjectorSoC(platform)
    builder = Builder(soc, output_dir="build", csr_csv="test/csr.csv")
    vns = builder.build()
    soc.do_exit(vns)


if __name__ == "__main__":
    main()
