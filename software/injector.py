import socket

from tlp import *


class Injector:
    def __init__(self, ip="127.0.0.1", port=2345):
        self.ip = ip
        self.port = port
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.sendto(bytes([0]), (self.ip, self.port))

    def run(self):
        dwords = []
        while True:
            # receive dwords
            data, addr = self.socket.recvfrom(1024)
            for i in range(len(data)//4):
                dword = int.from_bytes(data[4*i:4*(i+1)], byteorder="little")
                dwords.append(dword)
            # extract tlps
            while len(dwords):
                tlp, length = parse_dwords(dwords)
                if tlp is None:
                    break
                else:
                    print(tlp)
                    if isinstance(tlp, RD32):
                        cpl = CPLD()
                        cpl.fmt = 0x2
                        cpl.type = 0xa
                        cpl.length = 1
                        cpl.lower_address = (tlp.address & 0xff)*4
                        cpl.requester_id = 0x0
                        cpl.completer_id = 0x100
                        cpl.byte_count = 4
                        cpl.tag = 0x0
                        if tlp.address == 0x3c140601:
                            cpl.encode_dwords([0x50000000]) # FIXME (P)
                        elif tlp.address == 0x3c140602:
                            cpl.encode_dwords([0x51000000]) # FIXME (Q)
                        elif tlp.address == 0x3c140603:
                            cpl.encode_dwords([0x52000000]) # FIXME (R)
                        else:
                            cpl.encode_dwords([0x00000000]) # FIXME
                        packet = bytes()
                        for dword in cpl.dwords:
                            packet += dword.to_bytes(4, byteorder="little")
                        self.socket.sendto(packet, (self.ip, self.port))

                    dwords = dwords[length:]


if __name__ == '__main__':
    injector = Injector()
    injector.run()
