import math

from litex.gen import *
from litex.gen.fhdl.specials import Tristate
from litex.gen.genlib.cdc import MultiReg

from litex.soc.interconnect import stream
from litex.soc.interconnect.stream_packet import *


def phy_description(dw):
    payload_layout = [("data", dw)]
    return stream.EndpointDescription(payload_layout)

def anti_starvation(module, timeout):
        en = Signal()
        max_time = Signal()
        if timeout:
            t = timeout - 1
            time = Signal(max=t+1)
            module.comb += max_time.eq(time == 0)
            module.sync += If(~en,
                    time.eq(t)
                ).Elif(~max_time,
                    time.eq(time - 1)
                )
        else:
            module.comb += max_time.eq(0)
        return en, max_time


class FT245PHYSynchronous(Module):
    def __init__(self, pads, clk_freq,
                 fifo_depth=32,
                 read_time=128,
                 write_time=128):
        dw = len(pads.data)

        # read fifo (FTDI --> SoC)
        read_fifo = ClockDomainsRenamer({"write": "usb", "read": "sys"})(stream.AsyncFIFO(phy_description(32), fifo_depth))
        read_buffer = ClockDomainsRenamer("usb")(stream.SyncFIFO(phy_description(32), 4))
        self.comb += read_buffer.source.connect(read_fifo.sink)

        # write fifo (SoC --> FTDI)
        write_fifo = ClockDomainsRenamer({"write": "sys", "read": "usb"})(stream.AsyncFIFO(phy_description(32), fifo_depth))

        self.submodules += read_fifo, read_buffer, write_fifo

        # sink / source interfaces
        self.sink = write_fifo.sink
        self.source = read_fifo.source

        # read / write arbitration
        wants_write = Signal()
        wants_read = Signal()

        txe_n = Signal()
        rxf_n = Signal()

        self.comb += [
            txe_n.eq(pads.txe_n),
            rxf_n.eq(pads.rxf_n),
            wants_write.eq(~txe_n & write_fifo.source.valid),
            wants_read.eq(~rxf_n & read_fifo.sink.ready),
        ]

        read_time_en, max_read_time = anti_starvation(self, read_time)
        write_time_en, max_write_time = anti_starvation(self, write_time)

        data_w_accepted = Signal(reset=1)

        fsm = FSM(reset_state="READ")
        self.submodules += ClockDomainsRenamer("usb")(fsm)

        fsm.act("READ",
            read_time_en.eq(1),
            If(wants_write,
                If(~wants_read | max_read_time,
                    NextState("RTW")
                )
            )
        )
        fsm.act("RTW",
            NextState("WRITE")
        )
        fsm.act("WRITE",
            write_time_en.eq(1),
            If(wants_read,
                If(~wants_write | max_write_time,
                    NextState("WTR")
                )
            ),
            write_fifo.source.ready.eq(wants_write & data_w_accepted)
        )
        fsm.act("WTR",
            NextState("READ")
        )

        # databus tristate
        data_w = Signal(dw)
        data_r = Signal(dw)
        data_oe = Signal()
        self.specials += Tristate(pads.data, data_w, data_oe, data_r)

        # read / write actions
        pads.oe_n.reset = 1
        pads.rd_n.reset = 1
        pads.wr_n.reset = 1

        self.sync.usb += [
            If(fsm.ongoing("READ"),
                data_oe.eq(0),

                pads.oe_n.eq(0),
                pads.rd_n.eq(~wants_read),
                pads.wr_n.eq(1)

            ).Elif(fsm.ongoing("WRITE"),
                data_oe.eq(1),

                pads.oe_n.eq(1),
                pads.rd_n.eq(1),
                pads.wr_n.eq(~wants_write),

                data_w_accepted.eq(~txe_n)

            ).Else(
                data_oe.eq(1),

                pads.oe_n.eq(~fsm.ongoing("WTR")),
                pads.rd_n.eq(1),
                pads.wr_n.eq(1)
            ),
                read_buffer.sink.valid.eq(~pads.rd_n & ~rxf_n),
                read_buffer.sink.data.eq(data_r),
                If(~txe_n & data_w_accepted,
                    data_w.eq(write_fifo.source.data)
                )
        ]
