import socket
import sys

from tlp import *


class Dump:
    def __init__(self, ip="127.0.0.1", port=2345):
        self.ip = ip
        self.port = port
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def read(self, address):
        # send rd32
        rd = RD32()
        rd.fmt = 0b00
        rd.type = 0b00000
        rd.length = 1
        rd.first_be = 0xf
        rd.address = address//4
        rd.requester_id = 0x100
        rd.encode_dwords()
        packet = bytes()
        for dword in rd.dwords:
            packet += dword.to_bytes(4, byteorder="little")
        self.socket.sendto(packet, (self.ip, self.port))

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
                    if isinstance(tlp, CPLD):
                        return tlp.data[0]
                dwords = dwords[length:]


if __name__ == '__main__':
    dump = Dump()
    address = int(sys.argv[1], 16)
    length = int(sys.argv[2])
    for i in range(length):
        if i%4 == 0:
            print("0x%08x: " %(address + 4*i), end="")
        data = dump.read(address + 4*i)
        print("%08x " %data, end="")
        if i%4 == 3:
            print(" ")
