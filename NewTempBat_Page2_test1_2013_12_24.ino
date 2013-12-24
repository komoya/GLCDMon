//------------------------------------------------------------------------------------------------------------------------------------------------
// emonGLCD Home Energy Monitor example
// to be used with nanode Home Energy Monitor example

// Uses power1 variable - change as required if your using different ports

// emonGLCD documentation http://openEnergyMonitor.org/emon/emonglcd

// RTC to reset Kwh counters at midnight is implemented is software. 
// Correct time is updated via NanodeRF which gets time from internet
// Temperature recorded on the emonglcd is also sent to the NanodeRF for online graphing

// GLCD library by Jean-Claude Wippler: JeeLabs.org
// 2010-05-28 <jcw@equi4.com> http://opensource.org/licenses/mit-license.php
//
// Authors: Glyn Hudson and Trystan Lea
// Part of the: openenergymonitor.org project
// Licenced under GNU GPL V3
// http://openenergymonitor.org/emon/license

// THIS SKETCH REQUIRES:

// Libraries in the standard arduino libraries folder:
//
//	- OneWire library	http://www.pjrc.com/teensy/td_libs_OneWire.html
//	- DallasTemperature	http://download.milesburton.com/Arduino/MaximTemperature
//                           or https://github.com/milesburton/Arduino-Temperature-Control-Library
//	- JeeLib		https://github.com/jcw/jeelib
//	- RTClib		https://github.com/jcw/rtclib
//	- GLCD_ST7565		https://github.com/jcw/glcdlib
//
// Other files in project directory (should appear in the arduino tabs above)
//	- icons.ino
//	- templates.ino
//
//-------------------------------------------------------------------------------------------------------------------------------------------------

#include <JeeLib.h>
#include <GLCD_ST7565.h>
#include <avr/pgmspace.h>
GLCD_ST7565 glcd;

//#include <OneWire.h>		    // http://www.pjrc.com/teensy/td_libs_OneWire.html
//#include <DallasTemperature.h>      // http://download.milesburton.com/Arduino/MaximTemperature/ (3.7.2 Beta needed for Arduino 1.0)

#include <RTClib.h>                 // Real time clock (RTC) - used for software RTC to reset kWh counters at midnight
#include <Wire.h>                   // Part of Arduino libraries - needed for RTClib
RTC_DS1307 RTC; //非常重要，将原来的RTC_Millis RTC;改为RTC_DS1307 RTC;

//--------------------------------------------------------------------------------------------
// RFM12B Settings
//--------------------------------------------------------------------------------------------
#define MYNODE 20            // Should be unique on network, node ID 30 reserved for base station
#define freq RF12_433MHZ     // frequency - match to same frequency as RFM12B module (change to 868Mhz or 915Mhz if appropriate)
#define group 210 

#define ONE_WIRE_BUS 5              // temperature sensor connection - hard wired  no temp komo

boolean last_switch_state, switch_state;  // for page 2

byte page = 1;

unsigned long fast_update, slow_update;

//OneWire oneWire(ONE_WIRE_BUS); 
//allasTemperature sensors(&oneWire);

double temp,maxtemp,mintemp;

//---------------------------------------------------
// Data structures for transfering data between units
//---------------------------------------------------
typedef struct { int power1, power2, power3, battery; } PayloadTX;         // neat way of packaging data for RF comms /komo:rf数据打包// 这里需要和emontx一致。http://openenergymonitor.org/emon/buildingblocks/rfm12b2
PayloadTX emontx;

typedef struct { int temp; int battery; } Payloadtemp;
Payloadtemp emontemp;

int hour = 0, minute = 0;
double usekwh = 0;
double use_history[7], gen_history[7];  // for page 2
const int greenLED=6;               // Green tri-color LED
const int redLED=9;                 // Red tri-color LED
const int LDRpin=4;    		    // analog pin of onboard lightsensor /光纤传感器
int cval_use;
const int switch1=16; // for page 2  arduino 16= port 3 AIO

//-------------------------------------------------------------------------------------------- 
// Flow control
//-------------------------------------------------------------------------------------------- 
unsigned long last_emontx;                   // Used to count time from last emontx update
//unsigned long last_emonbase;                   // Used to count time from last emontx update /komo:no base use

//--------------------------------------------------------------------------------------------
// Setup
//--------------------------------------------------------------------------------------------
void setup()
{
  //-------RTC 增加部分
    Serial.begin(57600);
    Wire.begin();//
    RTC.begin();// 因为RTC改为DS1307，这里需要增加RTC.begin();
    
      if (! RTC.isrunning()) {
    Serial.println("RTC is NOT running!");
    RTC.adjust(DateTime(__DATE__, __TIME__));  //ADD RTC TIME,注意，这行必须在IF条件内，否则上电就会重置时间。
  }
  
  
  delay(500); 				   //wait for power to settle before firing up the RF
  rf12_initialize(MYNODE, freq,group);    //无线RF初始化
  delay(100);				   //wait for RF to settle befor turning on display
  glcd.begin(0x05); // 非常重要@@@  原程序带液晶深度定义，引起液晶错误。glcd.begin(0x18）
  //glcd.backLight(200);  //无需调整背光
  
 // sensors.begin();                         // start up the DS18B20 temp sensor onboard   no temp
  //sensors.requestTemperatures();

  //pinMode(greenLED, OUTPUT); 
  //pinMode(redLED, OUTPUT); 
}

//--------------------------------------------------------------------------------------------
// Loop   主循环开始
//--------------------------------------------------------------------------------------------
void loop()
{
  
  if (rf12_recvDone())  //接收数据开始
  {
    if (rf12_crc == 0 && (rf12_hdr & RF12_HDR_CTL) == 0)  // and no rf errors/检查RF没有错误，如没有错误，继续
    {
      int node_id = (rf12_hdr & 0x1F);
      if (node_id == 10) {emontx = *(PayloadTX*) rf12_data; last_emontx = millis();}  //Assuming 10 is the emonTx NodeID 如果emontx的ID node是10，接收数据。
      if (node_id == 18) {emontemp = *(Payloadtemp*) rf12_data;}
      /*if (node_id == 15)			//Assuming 15 is the emonBase node ID/ emonBase
      {
        RTC.adjust(DateTime(2012, 1, 1, rf12_data[1], rf12_data[2], rf12_data[3]));
        last_emonbase = millis();
      } 
      */
    }
  }

  //--------------------------------------------------------------------------------------------
  // Display update every 200ms  液晶屏是快速更新,komo改为 500
  //--------------------------------------------------------------------------------------------
  if ((millis()-fast_update)>500)
  {
    fast_update = millis();   //将目前的秒数赋予fast_update,为下一个循环使用。
    
    DateTime now = RTC.now();
    int last_hour = hour;
    hour = now.hour();
    minute = now.minute();
    
    //Print for test komo
   /*Serial.begin(9600);
    Serial.println(hour); 
    Serial.println(minute); 
    

   */
   //==================for page 2 history 显示第二页的信息
   if (last_hour == 23 && hour == 00) 
    { 
      int i; for(i=6; i>0; i--) use_history[i] = use_history[i-1];
      usekwh = 0;
    }
    use_history[0] = usekwh;   
   //==================for page 2     这一段是显示第二页的信息 

     temp = (emontemp.temperature/100.00);     // 得到tempnode的温度，除以100 2013-12-24更新为100.00，以便得到float类型
    if (fast_update< 10000)     //1秒内 ，对温度最大最小值赋值一次。
    {mintemp = temp; maxtemp = temp;} 
    if (temp > maxtemp) maxtemp = temp;               // reset min and max
    if (temp < mintemp) mintemp = temp;

    usekwh += (emontx.power1 * 0.5) / 3600000;// ！！重要：以200毫秒为时间单位，计算当前功率下的电能消耗，不断累加。
    if (last_hour == 23 && hour == 00) usekwh = 0;                //reset Kwh/d counter at midnight//夜晚12点将当前计算功率清零。
    cval_use = cval_use + (emontx.power1 - cval_use)*0.50;        //smooth transitions//平滑传输，能看到数字慢慢增加
    
    //=======================for page 2
    last_switch_state = switch_state;
    switch_state = digitalRead(switch1);  
    if (!last_switch_state && switch_state) { page += 1; if (page>2) page = 1; } //change page>4 to page>2
 
    
    if (page==1)
    {   
    draw_power_page( "POWER" ,cval_use, "USE", usekwh);// 显示功率和使用电量，注：有字符型，有数据型。
    draw_temperature_time_footer(temp, mintemp, maxtemp, hour,minute);// 显示最底下温度与时间的显示。详见templates
   
    glcd.refresh();
    }
 
    else if (page==2)
    {
      draw_history_page(gen_history, use_history);
    }
    //背光调整,根据光线亮度调整背光，这里暂时不需要。
    /* int LDR = analogRead(LDRpin);                     // Read the LDR Value so we can work out the light level in the room.
    int LDRbacklight = map(LDR, 0, 1023, 50, 250);    // Map the data from the LDR from 0-1023 (Max seen 1000) to var GLCDbrightness min/max
    LDRbacklight = constrain(LDRbacklight, 0, 255);   // Constrain the value to make sure its a PWM value 0-255
    if ((hour > 22) ||  (hour < 5)) glcd.backLight(0); else glcd.backLight(LDRbacklight);  
    */
  } 
  //----------------------
  //温度是慢速更新,no temp
  //----------------------
  /*if ((millis()-slow_update)>10000)
  {
    slow_update = millis();

    sensors.requestTemperatures();
    temp = (sensors.getTempCByIndex(0));
    if (temp > maxtemp) maxtemp = temp;
    if (temp < mintemp) mintemp = temp;                         // set emonglcd payload
       
  } */
 
}
