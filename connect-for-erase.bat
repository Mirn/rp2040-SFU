openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 5000" 
rem flash erase_address 0x10000000 0x400000