from litex.soc.tools.remote import RemoteClient

wb = RemoteClient(csr_data_width=32, debug=True)
wb.open()

# # #
	
wb.regs.usb_streamer_trig.write(1)

# # #

wb.close()
