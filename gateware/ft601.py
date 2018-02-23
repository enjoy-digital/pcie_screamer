#!/usr/bin/env python3
from migen import *
from migen.fhdl.specials import Tristate

from litex.soc.interconnect import stream

from liteusb.phy.ft245 import phy_description

class FT601Sync(Module):
    def __init__(self, pads, dw=32, timeout=1024):
        read_fifo = ClockDomainsRenamer({"write": "usb", "read": "sys"})(stream.AsyncFIFO(phy_description(dw), 128))
        write_fifo = ClockDomainsRenamer({"write": "sys", "read": "usb"})(stream.AsyncFIFO(phy_description(dw), 128))

        read_buffer = ClockDomainsRenamer("usb")(stream.SyncFIFO(phy_description(dw), 4))
        self.comb += read_buffer.source.connect(read_fifo.sink)

        self.submodules += read_fifo
        self.submodules += read_buffer
        self.submodules += write_fifo

        self.read_buffer = read_buffer

        self.sink = write_fifo.sink
        self.source = read_fifo.source

        self.tdata_w = tdata_w = Signal(dw)
        self.data_r = data_r = Signal(dw)
        self.data_oe = data_oe = Signal()
        self.specials += Tristate(pads.data, tdata_w, data_oe, data_r)

        data_w = Signal(dw)
        _data_w = Signal(dw)
        self.sync.usb += [
            _data_w.eq(data_w)
        ]
        for i in range(dw):
            self.specials += [
                Instance("ODDR",
                         p_DDR_CLK_EDGE="SAME_EDGE",
                         i_C=ClockSignal("usb"), i_CE=1, i_S=0, i_R=0,
                         i_D1=_data_w[i], i_D2=data_w[i], o_Q=tdata_w[i]
                )
            ]

        self.rd_n = rd_n = Signal()
        _rd_n = Signal(reset=1)
        self.wr_n = wr_n = Signal()
        _wr_n = Signal(reset=1)
        self.oe_n = oe_n = Signal()
        _oe_n = Signal(reset=1)
        self.sync.usb += [
            _rd_n.eq(rd_n),
            _wr_n.eq(wr_n),
            _oe_n.eq(oe_n),
        ]
        self.specials += [
            Instance("ODDR",
                     p_DDR_CLK_EDGE="SAME_EDGE",
                     i_C=ClockSignal("usb"), i_CE=1, i_S=0, i_R=0,
                     i_D1=_rd_n, i_D2=rd_n, o_Q=pads.rd_n
            ),
            Instance("ODDR",
                     p_DDR_CLK_EDGE="SAME_EDGE",
                     i_C=ClockSignal("usb"), i_CE=1, i_S=0, i_R=0,
                     i_D1=_wr_n, i_D2=wr_n, o_Q=pads.wr_n
            ),
            Instance("ODDR",
                     p_DDR_CLK_EDGE="SAME_EDGE",
                     i_C=ClockSignal("usb"), i_CE=1, i_S=0, i_R=0,
                     i_D1=_oe_n, i_D2=oe_n, o_Q=pads.oe_n
            )
        ]

        self.comb += [
            pads.rst.eq(~ResetSignal("usb")),
            pads.be.eq(0xf),
            pads.siwua.eq(1),
            data_oe.eq(oe_n),
        ]

        fsm = FSM()
        self.submodules.fsm = ClockDomainsRenamer("usb")(fsm)

        self.tempsendval = tempsendval = Signal(dw)
        self.temptosend = temptosend = Signal()

        self.tempreadval = tempreadval = Signal(dw)
        self.temptoread = temptoread = Signal()

        self.wants_read = wants_read = Signal()
        self.wants_write = wants_write = Signal()
        self.cnt_write = cnt_write = Signal(max=timeout+1)
        self.cnt_read = cnt_read = Signal(max=timeout+1)

        first_write = Signal()

        self.comb += [
            wants_read.eq(~temptoread & ~pads.rxf_n),
            wants_write.eq((temptosend | write_fifo.source.valid) & (pads.txe_n == 0)),
        ]

        self.fsmstate = Signal(4)
        self.comb += [
            self.fsmstate.eq(Cat(fsm.ongoing("IDLE"),
                                 fsm.ongoing("WRITE"),
                                 fsm.ongoing("RDWAIT"),
                                 fsm.ongoing("READ")))
        ]

        self.sync.usb += [
            If(~fsm.ongoing("READ"),
                If(temptoread,
                    If(read_buffer.sink.ready,
                        temptoread.eq(0)
                    )
                )
            )
        ]
        self.comb += [
            If(~fsm.ongoing("READ"),
                If(temptoread,
                    read_buffer.sink.data.eq(tempreadval),
                    read_buffer.sink.valid.eq(1),
                )
            )
        ]

        fsm.act("IDLE",
            rd_n.eq(1),
            wr_n.eq(1),

            If(wants_write,
                oe_n.eq(1),
                NextValue(cnt_write, 0),
                NextValue(first_write, 1),
                NextState("WRITE"),
            ).Elif(wants_read,
                oe_n.eq(0),
                NextState("RDWAIT")
            ).Else(
                oe_n.eq(1),
            )
        )

        fsm.act("WRITE",
            If(wants_read,
                NextValue(cnt_write, cnt_write + 1),
            ),
            NextValue(first_write, 0),

            rd_n.eq(1),
            If(pads.txe_n,
                oe_n.eq(1),
                wr_n.eq(1),
                write_fifo.source.ready.eq(0),
                If(write_fifo.source.valid & ~first_write,
                    NextValue(temptosend, 1)
                ),
                NextState("IDLE")
            ).Elif(temptosend,
                oe_n.eq(1),
                data_w.eq(tempsendval),
                wr_n.eq(0),
                NextValue(temptosend, 0)
            ).Elif(cnt_write > timeout,
                oe_n.eq(0),
                NextState("RDWAIT")
            ).Elif(write_fifo.source.valid,
                oe_n.eq(1),
                data_w.eq(write_fifo.source.data),
                write_fifo.source.ready.eq(1),
                NextValue(tempsendval, write_fifo.source.data),
                NextValue(temptosend, 0),
                wr_n.eq(0),
            ).Else(
                oe_n.eq(1),
                wr_n.eq(1),
                NextValue(temptosend, 0),
                NextState("IDLE")
            )
        )

        fsm.act("RDWAIT",
            rd_n.eq(0),
            oe_n.eq(0),
            wr_n.eq(1),
            NextValue(cnt_read, 0),
            NextState("READ")
        )

        fsm.act("READ",
            If(wants_write,
                NextValue(cnt_read, cnt_read + 1),
            ),

            wr_n.eq(1),
            If(pads.rxf_n,
                oe_n.eq(0),
                rd_n.eq(1),
                NextState("IDLE"),
            ).Elif(cnt_read > timeout,
                NextValue(cnt_write, 0),
                NextValue(first_write, 1),
                NextState("WRITE"),
                oe_n.eq(1),
            ).Else(
                oe_n.eq(0),
                read_buffer.sink.valid.eq(1),
                read_buffer.sink.data.eq(data_r),
                NextValue(tempreadval, data_r),
                If(read_buffer.sink.ready,
                    rd_n.eq(0)
                ).Else(
                    NextValue(temptoread, 1),
                    NextState("IDLE"),
                    rd_n.eq(1)
                )
            )
        )
