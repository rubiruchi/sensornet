#include "nRF24L01+.h"
#include "nRFSN.h"


/*------------------------------------------------
 * Initialize function
 * - Initializes CE, CSN, IRQ pins
 * - Initializes SPI
 * - Initializes nRF settings to default
 * - Sets TX, RX addresses to default/EEPROM stored values
 * - Initializes interrupt flags and enables IRQ interrupt
 * SPIDiv: SPIDiv
------------------------------------------------*/
void nRFSN::init(uint8_t SPIDiv, uint8_t CEpin, uint8_t CSNpin, uint8_t IRQpin, uint8_t intNum)
{
	nRFSN_CE = CEpin;
	nRFSN_CSN = CSNpin;
	nRFSN_IRQ = IRQpin;

	pinMode(nRFSN_CE, OUTPUT);
	pinMode(nRFSN_CE, OUTPUT);
	pinMode(nRFSN_IRQ, INPUT);
	digitalWrite(nRFSN_CE, HIGH);
	digitalWrite(nRFSN_CSN, HIGH);

	if (!SPIDiv)
	{
		SPIDiv = SPI_CLOCK_DIV4;	// Default to Fosc/4 (4MHz on 16MHz Arduino)
	}
	initSPI(SPIDiv);

	// nRF defaults
	CONFIG_CURR           = B00101011;   // Show RX_DR and MAX_RT interrupts; Enable CRC - 1 uint8_t; Power up; RX
	EN_AA_CURR            = B00000011;   // Enable Auto Ack for pipe 0,1
	EN_RXADDR_CURR        = B00000011;   // Enable data pipe 0,1
	SETUP_AW_CURR         = B00000010;   // Set up for 4 address
	SETUP_RETR_CURR       = B00110000;   // 1000us retransmit delay; 10 retransmits
	RF_CH_CURR            = B01101001;   // Channel 105 (2.400GHz + 0.105GHz = 2.505GHz)
	RF_SETUP_CURR         = B00000110;   // RF data rate to 1Mbps; 0dBm output power (highest)
	RX_PW_P0_CURR         = B00000001;   // 1 payload
	DYNPD_CURR            = B00000011;   // Set dynamic payload for pipe 0
	FEATURE_CURR          = B00000100;   // Enable dynamic payload

	if (!checkAddrs())		// Is there an address in EEPROM?
	{
		RX_ADDRESS[4]         = {0xE7,0xE7,0xE7,0xE7};	// If no EEPROM stored addresses,
		TX_ADDRESS[4]         = {0xE7,0xE7,0xE7,0xE7};	// set deafults
	}

	// nRF24L01+ setup
	// Write to CONFIG register
	configReg('w',CONFIG,CONFIG_CURR);
	// Write to EN_RXADDR register  
	configReg('w',EN_RXADDR,EN_RXADDR_CURR);
	// Write to EN_AA register
	configReg('w',EN_AA,EN_AA_CURR);
	// Write to SETUP_AW register
	configReg('w',SETUP_AW,SETUP_AW_CURR);
	// Write to SETUP_RETR register
	configReg('w',SETUP_RETR,SETUP_RETR_CURR);
	// Write to RF channel register
	configReg('w',RF_CH,RF_CH_CURR);
	// Write to RF setup register  
	configReg('w',RF_SETUP,RF_SETUP_CURR);
	// set TX address
	setTXAddr(TX_ADDRESS,4);
	// set RX address
	setRXAddr(RX_ADDR_P0,RX_ADDRESS,4);
	// Set dynamic payload for pipe 0
	configReg('w',DYNPD,DYNPD_CURR);
	// Write to FEATURE register
	configReg('w',FEATURE,FEATURE_CURR);
	// Flush RX FIFO
	spiTransfer('n',FLUSH_RX,0);
	// Flush TX FIFO
	spiTransfer('n',FLUSH_TX,0);

	// Initialize interrupt flags
	nRFSN_RXInt = 0;
	nRFSN_TXInt = 0;
	nRFSN_MAXInt = 0;
	attachInterrupt(intNum,nRF_ISR,LOW);	// Enable IRQ interrupt
}


/*------------------------------------------------
 * nRFSN sync function. Syncs to Root (RPi) and stores new TX, RX addresses.
 * Returns 0: Sync fail 1: Sync succeed
------------------------------------------------*/
uint8_t nRFSN::sync(void)
{
	// make copy of current TX, RX addresses
	uint8_t prevTXAddr[4];
	memcpy(prevTXAddr,TX_ADDRESS,4);
	uint8_t prevRXAddr[4];
	memcpy(prevRXAddr,RX_ADDRESS,4);

	// set default addresses for syncing
	uint8_t addr[4] = {0xE7,0xE7,0xE7,0xE7};
	setRXAddr(RX_ADDR_P0,addr,4);
	uint8_t addr[4] = {0xE7,0xE7,0xE7,0xE7};
	setTXAddr(addr,4);

	// set to receive mode
	setRXMode();
	uint32_t time = 0;
	uint8_t synced = 0;
	while ((time < 300000) && (synced == 0))				// while less than 30s and unsynced
	{
		if (nRFSN_RXInt)									// has a packet been received?
		{
			uint8_t size = getPayloadSize();				// If so, get the size
			getPayload(size);								// then get the packet data and put in buffer

			if ((nRFSN_BufIn[0] == 0x03) && (size == 9))	// check for sync command and packet size of 9
			{
				for (uint8_t i=0; i<4; i++)					// if valid sync packet, write new addresses to EEPROM
				{
					EEPROM.write(i,nRFSN_BufIn[i+1]);
					EEPROM.write(i+4,nRFSN_BufIn[i+5]);
				}

				synced = 1;									// stop syncing
			}
			else
			{
				break;										// packet was not a valid sync packet
			}
		}
		delayMicroseconds(1000);							// wait 1ms
		time++;												// increment time variable
	}

	if (synced) {								// if sync was successful, set new addresses
		for (uint8_t i=0; i<4; i++)
		{
			TX_ADDRESS[i] = nRFSN_BufIn[i+1];
			RX_ADDRESS[i] = nRFSN_BufIn[i+5];
		}
		// set new TX address
		setTXAddr(TX_ADDRESS,4);
		// set new RX address
		setRXAddr(RX_ADDR_P0,RX_ADDRESS,4);
	} else {									// if sync failed, restore prev addresses and delete garbage
		memcpy(TX_ADDRESS,prevTXAddr,4);
		memcpy(RX_ADDRESS,prevRXAddr,4);
		delete prevTXAddr;
		delete prevRXAddr;
	}

	return synced;								// return 0 if failed, 1 if sucessful
}


/*------------------------------------------------
 * Check EEPROM for valid addresses
------------------------------------------------*/
uint8_t nRFSN::checkAddrs(void)
{
	uint8_t newAddr = 0;

	uint8_t addrByte = EEPROM.read(0);			// get first byte of EEPROM
	
	if (addrByte != 0xFF)						// addresses NEVER start with 255. If 255 was read from EEPROM,
	{											// then device has never been synced before
		for (uint8_t i=0; i<4; i++)
		{
			TX_ADDRESS[i] = EEPROM.read(i);		// If valid addresses was found in EEPROM, get them
			RX_ADDRESS[i] = EEPROM.read(i+4);
		}

		// set TX address
		setTXAddr(TX_ADDRESS,4);
		// set RX address
		setRXAddr(RX_ADDR_P0,RX_ADDRESS,4);

		newAddr = 1;							// flag success
	}

	return newAddr;								// return 0: no addresses found 1: addresses found
}


/*------------------------------------------------
 * IRQ pin interrupt service routine
------------------------------------------------*/
void nRFSN::nRF_ISR(void)
{
	updateStatus();						// Get current nRF status

	if (nRFSN_Status & B01000000)		// If data received IRQ
	{
		nRFSN_RXInt = 1;
	}
	else if (nRFSN_Status & B00100000)	// If data sent IRQ
	{
		nRFSN_TXInt = 1;
		nRFSN_Busy = 0;					// if data sent, nRF no longer busy
	}
	else if (nRFSN_Status & B00010000)	// If max retransmits IRQ
	{
		nRFSN_MAXInt = 1;
		nRFSN_Busy = 0;					// nRF no longer busy
	}
}


/*------------------------------------------------
 * nRF24L01+ command and register definitions
------------------------------------------------*/
void nRFSN::setPower(uint8_t pwrLvl)
{
	RF_SETUP_CURR = pwrLvl << 1;
	configReg('w',RF_SETUP,RF_SETUP_CURR);
}


/*------------------------------------------------
 * nRF24L01+ command and register definitions
------------------------------------------------*/
void nRFSN::setTXMode(void)
{
	CONFIG_CURR = B01001010;
	configReg('w',CONFIG,CONFIG_CURR);
}


/*------------------------------------------------
 * nRF24L01+ command and register definitions
------------------------------------------------*/
void nRFSN::setRXMode(void)
{
	CONFIG_CURR = B00101011;
	configReg('w',CONFIG,CONFIG_CURR);
}


/*------------------------------------------------
 * nRF24L01+ command and register definitions
------------------------------------------------*/
void nRFSN::setMAX_RT(uint8_t numRT)
{
	SETUP_RETR_CURR = (SETUP_RETR_CURR & B11110000) | (numRT & B00001111);
	configReg('w',SETUP_RETR,SETUP_RETR_CURR);
}


/*------------------------------------------------
 * nRF24L01+ command and register definitions
------------------------------------------------*/
void nRFSN::setChannel(uint8_t ch)
{
	RF_CH_CURR = ch;
	configReg('w',RF_CH,RF_CH_CURR);
}


/*------------------------------------------------
 * nRF24L01+ command and register definitions
------------------------------------------------*/
void nRFSN::transfer(char wrn, uint8_t command, uint8_t len)
{
	digitalWrite(nRFSN_CSN, LOW);

	if (wrn == 'w') {                           // Write
        SPI.transfer(W_REGISTER|command);       // Send command
    } else if (wrn == 'r') {                    // Read
        SPI.transfer(R_REGISTER|command);       // Send command
    } else if(wrn == 'n') {
        SPI.transfer(command);                  // Send command
    }

    if (len != 0) {
        for (int i=1;i<=len;i++) {
            nRFSN_BufIn[i-1] = SPI.transfer(nRFSN_BufOut[i-1]);
        }
    }

	digitalWrite(nRFSN_CSN, HIGH);
}


/*------------------------------------------------
 * nRF24L01+ command and register definitions
------------------------------------------------*/
uint8_t nRFSN::getPayloadSize(void)
{
	SPI.transfer(R_RX_PL_WID);
	uint8_t payloadSize = SPI.transfer(NRF_NOP);
	
	return payloadSize;
}


/*------------------------------------------------
 * nRF24L01+ command and register definitions
------------------------------------------------*/
void nRFSN::getPayload(uint8_t payloadSize)
{
	transfer('r',R_RX_PAYLOAD,payloadSize);
}


/*------------------------------------------------
 * nRF24L01+ command and register definitions
------------------------------------------------*/
uint8_t nRFSN::configReg(char wr, uint8_t command, uint8_t data)
{
	digitalWrite(nRFSN_CSN, LOW);

	if (wr == 'w') {                            // Write
        SPI.transfer(W_REGISTER|command);       // Send command
        SPI.transfer(data);
    } else if (wr == 'r') {                     // Read
        SPI.transfer(R_REGISTER|command);       // Send command
        data = SPI.transfer(NRF_NOP);
	}

	digitalWrite(nRFSN_CSN, HIGH);

	return data;
}


/*------------------------------------------------
 * nRF24L01+ command and register definitions
------------------------------------------------*/
void nRFSN::setTXAddr(uint8_t addr[], uint8_t len)
{
	digitalWrite(nRFSN_CSN, LOW);

	SPI.transfer(W_REGISTER|TX_ADDR);

	if (len != 0) {
        // Send address bytes
        for (int i=1;i<=len;i++) {
            SPI.transfer(addr[i-1]);
        }
    }

	digitalWrite(nRFSN_CSN, HIGH);
}


/*------------------------------------------------
 * nRF24L01+ command and register definitions
------------------------------------------------*/
void nRFSN::setRXAddr(uint8_t pipe, uint8_t addr[], uint8_t len)
{
	digitalWrite(nRFSN_CSN, LOW);

	SPI.transfer(W_REGISTER|pipe);

	if (len != 0) {
        // Send address bytes
        for (int i=1;i<=len;i++) {
            SPI.transfer(addr[i-1]);
        }
    }

	digitalWrite(nRFSN_CSN, HIGH);
}


/*------------------------------------------------
 * nRF24L01+ command and register definitions
------------------------------------------------*/
void nRFSN::initSPI(uint8_t SPIDiv)
{
	// SPI setup
	SPI.setBitOrder(MSBFIRST);              // Set most significant bit first
	SPI.setClockDivider(SPIDiv);			// Clock to Fosc/SPIDiv
	SPI.setDataMode(SPI_MODE0);             // Clock polarity 0; clock phase 0
	SPI.begin();                            // Start SPI
}


/*------------------------------------------------
 * nRF24L01+ command and register definitions
------------------------------------------------*/
void nRFSN::updateStatus(void)
{
	digitalWrite(nRFSN_CSN, LOW);

	nRFSN_Status = SPI.transfer(NRF_NOP);

	digitalWrite(nRFSN_CSN, HIGH);
}


/*------------------------------------------------
 * nRF24L01+ command and register definitions
------------------------------------------------*/
void nRFSN::clearInt(uint8_t interrupt)
{
	configReg('w',STATUS,(interrupt << 4));
}