from litex.soc.tools.remote import RemoteClient

wb = RemoteClient()
wb.open()

# # #

wb.regs.msi_data.write(0x5a)
wb.regs.msi_send.write(1)
while wb.regs.msi_done.read() == 0:
	pass

# # #

wb.close()
