from litex.soc.tools.remote.etherbone import EtherbonePacket, EtherboneRecord
from litex.soc.tools.remote.etherbone import EtherboneWrites, EtherboneReads

# write
print("Etherbone write 0x12345678@0x40000000")
record = EtherboneRecord()
record.writes = EtherboneWrites(base_addr=0x40000000, datas=[0x12345678])
record.wcount = len(record.writes)

packet = EtherbonePacket()
packet.records = [record]
packet.encode()
print(packet)

# read
print("Etherbone read @0x40000000")
# prepare packet
record = EtherboneRecord()
record.reads = EtherboneReads(addrs=[0x40000000])
record.rcount = len(record.reads)

# send packet
packet = EtherbonePacket()
packet.records = [record]
packet.encode()
print(packet)
