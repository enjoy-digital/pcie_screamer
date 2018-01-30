import socket

from litex.soc.tools.remote.etherbone import *
from litex.soc.tools.remote.csr_builder import CSRBuilder


class Etherbone(CSRBuilder):
    def __init__(self, csr_csv=None, csr_data_width=32, debug=False):
        if csr_csv is not None:
            CSRBuilder.__init__(self, self, csr_csv, csr_data_width)
        self.debug = debug
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def read(self, addr, length=None):
        length_int = 1 if length is None else length
        datas = []
        for i in range(length_int):
            record = EtherboneRecord()
            record.reads = EtherboneReads(addrs=[addr + 4*i])
            record.rcount = 1

            packet = EtherbonePacket()
            packet.records = [record]
            packet.encode()
            self.socket.sendto(bytes(packet), ("127.0.0.1", 1234))

            data, addr = self.socket.recvfrom(1024)
            packet = EtherbonePacket(data)
            packet.decode()
            datas.append(packet.records.pop().writes.get_datas()[0])
        if self.debug:
            for i, data in enumerate(datas):
                print("read {:08x} @ {:08x}".format(data, addr + 4*i))
        return datas[0] if length is None else datas

    def write(self, addr, datas):
        datas = datas if isinstance(datas, list) else [datas]
        for i, data in enumerate(datas):
            record = EtherboneRecord()
            record.writes = EtherboneWrites(base_addr=addr + 4*i, datas=[data])
            record.wcount = 1

            packet = EtherbonePacket()
            packet.records = [record]
            packet.encode()
            self.socket.sendto(bytes(packet), ("127.0.0.1", 1234))

            if self.debug:
                print("write {:08x} @ {:08x}".format(data, addr + 4*i))


if __name__ == '__main__':
    etherbone = Etherbone()

    print("Testing SRAM write/read:")
    for i in range(32):
        etherbone.write(0x10000000 + 4*i, i)
        print("%08x" %etherbone.read(0x10000000 + 4*i))

    identifier = ""
    for i in range(0, 32):
        identifier += "%c" %etherbone.read(0xe0001800 + 4*i)
    print("\nSoC identifier: " + identifier)

    pcie_id = etherbone.read(0xe000881c)
    print("\nPCIe ID: %04x" %pcie_id)
