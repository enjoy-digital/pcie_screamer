from migen import *

from litex.soc.interconnect import stream
from litex.soc.interconnect.csr import *

from litepcie.common import msi_layout


class MSI(Module, AutoCSR):
    def __init__(self):
        self.send = CSR()
        self.done = CSRStatus()
        self.data = CSRStorage(8)

        self.source = stream.Endpoint(msi_layout())

        # # #

        fsm = FSM(reset_state="IDLE")
        self.submodules += fsm

        fsm.act("IDLE",
            self.done.status.eq(1),
            If(self.send.re & self.send.r,
                NextState("SEND")
            )
        )
        fsm.act("SEND",
            self.source.valid.eq(1),
            self.source.dat.eq(self.data.storage),
            If(self.source.ready,
                NextState("IDLE")
            )
        )
