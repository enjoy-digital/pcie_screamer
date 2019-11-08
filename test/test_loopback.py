#!/usr/bin/env python
# -*- coding: utf-8 -*-

def main():
    f = open("/dev/ft60x0", "r+b")
    f.write(bytearray([0x41, 0x42, 0x43, 0x44]))
    f.write(bytearray([0x45, 0x46, 0x47, 0x48]))
    data = f.read(4)
    print(data.hex())
    data = f.read(4)
    print(data.hex())

if __name__ == "__main__":
    main()
