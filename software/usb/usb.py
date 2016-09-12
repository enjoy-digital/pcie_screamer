from ft601 import FT601Device


class ProtocolError(Exception):
    pass


class TimeoutError(Exception):
    pass


INCOMPLETE = -1
UNMATCHED = 0
class BaseService:
    def match_identifier(self, byt):
        r = True
        r = r and (byt[0] == 0x5a)
        r = r and (byt[1] == 0xa5)
        r = r and (byt[2] == 0x5a)
        r = r and (byt[3] == 0xa5)
        r = r and (byt[4] == self.tag)
        return r

    def get_needed_size_for_identifier(self):
        return self.NEEDED_FOR_SIZE

    def present_bytes(self, b):
        if len(b) < self.get_needed_size_for_identifier():
            return INCOMPLETE

        if not self.match_identifier(b):
            return UNMATCHED

        size = self.get_packet_size(b)

        if len(b) < size:
            return INCOMPLETE

        self.consume(b[:size])

        return size
