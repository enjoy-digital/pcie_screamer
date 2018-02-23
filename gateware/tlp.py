from migen import *

from litex.soc.interconnect import stream
from litex.soc.interconnect.stream import Converter, StrideConverter

import sys
sys.path.append("../")

from litepcie.common import phy_layout as tlp_description
from gateware.usb import user_description as usb_description


class TLPSender(Module):
    def __init__(self, identifier, fifo_depth=512):
        self.sink = sink = stream.Endpoint(tlp_description(64))
        self.source = source = stream.Endpoint(usb_description(32))

        # # #

        buf = stream.SyncFIFO(tlp_description(64), 128)
        converter = StrideConverter(tlp_description(64),
                                    tlp_description(32))
        fifo = stream.SyncFIFO(tlp_description(32), fifo_depth)
        self.submodules += buf, converter, fifo
        self.comb += [
                sink.connect(buf.sink),
                buf.source.connect(converter.sink),
                converter.source.connect(fifo.sink),
                fifo.sink.valid.eq(converter.source.valid &
                                   (converter.source.be == 0xf))
        ]

        level = Signal(max=fifo_depth)
        level_update = Signal()
        self.sync += If(level_update, level.eq(fifo.level))

        counter = Signal(max=fifo_depth)
        counter_reset = Signal()
        counter_ce = Signal()
        self.sync += \
            If(counter_reset,
                counter.eq(0)
            ).Elif(counter_ce,
                counter.eq(counter + 1)
            )

        self.submodules.fsm = fsm = FSM(reset_state="IDLE")
        fsm.act("IDLE",
            If(fifo.source.valid,
                level_update.eq(1),
                counter_reset.eq(1),
                NextState("SEND")
            )
        )
        fsm.act("SEND",
            source.valid.eq(fifo.source.valid),
            If(level == 0,
                source.last.eq(1),
            ).Else(
                source.last.eq(counter == (level-1)),
            ),
            source.dst.eq(identifier),
            If(level == 0,
                source.length.eq(4),
            ).Else(
                source.length.eq(4*level),
            ),
            source.data.eq(fifo.source.dat),
            fifo.source.ready.eq(source.ready),
            If(source.valid & source.ready,
                counter_ce.eq(1),
                If(source.last,
                    NextState("IDLE")
                )
            )
        )


class TLPReceiver(Module):
    def __init__(self):
        self.sink = sink = stream.Endpoint(usb_description(32))
        self.source = source = stream.Endpoint(tlp_description(64))

        # # #

        converter = Converter(32, 64, report_valid_token_count=True)
        self.submodules += converter

        self.comb += [
            converter.sink.valid.eq(self.sink.valid),
            converter.sink.last.eq(self.sink.last),
            self.sink.ready.eq(converter.sink.ready),
            converter.sink.data.eq(self.sink.data),

            source.valid.eq(converter.source.valid),
            source.last.eq(converter.source.last),
            If(converter.source.valid_token_count == 1,
                source.be.eq(0x0f)
            ).Else(
                source.be.eq(0xff)
            ),
            source.dat.eq(converter.source.data),
            converter.source.ready.eq(source.ready)
        ]


class TLP(Module):
    def __init__(self, usb_core, identifier):
        self.submodules.sender = sender = TLPSender(identifier)
        self.submodules.receiver = receiver = TLPReceiver()
        usb_port = usb_core.crossbar.get_port(identifier)
        self.comb += [
            sender.source.connect(usb_port.sink),
            usb_port.source.connect(receiver.sink)
        ]


if __name__ == '__main__':
        receiver = TLPReceiver()

        def generator(dut):
            yield dut.source.ready.eq(1)
            yield
            yield dut.sink.valid.eq(1)
            yield dut.sink.data.eq(0x01234567)
            yield
            yield dut.sink.valid.eq(1)
            yield dut.sink.data.eq(0x89abcdef)
            yield
            yield dut.sink.valid.eq(1)
            yield dut.sink.last.eq(1)
            yield dut.sink.data.eq(0x11112222)
            yield
            yield dut.sink.valid.eq(0)
            yield dut.sink.last.eq(0)
            yield dut.sink.data.eq(0)
            for i in range(16):
                yield

        run_simulation(receiver, generator(receiver), vcd_name="sim.vcd")
