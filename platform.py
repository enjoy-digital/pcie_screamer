from litex.build.generic_platform import *
from litex.build.xilinx import XilinxPlatform, VivadoProgrammer

_io = [
    ("clk100", 0, Pins("D17"), IOStandard("LVCMOS33")),

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
        Subsignal("rx_p", Pins("B8")),
        Subsignal("rx_n", Pins("A8")),
        Subsignal("tx_p", Pins("B4")),
        Subsignal("tx_n", Pins("A4"))
    ),

    ("pcie_x2", 0,
        Subsignal("rst_n", Pins("AB7"), IOStandard("LVCMOS33")),
        Subsignal("clk_p", Pins("F6")),
        Subsignal("clk_n", Pins("E6")),
        Subsignal("rx_p", Pins("B8 D11")),
        Subsignal("rx_n", Pins("A8 C11")),
        Subsignal("tx_p", Pins("B4 D5")),
        Subsignal("tx_n", Pins("A4 C5"))
    ),

    ("usb_fifo_clock", 0, Pins("D17"), IOStandard("LVCMOS33")),
    ("usb_fifo_rst", 0, Pins("K22"), IOStandard("LVCMOS33")),
    ("usb_fifo", 0,
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
        XilinxPlatform.__init__(self, "xc7a50t-fgg484-1", _io,
                                toolchain=toolchain)
        self.toolchain.bitstream_commands = \
            ["set_property BITSTREAM.CONFIG.SPI_BUSWIDTH 4 [current_design]"]
        self.toolchain.additional_commands = \
            ["write_cfgmem -force -format bin -interface spix4 -size 16 "
             "-loadbit \"up 0x0 {build_name}.bit\" -file {build_name}.bin"]
        self.programmer = programmer
        self.add_platform_command("set_property INTERNAL_VREF 0.750 [get_iobanks 35]")


    def create_programmer(self):
        if self.programmer == "vivado":
            return VivadoProgrammer(flash_part="n25q128-3.3v-spi-x1_x2_x4")
        else:
            raise ValueError("{} programmer is not supported"
                             .format(self.programmer))

    def do_finalize(self, fragment):
        XilinxPlatform.do_finalize(self, fragment)
