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

