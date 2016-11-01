


struct spi_txrx {
  int cmd;
  int param;
  union {
    unsigned char r_dmxbuf[516]; // 513 + alignment. 
    int r_params[10];
  } rest;
}; 

struct spi_txrx spi_in, spi_out;

#define dmxbuf rest.r_dmxbuf
#define params rest.r_params



struct config {
  int mab;
  int breaktime;
  int autosend;
  int datalen;
} config;


enum mode_t { DMX_IDLE, DMX_TX, DMX_RX};


/* ******************** the protocol *******************************/
enum spi_cmd_t { CMD_DMXDATA=0x1234, 
		 CMD_SETBREAK,   
		 CMD_SETMAB, 
		 CMD_SETDATALEN,
		 CMD_AUTOSEND, 
		 CMD_IDLE, 
		 CMD_READ_DMX, // 0x123a
		 STAT_NODATA,  // 0x123b
		 STAT_RX_IN_PROGRESS, //3c
		 STAT_RXDONE,  // 0x123d
		 STAT_RXOK,    // 0x123e
		 STAT_TX_ACTIVE,
		 STAT_TX_DONE,
};

