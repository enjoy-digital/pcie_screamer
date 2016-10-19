import socket

from litepcie.common import *
from litepcie.core.tlp.common import *


def get_field_data(field, dwords):
    return (dwords[field.byte//4] >> field.offset) & (2**field.width-1)


tlp_headers_dict = {
    "RD32": tlp_request_header,
    "WR32": tlp_request_header,
    "CPLD": tlp_completion_header,
    "CPL":  tlp_completion_header
}


class TLP:
    def __init__(self, name, dwords=[0, 0, 0]):
        self.name = name
        self.header = dwords[:3]
        self.data = dwords[3:]
        self.dwords = self.header + self.data
        self.decode_dwords()

    def decode_dwords(self):
        for k, v in tlp_headers_dict[self.name].fields.items():
            setattr(self, k, get_field_data(v, self.header))

    def encode_dwords(self, data=[]):
        self.header = [0, 0, 0]
        for k, v in tlp_headers_dict[self.name].fields.items():
            field = tlp_headers_dict[self.name].fields[k]
            self.header[field.byte//4] |= (getattr(self, k) << field.offset)
        self.data = data
        self.dwords = self.header + self.data
        return self.dwords

    def __repr__(self):
        r = self.name + "\n"
        r += "--------\n"
        for k in sorted(tlp_headers_dict[self.name].fields.keys()):
            r += k + " : 0x{:x}".format(getattr(self, k)) + "\n"
        if len(self.data) != 0:
            r += "data:\n"
            for d in self.data:
                r += "{:08x}\n".format(d)
        return r


class RD32(TLP):
    def __init__(self, dwords=[0, 0, 0]):
        TLP.__init__(self, "RD32", dwords)


class WR32(TLP):
    def __init__(self, dwords=[0, 0, 0]):
        TLP.__init__(self, "WR32", dwords)


class CPLD(TLP):
    def __init__(self, dwords=[0, 0, 0]):
        TLP.__init__(self, "CPLD", dwords)


class CPL(TLP):
    def __init__(self, dwords=[0, 0, 0]):
        TLP.__init__(self, "CPL", dwords)


class Unknown:
    def __repr__(self):
        r = "UNKNOWN\n"
        return r

fmt_type_dict = {
    fmt_type_dict["mem_rd32"]: (RD32, 3),
    fmt_type_dict["mem_wr32"]: (WR32, 4),
    fmt_type_dict["cpld"]: (CPLD, 4),
    fmt_type_dict["cpl"]: (CPL, 3)
}


def parse_dwords(dwords):
    f = get_field_data(tlp_common_header.fields["fmt"], dwords)
    t = get_field_data(tlp_common_header.fields["type"], dwords)
    fmt_type = (f << 5) | t
    tlp_cls, tlp_length = fmt_type_dict[fmt_type]
    if len(dwords) >= tlp_length:
        return tlp_cls(dwords[:tlp_length]), tlp_length
    else:
        return None, 0


class TLPSniffer:
    def __init__(self, debug=False):
        self.debug = debug
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def start(self):
        self.socket.sendto(bytes([0]), ("127.0.0.1", 2345))
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
                        self.socket.sendto(packet, ("127.0.0.1", 2345))

                    dwords = dwords[length:]


if __name__ == '__main__':
    sniffer = TLPSniffer()
    sniffer.start()
