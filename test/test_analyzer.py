from litex.soc.tools.remote import RemoteClient
from litescope.software.driver.analyzer import LiteScopeAnalyzerDriver

wb = RemoteClient(csr_data_width=32)
wb.open()

# # #

analyzer = LiteScopeAnalyzerDriver(wb.regs, "analyzer", debug=True)
analyzer.configure_trigger(cond={"usb_phy_read_fifo_source_valid" : 1})
analyzer.configure_subsampler(1)
analyzer.run(offset=128, length=1024)
while not analyzer.done():
    pass
analyzer.upload()
analyzer.save("dump.vcd")

# # #

wb.close()
