bw_rpi_tools
============

Tools to access the bitwizard I2C and SPI expansion boards
on raspberry pi. 


Getting / compiling / installing 
================================

If you don't have git installed already, on debian or derived
operating systems (raspian): 

   sudo apt-get install git-core

Then get the bw_rpi_tools: 

   git clone https://github.com/rewolff/bw_rpi_tools.git

now, change into the directory: 

   cd bw_rpi_tools
   cd bw_tool

and build: 

   make

If that looks succesful, you can  install with "make install". 

The "gpio" tools that are included were written before others wrote 
similar tools. Those seem more popular right now. Unless you need a
specific feature from my gpio tools, just use what everybody else 
uses. 

The "wiringpi" library is the "similar tools" that I'm talking about. 
It uses the kernel interfaces to GPIO of the platform, and therefore
does not have to do SOC-specific low-level-stuff that my GPIO tools
here have to do. VZ just notified me that the base address of GPIOs
changed on the "upgrade" from the raspberry pi model B+ to the 
raspberry pi 2 model B. That change has also been made to the kernel
on the new platform, so the wiring pi library continues to work without
modification while my gpio tools (unmaintained "two years" before the 
pi2 came out) would need a patch. 


