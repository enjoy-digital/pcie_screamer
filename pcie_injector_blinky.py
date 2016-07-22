#!/usr/bin/env python3
from litex.gen import *
from litex.gen.genlib.io import CRG

import pcie_injector_platform as pcie_injector

class Blinky(Module):
    def __init__(self, platform):
        self.submodules.crg = CRG(platform.request("clk100"))

        # led blink
        counter = Signal(32)
        self.sync += counter.eq(counter + 1)
        self.comb += [
            platform.request("user_led", 0).eq(counter[25]),
            platform.request("user_led", 1).eq(counter[26])
        ]

def main():
    platform = pcie_injector.Platform()
    blinky = Blinky(platform)
    platform.build(blinky)


if __name__ == "__main__":
    main()
