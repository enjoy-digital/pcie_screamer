# This file is Copyright (c) 2016 Florent Kermarrec <florent@enjoy-digital.fr>
# License: BSD

from collections import OrderedDict

from migen.genlib.misc import WaitTimer

from litex.soc.interconnect import stream
from litex.soc.interconnect.stream import EndpointDescription
from litex.soc.interconnect.stream_packet import *
from litex.soc.interconnect.wishbonebridge import WishboneStreamingBridge


packet_header_length = 12
packet_header_fields = {
    "preamble": HeaderField(0,  0, 32),
    "dst":      HeaderField(4,  0, 32),
    "length":   HeaderField(8,  0, 32)
}
packet_header = Header(packet_header_fields,
                       packet_header_length,
                       swap_field_bytes=True)


def phy_description(dw):
    payload_layout = [("data", dw)]
    return EndpointDescription(payload_layout)


def packet_description(dw):
    param_layout = packet_header.get_layout()
    payload_layout = [
        ("data", dw),
        ("error", dw//8)
    ]
    return EndpointDescription(payload_layout, param_layout)


def user_description(dw):
    param_layout = [
        ("dst",    8),
        ("length", 32)
    ]
    payload_layout = [
        ("data", dw),
        ("error", dw//8)
    ]
    return EndpointDescription(payload_layout, param_layout)


class USBMasterPort:
    def __init__(self, dw):
        self.source = stream.Endpoint(user_description(dw))
        self.sink = stream.Endpoint(user_description(dw))


class USBSlavePort:
    def __init__(self, dw, tag):
        self.sink = stream.Endpoint(user_description(dw))
        self.source = stream.Endpoint(user_description(dw))
        self.tag = tag


class USBUserPort(USBSlavePort):
    def __init__(self, dw, tag):
        USBSlavePort.__init__(self, dw, tag)


class USBPacketizer(Module):
    def __init__(self):
        self.sink = sink = stream.Endpoint(user_description(32))
        self.source = source = stream.Endpoint(phy_description(32))

        # # #

        # Packet description
        #   - preamble : 4 bytes
        #   - unused   : 3 bytes
        #   - dst      : 1 byte
        #   - length   : 4 bytes
        #   - payload
        header = [
            # preamble
            0x5aa55aa5,
            # dst
            sink.dst,
            # length
            sink.length
        ]

        header_unpack = stream.Unpack(len(header), phy_description(32))
        self.submodules += header_unpack

        for i, byte in enumerate(header):
            chunk = getattr(header_unpack.sink.payload, "chunk" + str(i))
            self.comb += chunk.data.eq(byte)

        fsm = FSM(reset_state="IDLE")
        self.submodules += fsm

        fsm.act("IDLE",
            If(sink.valid,
                NextState("INSERT_HEADER")
            )
        )

        fsm.act("INSERT_HEADER",
            header_unpack.sink.valid.eq(1),
            source.valid.eq(1),
            source.data.eq(header_unpack.source.data),
            header_unpack.source.ready.eq(source.ready),
            If(header_unpack.sink.ready,
                NextState("COPY")
            )
        )

        fsm.act("COPY",
            source.valid.eq(sink.valid),
            source.data.eq(sink.data),
            sink.ready.eq(source.ready),
            If(source.valid & source.ready & sink.last,
                NextState("IDLE")
            )
        )


class USBDepacketizer(Module):
    def __init__(self, clk_freq, timeout=10):
        self.sink = sink = stream.Endpoint(phy_description(32))
        self.source = source = stream.Endpoint(user_description(32))

        # # #

        # Packet description
        #   - preamble : 4 bytes
        #   - unused   : 3 bytes
        #   - dst      : 1 byte
        #   - length   : 4 bytes
        #   - payload
        preamble = Signal(32)

        header = [
            # dst
            source.dst,
            # length
            source.length
        ]

        header_pack = ResetInserter()(stream.Pack(phy_description(32), len(header)))
        self.submodules += header_pack

        for i, byte in enumerate(header):
            chunk = getattr(header_pack.source.payload, "chunk" + str(i))
            self.comb += byte.eq(chunk.data)

        fsm = FSM(reset_state="IDLE")
        self.submodules += fsm

        self.comb += preamble.eq(sink.data)
        fsm.act("IDLE",
            sink.ready.eq(1),
            If((sink.data == 0x5aa55aa5) & sink.valid,
                   NextState("RECEIVE_HEADER")
            ),
            header_pack.source.ready.eq(1)
        )

        self.submodules.timer = WaitTimer(clk_freq*timeout)
        self.comb += self.timer.wait.eq(~fsm.ongoing("IDLE"))

        fsm.act("RECEIVE_HEADER",
            header_pack.sink.valid.eq(sink.valid),
            header_pack.sink.payload.eq(sink.payload),
            If(self.timer.done,
                NextState("IDLE")
            ).Elif(header_pack.source.valid,
                NextState("COPY")
            ).Else(
                sink.ready.eq(1)
            )
        )

        self.comb += header_pack.reset.eq(self.timer.done)

        last = Signal()
        cnt = Signal(32)

        fsm.act("COPY",
            source.valid.eq(sink.valid),
            source.last.eq(last),
            source.data.eq(sink.data),
            sink.ready.eq(source.ready),
            If((source.valid & source.ready & last) | self.timer.done,
                NextState("IDLE")
            )
        )

        self.sync += \
            If(fsm.ongoing("IDLE"),
                cnt.eq(0)
            ).Elif(source.valid & source.ready,
                cnt.eq(cnt + 1)
            )
        self.comb += last.eq(cnt == source.length[2:] - 1)


class USBCrossbar(Module):
    def __init__(self):
        self.users = OrderedDict()
        self.master = USBMasterPort(32)
        self.dispatch_param = "dst"

    def get_port(self, dst):
        port = USBUserPort(32, dst)
        if dst in self.users.keys():
            raise ValueError("Destination {0:#x} already assigned".format(dst))
        self.users[dst] = port
        return port

    def do_finalize(self):
        # TX arbitrate
        sinks = [port.sink for port in self.users.values()]
        self.submodules.arbiter = Arbiter(sinks, self.master.source)

        # RX dispatch
        sources = [port.source for port in self.users.values()]
        self.submodules.dispatcher = Dispatcher(self.master.sink,
                                                sources,
                                                one_hot=True)
        cases = {}
        cases["default"] = self.dispatcher.sel.eq(0)
        for i, (k, v) in enumerate(self.users.items()):
            cases[k] = self.dispatcher.sel.eq(2**i)
        self.comb += \
            Case(getattr(self.master.sink, self.dispatch_param), cases)


class USBCore(Module):
    def __init__(self, phy, clk_freq):
        rx_pipeline = [phy]
        tx_pipeline = [phy]

        # depacketizer / packetizer
        self.submodules.depacketizer = USBDepacketizer(clk_freq)
        self.submodules.packetizer = USBPacketizer()
        rx_pipeline += [self.depacketizer]
        tx_pipeline += [self.packetizer]

        # crossbar
        self.submodules.crossbar = USBCrossbar()
        rx_pipeline += [self.crossbar.master]
        tx_pipeline += [self.crossbar.master]

        # graph
        self.submodules.rx_pipeline = stream.Pipeline(*rx_pipeline)
        self.submodules.tx_pipeline = stream.Pipeline(*reversed(tx_pipeline))
