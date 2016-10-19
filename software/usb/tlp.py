import socket


class TLP:
    def __init__(self, debug=False):
        self.debug = debug
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def snif(self):
        self.socket.sendto(bytes([0]), ("127.0.0.1", 2345))
        while True:
            data, addr = self.socket.recvfrom(1024)
            print(data)


if __name__ == '__main__':
    tlp = TLP()
    tlp.snif()
