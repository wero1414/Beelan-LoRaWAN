/*
    lorawan-arduino-rfm.cpp

    Author: Eduardo Contreras
    Date: 2020-02-23

    Encapsulate Ideetron LoRaWAN simple node demonstrator
    *This fimrware supports
        *Over The Air Activation
        *Activation By Personalization
        *Class switching between Class A and Class C motes
        *Channel hopping
        
        *The following settings can be done
            *Channel Receive and Transmit
            *Datarate Receive and Transmit
            *Transmit power
            *Confirmed or unconfirmed messages
            *Device Address
            *Application Session Key
            *Network Session Key
            *Device EUI
            *Application EUI
            *Application key
            *Mote Class

    Use of this source code is governed by the MIT license that can be found in the LICENSE file.
*/


#include "lorawan-arduino-rfm.h"
#include "Conversions.h"

#define ISR_PREFIX

LoRaWANClass::LoRaWANClass():
  _onReceive(NULL)
{

}

LoRaWANClass::~LoRaWANClass()
{

}

bool LoRaWANClass::init(void)
{
    // Lora Setting Class
    dev_class = CLASS_A;
    // Random seed
    //randomSeed(analogRead(0));

    Rx_Status = NO_RX;

    // current channel
    currentChannel = MULTI;

    // Initialise session data struct (Semtech default key)
    memset(Address_Tx, 0x00, 4);
    memset(NwkSKey, 0x00, 16);
    memset(AppSKey, 0x00, 16);
    
    Frame_Counter_Tx = 0x0000;
    Session_Data.NwkSKey = NwkSKey;
    Session_Data.AppSKey = AppSKey;
    Session_Data.DevAddr = Address_Tx;
    Session_Data.Frame_Counter = &Frame_Counter_Tx;

    //Initialize OTAA data struct
    memset(DevEUI, 0x00, 8);
    memset(AppEUI, 0x00, 8);

    memset(AppKey, 0x00, 16);
    memset(DevNonce, 0x00, 2);
    memset(AppNonce, 0x00, 3);
    memset(NetID, 0x00, 3);

    OTAA_Data.DevEUI = DevEUI;
    OTAA_Data.AppEUI = AppEUI;
    OTAA_Data.AppKey = AppKey;
    OTAA_Data.DevNonce = DevNonce;
    OTAA_Data.AppNonce = AppNonce;
    OTAA_Data.NetID = NetID;
    
    // Device Class
    LoRa_Settings.Mote_Class = 0x00; //0x00 is type A, 0x01 is type C
    
    // Rx
    #if defined(AS_923)
        LoRa_Settings.Datarate_Rx = 0x02;   //set to SF10 BW 125 kHz
    #elif defined(EU_868)
        LoRa_Settings.Datarate_Rx = 0x03;   //set to SF9 BW 125 kHz
    #else //US_915
        LoRa_Settings.Datarate_Rx = 0x0C;   //set to SF8 BW 500 kHz
    #endif
        LoRa_Settings.Channel_Rx = 0x0A;    // set to recv channel

        // Tx
    #if defined(US_915)
        LoRa_Settings.Datarate_Tx = drate_common = 0x02;   //set to SF7 BW 125 kHz
    #else
        LoRa_Settings.Datarate_Tx = drate_common = 0x00;   //set to SF12 BW 125 kHz
    #endif
    LoRa_Settings.Channel_Tx = 0x00;    // set to channel 0

    LoRa_Settings.Confirm = 0x00; //0x00 unconfirmed, 0x01 confirmed
    LoRa_Settings.Channel_Hopping = 0x00; //0x00 no channel hopping, 0x01 channel hopping

    // Initialise buffer for data to transmit
    memset(Data_Tx, 0x00, sizeof(Data_Tx));
    Buffer_Tx.Data = Data_Tx;
    Buffer_Tx.Counter = 0x00;
    // Initialise buffer for data to receive
    memset(Data_Rx, 0x00, sizeof(Data_Rx));
    Buffer_Rx.Data = Data_Rx;
    Buffer_Rx.Counter = 0x00;
    Message_Rx.Direction = 0x01; //Set down direction for Rx message

    //Initialize I/O pins
    pinMode(RFM_pins.DIO0,INPUT);
    pinMode(RFM_pins.DIO1,INPUT);
    #ifdef BOARD_DRAGINO_SHIELD
    pinMode(RFM_pins.DIO5,INPUT);
    #endif
    pinMode(RFM_pins.DIO2,INPUT);
    pinMode(RFM_pins.CS,OUTPUT);
    pinMode(RFM_pins.RST,OUTPUT);
    
    digitalWrite(RFM_pins.CS,HIGH);
    
    // Reset
    digitalWrite(RFM_pins.RST,HIGH);
    delay(10);
    digitalWrite(RFM_pins.RST,LOW);
    delay(10);
    digitalWrite(RFM_pins.RST,HIGH);

    //Initialise the SPI port
    SPI.begin();
    SPI.beginTransaction(SPISettings(4000000,MSBFIRST,SPI_MODE0));

    //Wait until RFM module is started
    delay(50);
    
    //Initialize RFM module
    if(!RFM_Init()){
    return 0;
    }
    return 1;
}

bool LoRaWANClass::join(void)
{    
    bool join_status;
    const unsigned long timeout = 6000;
    unsigned long prev_millis;
    unsigned char i = 0; 

    if (currentChannel == MULTI) {
        randomChannel();
    }
    // join request
    LORA_Send_JoinReq(&OTAA_Data, &LoRa_Settings);

    // loop for <timeout> wait for join accept
    prev_millis = millis();
    do {
        join_status = LORA_join_Accept(&Buffer_Rx, &Session_Data, &OTAA_Data, &Message_Rx, &LoRa_Settings);

    }while ((millis() - prev_millis) < timeout && !join_status);

    return join_status;
}

void LoRaWANClass::setDevEUI(const char *devEUI_in)
{
    for(byte i = 0; i < 8; ++i)
        DevEUI[i] = ASCII2Hex(devEUI_in[i*2],devEUI_in[(i*2) + 1]);
    //Reset frame counter
    Frame_Counter_Tx = 0x0000;

}

void LoRaWANClass::setAppEUI(const char *appEUI_in)
{
    for(byte i = 0; i < 8; ++i)
        AppEUI[i] = ASCII2Hex(appEUI_in[i*2],appEUI_in[(i*2) + 1]);
    //Reset frame counter
    Frame_Counter_Tx = 0x0000;

}

void LoRaWANClass::setAppKey(const char *appKey_in)
{
    for(byte i = 0; i < 16; ++i)
        AppKey[i] = ASCII2Hex(appKey_in[i*2],appKey_in[(i*2) + 1]);
    //Reset frame counter
    Frame_Counter_Tx = 0x0000;
}

void LoRaWANClass::setNwkSKey(const char *NwkKey_in)
{
    for (uint8_t i = 0; i < 16; ++i)
        NwkSKey[i] = ASCII2Hex(NwkKey_in[i*2],NwkKey_in[(i*2)+1]);

    //Reset frame counter
    Frame_Counter_Tx = 0x0000;
}

void LoRaWANClass::setAppSKey(const char *ApskKey_in)
{
    for (uint8_t i = 0; i < 16; ++i)
        AppSKey[i] = ASCII2Hex(ApskKey_in[i*2],ApskKey_in[(i*2)+1]);
    
    //Reset frame counter
    Frame_Counter_Tx = 0x0000;
}

void LoRaWANClass::setDevAddr(const char *devAddr_in)
{
    memset(Session_Data.DevAddr, 0x30, sizeof(Session_Data.DevAddr));

    //Check if it is a set command and there is enough data sent
    Address_Tx[0] = ASCII2Hex(devAddr_in[0],devAddr_in[1]);
    Address_Tx[1] = ASCII2Hex(devAddr_in[2],devAddr_in[3]);
    Address_Tx[2] = ASCII2Hex(devAddr_in[4],devAddr_in[5]);
    Address_Tx[3] = ASCII2Hex(devAddr_in[6],devAddr_in[7]);

    //Reset frame counter
    Frame_Counter_Tx = 0x0000;
}

void LoRaWANClass::setDeviceClass(devclass_t dev_class)
{
    LoRa_Settings.Mote_Class = (dev_class == CLASS_A)? 0x00 : 0x01;

    if (LoRa_Settings.Mote_Class == 0x00) {
        RFM_Switch_Mode(0x01); //Class A 
    } 
    else {
        RFM_Continuous_Receive(&LoRa_Settings); //Class C


    }
}

void LoRaWANClass::sendUplink(char *data, unsigned int len, unsigned char confirm)
{
    static const unsigned int Receive_Delay_1 = 500;
	static const unsigned int Receive_Delay_2 = 1000;
	unsigned long prevTime = 0;
    
    if (currentChannel == MULTI) {
        randomChannel();
    }

    LoRa_Settings.Confirm = (confirm == 0) ? 0 : 1;
    
    Buffer_Tx.Counter = len;
    memcpy(Buffer_Tx.Data,data,len);
    
    //Send here uplink with the RX wait windows
    //LORA_Cycle(&Buffer_Tx, &Buffer_Rx, &RFM_Command_Status, &Session_Data, &OTAA_Data, &Message_Rx, &LoRa_Settings);
    LORA_Send_Data(&Buffer_Tx, &Session_Data, &LoRa_Settings);
    //LORA_Send_Data(Buffer_Tx, Session_Data, LoRa_Settings);
    prevTime = millis();

    	// wait rx1 window
    while((digitalRead(RFM_pins.DIO0) != HIGH) && (millis() - prevTime < Receive_Delay_1));
    //Get data
    LORA_Receive_Data(&Buffer_Rx, &Session_Data, &OTAA_Data, &Message_Rx, &LoRa_Settings);
	//LORA_Receive_Data(Buffer_Rx, Session_Data, OTAA_Data, Message_Rx, LoRa_Settings);

    
	// wait rx2 window
    while((digitalRead(RFM_pins.DIO0) != HIGH) && (millis() - prevTime < Receive_Delay_2));

    //Get data
	LORA_Receive_Data(&Buffer_Rx, &Session_Data, &OTAA_Data, &Message_Rx, &LoRa_Settings);
	//LORA_Receive_Data(Data_Rx, Session_Data, OTAA_Data, Message_Rx, LoRa_Settings);

    if(Buffer_Rx.Counter != 0x00)
      {
        Rx_Status = NEW_RX;
      }

}

void LoRaWANClass::setDataRate(unsigned char data_rate)
{
    drate_common = data_rate;
    #ifndef US_915
    //Check if the value is oke
    if(drate_common <= 0x06)
    {
        LoRa_Settings.Datarate_Tx = drate_common;
    }
    #else
    if(drate_common <= 0x04){
        LoRa_Settings.Datarate_Tx = drate_common;
        LoRa_Settings.Datarate_Rx = data_rate + 0x0A;
    }

    #endif
}

void LoRaWANClass::setChannel(unsigned char channel)
{
    if (channel <= 7) {
        currentChannel = channel;
        LoRa_Settings.Channel_Tx = channel;
    #ifdef US_915
            LoRa_Settings.Channel_Rx = channel + 0x08;    
    #endif
    } else if (channel == MULTI) {
        currentChannel = MULTI;
    }
}

unsigned char LoRaWANClass::getChannel()
{
    return LoRa_Settings.Channel_Tx;
}

unsigned char LoRaWANClass::getDataRate() {
    return LoRa_Settings.Datarate_Tx;
}
void LoRaWANClass::setTxPower(unsigned char power_idx)
{
    unsigned char RFM_Data;
    LoRa_Settings.Transmit_Power = (power_idx > 0x0F) ? 0x0F : power_idx; 
    RFM_Data = LoRa_Settings.Transmit_Power + 0xF0;
    RFM_Write(0x09,RFM_Data);
}

int LoRaWANClass::readData(char *outBuff)
{
    int res = 0;

    //If there is new data
    //Flag for interrupt
    if(Rx_Status == NEW_RX)
    {
        res = Buffer_Rx.Counter;
        memset(outBuff, 0x00, res + 1);
        memcpy(outBuff, Buffer_Rx.Data, res);
        
        // Clear Buffer counter
        Buffer_Rx.Counter = 0x00;
        
        Rx_Status = NO_RX;
    }

    return res;
}

void LoRaWANClass::update(void)
{
      //Receive class C 
      if(digitalRead(RFM_pins.DIO0) == HIGH)
      {
        //Get data
        LORA_Receive_Data(&Buffer_Rx, &Session_Data, &OTAA_Data, &Message_Rx, &LoRa_Settings);

        if(Buffer_Rx.Counter != 0x00)
        {
          Rx_Status = NEW_RX;
        }        
      }
}

void LoRaWANClass::onReceive(void(*callback)(int))
{
  _onReceive = callback;
  LORA_Set_Interrupt();
  if (callback) {
    attachInterrupt(digitalPinToInterrupt(RFM_pins.DIO0), LoRaWANClass::onDio0Rise, RISING);
  } else {
    detachInterrupt(digitalPinToInterrupt(RFM_pins.DIO0));
  }
}


void LoRaWANClass::ISR_handler(void)
{
    LORA_Get_Data(&Buffer_Rx, &Session_Data, &OTAA_Data, &Message_Rx, &LoRa_Settings);
}


ISR_PREFIX void LoRaWANClass::onDio0Rise()
{
  lora.ISR_handler();
}


void LoRaWANClass::randomChannel()
{
    unsigned char freq_idx;
    #ifdef AS_923
        freq_idx = random(0,9);
        // limit drate, ch 8 -> sf7bw250
        LoRa_Settings.Datarate_Tx = freq_idx == 0x08? 0x06 : drate_common;
    #else
        freq_idx = random(0,8);
        LoRa_Settings.Channel_Rx = freq_idx + 0x08;
    #endif
    LoRa_Settings.Channel_Tx = freq_idx;
}

unsigned int LoRaWANClass::getFrameCounter() {
    return Frame_Counter_Tx;
}

void LoRaWANClass::setFrameCounter(unsigned int FrameCounter) {
    Frame_Counter_Tx = FrameCounter;
}

// define lora objet 
LoRaWANClass lora;
