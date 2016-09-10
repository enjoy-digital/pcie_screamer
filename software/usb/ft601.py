import platform
import ctypes

if platform.system() == "Windows":
    libftdicom =  ctypes.cdll.LoadLibrary("./libft601.dll")
else:
    libftdicom =  ctypes.cdll.LoadLibrary("./libft601.so")


class FT601_Device(ctypes.Structure):
    _fields_ = [('_1', ctypes.c_void_p)]


pFTDI_Device = ctypes.POINTER(FTDI_Device)

# FT601_Open
FT601_Open = libftdicom.FT601_Open
FT601_Open.argtypes = [
        pFTDI_Device # Dev
    ]
FT601_Open.restype = ctypes.c_int

# FT601_Close
FT601_Close = libftdicom.FT601_Close
FT601_Close.argtypes = [
        pFTDI_Device # Dev
    ]

# FT601_Write
FT601_Write = libftdicom.FT601_Write
FT601_Write.argtypes = [
    pFTDI_Device,     # Dev
    ctypes.c_char_p,  # Buffer
    ctypes.c_size_t   # Length
]
FT601_Write.restype = ctypes.c_int

# FT601_Read
FT601_Read = libftdicom.FT601_Read
FT601_Read.argtypes = [
    pFTDI_Device,     # Dev
    ctypes.c_char_p,  # Buffer
    ctypes.c_size_t   # Length
]
FT601_Read.restype = ctypes.c_int


class FT601Device:
    def __init__(self):
        self.__is_open = False
        self._dev = FT601_Device()
        self.rdbuf = ctypes.create_string_buffer(8192)

    def open(self):
        return FT601_Open(self._dev)

    def close(self):
        return FT601_Close(self._dev)
    
    def write(self, buf):
        assert isinstance(buf, bytes)
        return FT601_Write(self._dev, buf, len(buf))

    def read(self, length):
        r = FT601_Read(self._dev, self.rdbuf, length)
        if r:
            buf = []
            buf.extend(ctypes.string_at(self.rdbuf, r))
            return buf
        else:
            return b''
