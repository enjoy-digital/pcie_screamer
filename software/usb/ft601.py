import platform
import ctypes
import os
import time
import queue
import threading

libftdicom =  ctypes.cdll.LoadLibrary("./libft601.dll")


class FTDI_Device(ctypes.Structure):
    _fields_ = [('_1', ctypes.c_void_p)]
    
pFTDI_Device = ctypes.POINTER(FTDI_Device)



# FTDIDevice_Open
FT601_Open = libftdicom.FT601_Open
FT601_Open.argtypes = [pFTDI_Device]
FT601_Open.restype = ctypes.c_int

# FTDIDevice_Close
FT601_Close = libftdicom.FT601_Close
FT601_Close.argtypes = [pFTDI_Device]



FT601_Write = libftdicom.FT601_Write
FT601_Write.argtypes = [
    pFTDI_Device,
    ctypes.c_char_p,
    ctypes.c_size_t,
]
FT601_Write.restype = ctypes.c_int

FT601_Read = libftdicom.FT601_Read
FT601_Read.argtypes = [
    pFTDI_Device,
    ctypes.c_char_p,
    ctypes.c_size_t,
]
FT601_Read.restype = ctypes.c_int


class FT601Device:
    def __init__(self):
        self.__is_open = False
        self._dev = FTDI_Device()
        self.rdbuf = ctypes.create_string_buffer(8192)

    def open(self):
        return FT601_Open(self._dev)


    def close(self):
        return FT601_Close(self._dev)
    
    def write(self, buf):
                if not isinstance(buf, bytes):
                    raise TypeError("buf must be bytes")
                return FT601_Write(self._dev, buf, len(buf))


    def read(self , ll):
        buf = []
        ret = FT601_Read(self._dev, self.rdbuf, ll)
        if ret:
            tmp = ctypes.string_at(self.rdbuf, ret)
            buf.extend(tmp)
            return buf
        else:
            return b''
