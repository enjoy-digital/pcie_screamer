from ft601 import FT601Device


class Etherbone:
    def __init__(self, device):
        self.device = device

    def write(self, addr, data):
        self.device.write(
            (0x5aa55aa5).to_bytes(4, byteorder="little") +
            (0x00000000).to_bytes(4, byteorder="little") +
            (0x00000014).to_bytes(4, byteorder="little") +
            (0x4e6f1044).to_bytes(4, byteorder="big") +
            (0x00000000).to_bytes(4, byteorder="big") +
            (0x000f0100).to_bytes(4, byteorder="big") +
            (addr).to_bytes(4, byteorder="big") +
            (data).to_bytes(4, byteorder="big")
        )

    def read(self, addr):
        self.device.write(
            (0x5aa55aa5).to_bytes(4, byteorder="little") +
            (0x00000000).to_bytes(4, byteorder="little") +
            (0x00000014).to_bytes(4, byteorder="little") +
            (0x4e6f1044).to_bytes(4, byteorder="big") +
            (0x00000000).to_bytes(4, byteorder="big") +
            (0x00f00001).to_bytes(4, byteorder="big") +
            (0x00000000).to_bytes(4, byteorder="big") +
            (addr).to_bytes(4, byteorder="big")
        )
        r = self.device.read(36)
        assert len(r) == 36
        return int.from_bytes(r[-4:],  byteorder="big")


if __name__ == '__main__':
    ft601 = FT601Device()
    ft601.open()
    etherbone = Etherbone(ft601)
    etherbone.write(0x40000000, 0x12345678)
    print("%08x" %etherbone.read(0x40000000))
    ft601.close()