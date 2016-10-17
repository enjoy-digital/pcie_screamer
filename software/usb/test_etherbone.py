import socket
import time
from litex.soc.tools.remote.etherbone import *

socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

# test writes
writes_datas = [0x12345678]
writes = EtherboneWrites(base_addr=0x40000000, datas=writes_datas)
record = EtherboneRecord()
record.writes = writes
record.wcount = len(writes_datas)

packet = EtherbonePacket()
packet.records = [record]
packet.encode()
socket.sendto(bytes(packet), ("127.0.0.1", 1234))
time.sleep(0.01)

# test reads
reads_addrs = [0x40000000]
reads = EtherboneReads(addrs=reads_addrs)
record = EtherboneRecord()
record.reads = reads
record.rcount = len(reads_addrs)

packet = EtherbonePacket()
packet.records = [record]
packet.encode()
socket.sendto(bytes(packet), ("127.0.0.1", 1234))
data, addr = socket.recvfrom(1024)
packet = EtherbonePacket(data)
packet.decode()
datas = packet.records.pop().writes.get_datas()

print("%08x" %datas[0])
