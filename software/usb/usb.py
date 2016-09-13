from ft601 import FT601Device
from litex.soc.tools.remote.csr_builder import CSRBuilder


class Etherbone(CSRBuilder):
    def __init__(self, device, csr_csv=None, csr_data_width=32, debug=False):
        self.device = device
        if csr_csv is not None:
            CSRBuilder.__init__(self, self, csr_csv, csr_data_width)
        self.debug = debug

    def read(self, addr, length=None):
        length_int = 1 if length is None else length
        datas = []
        for i in range(length_int):
            self.device.write(
                (0x5aa55aa5).to_bytes(4, byteorder="little") +
                (0x00000000).to_bytes(4, byteorder="little") +
                (0x00000014).to_bytes(4, byteorder="little") +
                (0x4e6f1044).to_bytes(4, byteorder="big") +
                (0x00000000).to_bytes(4, byteorder="big") +
                (0x00f00001).to_bytes(4, byteorder="big") +
                (0x00000000).to_bytes(4, byteorder="big") +
                (addr + 4*i).to_bytes(4, byteorder="big")
            )
            r = self.device.read(36)
            assert len(r) == 36
            datas.append(int.from_bytes(r[-4:],  byteorder="big"))
        if self.debug:
            for i, data in enumerate(datas):
                print("read {:08x} @ {:08x}".format(data, addr + 4*i))
        return datas[0] if length is None else datas

    def write(self, addr, datas):
        datas = datas if isinstance(datas, list) else [datas]
        for i, data in enumerate(datas):
            self.device.write(
                (0x5aa55aa5).to_bytes(4, byteorder="little") +
                (0x00000000).to_bytes(4, byteorder="little") +
                (0x00000014).to_bytes(4, byteorder="little") +
                (0x4e6f1044).to_bytes(4, byteorder="big") +
                (0x00000000).to_bytes(4, byteorder="big") +
                (0x000f0100).to_bytes(4, byteorder="big") +
                (addr + 4*i).to_bytes(4, byteorder="big") +
                (data).to_bytes(4, byteorder="big")
            )
            if self.debug:
                print("write {:08x} @ {:08x}".format(data, addr + 4*i))


if __name__ == '__main__':
    ft601 = FT601Device()
    ft601.open()
    etherbone = Etherbone(ft601)

    # test sram write/read
    for i in range(32):
        etherbone.write(0x40000000 + 4*i, i)
        print("%08x" %etherbone.read(0x40000000 + 4*i))

    # get identifier
    identifier = ""
    for i in range(1, 32):
        identifier += "%c" %etherbone.read(0xe0001800 + 4*i)
    print(identifier)

    ft601.close()