#!/usr/bin/env python3
from litex.gen import *
from litex.gen.genlib.io import CRG

from litex.boards.platforms import kc705

class Blinky(Module):
    def __init__(self, platform):
        self.submodules.crg = CRG(platform.request("clk200"), platform.request("cpu_reset"))

        # led blink
        counter = Signal(32)
        self.sync += counter.eq(counter + 1)
        self.comb += [
            platform.request("user_led", 0).eq(counter[25]),
            platform.request("user_led", 1).eq(counter[26]),
            platform.request("user_led", 2).eq(1),
            platform.request("user_led", 3).eq(0),
        ]

def main():
    platform = kc705.Platform()
    blinky = Blinky(platform)
    platform.build(blinky)


if __name__ == "__main__":
    main()
