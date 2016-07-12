

/*

 Weather Shield Example

 By: Nathan Seidle

 SparkFun Electronics

 Date: November 16th, 2013

 License: This code is public domain but you buy me a beer if you use this and we meet someday (Beerware license).



 Much of this is based on Mike Grusin's USB Weather Board code: https://www.sparkfun.com/products/10586



 This code reads all the various sensors (wind speed, direction, rain gauge, humidty, pressure, light, batt_lvl)

 and reports it over the serial comm port. This can be easily routed to an datalogger (such as OpenLog) or

 a wireless transmitter (such as Electric Imp).



 Measurements are reported once a second but windspeed and rain gauge are tied to interrupts that are

 calcualted at each report.



 This example code assumes the GPS module is not used.



 */



#include <Wire.h> //I2C needed for sensors

#include "SparkFunMPL3115A2.h" //Pressure sensor

#include "SparkFunHTU21D.h" //Humidity sensor

#include <SoftwareSerial.h>


MPL3115A2 myPressure; //Create an instance of the pressure sensor

HTU21D myHumidity; //Create an instance of the humidity sensor



//Hardware pin definitions

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

// digital I/O pins

const byte WSPEED = 3;

const byte RAIN = 2;

const byte STAT1 = 7;

const byte STAT2 = 8;



// analog I/O pins

const byte REFERENCE_3V3 = A3;

const byte LIGHT = A1;

const byte BATT = A2;

const byte WDIR = A0;

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//stuff for imp

const int IMP_SERIAL_RX = 8;
const int IMP_SERIAL_TX = 9;
SoftwareSerial impSerial(IMP_SERIAL_RX, IMP_SERIAL_TX);

//Global Variables

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

long lastSecond; //The millis counter to see when a second rolls by

byte seconds; //When it hits 60, increase the current minute

byte seconds_2m; //Keeps track of the "wind speed/dir avg" over last 2 minutes array of data

byte minutes; //Keeps track of where we are in various arrays of data

byte minutes_10m; //Keeps track of where we are in wind gust/dir over last 10 minutes array of data



long lastWindCheck = 0;

volatile long lastWindIRQ = 0;

volatile byte windClicks = 0;



//We need to keep track of the following variables:

//Wind speed/dir each update (no storage)

//Wind gust/dir over the day (no storage)

//Wind speed/dir, avg over 2 minutes (store 1 per second)

//Wind gust/dir over last 10 minutes (store 1 per minute)

//Rain over the past hour (store 1 per minute)

//Total rain over date (store one per day)



byte windspdavg[120]; //120 bytes to keep track of 2 minute average



#define WIND_DIR_AVG_SIZE 120

int winddiravg[WIND_DIR_AVG_SIZE]; //120 ints to keep track of 2 minute average

float windgust_10m[10]; //10 floats to keep track of 10 minute max

int windgustdirection_10m[10]; //10 ints to keep track of 10 minute max

volatile float rainHour[60]; //60 floating numbers to keep track of 60 minutes of rain



//These are all the weather values that wunderground expects:

int winddir = 0; // [0-360 instantaneous wind direction]

float windspeedmph = 0; // [mph instantaneous wind speed]

float windgustmph = 0; // [mph current wind gust, using software specific time period]

int windgustdir = 0; // [0-360 using software specific time period]

float windspdmph_avg2m = 0; // [mph 2 minute average wind speed mph]

int winddir_avg2m = 0; // [0-360 2 minute average wind direction]

float windgustmph_10m = 0; // [mph past 10 minutes wind gust mph ]

int windgustdir_10m = 0; // [0-360 past 10 minutes wind gust direction]

float humidity = 0; // [%]

float tempf = 0; // [temperature F]

float rainin = 0; // [rain inches over the past hour)] -- the accumulated rainfall in the past 60 min

volatile float dailyrainin = 0; // [rain inches so far today in local time]

//float baromin = 30.03;// [barom in] - It's hard to calculate baromin locally, do this in the agent

float pressure = 0;

//float dewptf; // [dewpoint F] - It's hard to calculate dewpoint locally, do this in the agent



float batt_lvl = 11.8; //[analog value from 0 to 1023]

float light_lvl = 455; //[analog value from 0 to 1023]



// volatiles are subject to modification by IRQs

volatile unsigned long raintime, rainlast, raininterval, rain;



//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=



//Interrupt routines (these are called by the hardware interrupts, not by the main code)

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

void rainIRQ()

// Count rain gauge bucket tips as they occur

// Activated by the magnet and reed switch in the rain gauge, attached to input D2

{

	raintime = millis(); // grab current time

	raininterval = raintime - rainlast; // calculate interval between this and last event



	if (raininterval > 10) // ignore switch-bounce glitches less than 10mS after initial edge

	{

		dailyrainin += 0.011; //Each dump is 0.011" of water

		rainHour[minutes] += 0.011; //Increase this minute's amount of rain



		rainlast = raintime; // set up for next event

	}

}



void wspeedIRQ()

// Activated by the magnet in the anemometer (2 ticks per rotation), attached to input D3

{

	if (millis() - lastWindIRQ > 10) // Ignore switch-bounce glitches less than 10ms (142MPH max reading) after the reed switch closes

	{

		lastWindIRQ = millis(); //Grab the current time

		windClicks++; //There is 1.492MPH for each click per second.

	}

}





void setup()

{

	// Open the hardware serial port
      Serial.begin(19200);

      // set the data rate for the SoftwareSerial port
      impSerial.begin(19200);



	pinMode(STAT1, OUTPUT); //Status LED Blue

	pinMode(STAT2, OUTPUT); //Status LED Green



	pinMode(WSPEED, INPUT_PULLUP); // input from wind meters windspeed sensor

	pinMode(RAIN, INPUT_PULLUP); // input from wind meters rain gauge sensor



	pinMode(REFERENCE_3V3, INPUT);

	pinMode(LIGHT, INPUT);



	//Configure the pressure sensor

	myPressure.begin(); // Get sensor online

	myPressure.setModeBarometer(); // Measure pressure in Pascals from 20 to 110 kPa

	myPressure.setOversampleRate(7); // Set Oversample to the recommended 128

	myPressure.enableEventFlags(); // Enable all three pressure and temp event flags



	//Configure the humidity sensor

	myHumidity.begin();



	seconds = 0;

	lastSecond = millis();



	// attach external interrupt pins to IRQ functions

	attachInterrupt(0, rainIRQ, FALLING);

	attachInterrupt(1, wspeedIRQ, FALLING);



	// turn on interrupts

	interrupts();






}



void loop()

{

	//Keep track of which minute it is

  if(millis() - lastSecond >= 15*60000)

	{

		digitalWrite(STAT1, HIGH); //Blink stat LED



    lastSecond += 15*60000;
	//Report all readings every second

		printWeather();



		digitalWrite(STAT1, LOW); //Turn off stat LED

	}



  delay(10000);

}



//Calculates each of the variables that wunderground is expecting

void calcWeather()

{

	//read the various sensors

	humidity = myHumidity.readHumidity();

	tempf = myPressure.readTempF();

	pressure = myPressure.readPressure();

	light_lvl = get_light_level();

	batt_lvl = get_battery_level();

}



//Returns the voltage of the light sensor based on the 3.3V rail

//This allows us to ignore what VCC might be (an Arduino plugged into USB has VCC of 4.5 to 5.2V)

float get_light_level()

{

	float operatingVoltage = analogRead(REFERENCE_3V3);



	float lightSensor = analogRead(LIGHT);



	operatingVoltage = 3.3 / operatingVoltage; //The reference voltage is 3.3V



	lightSensor = operatingVoltage * lightSensor;



	return(lightSensor);

}



//Returns the voltage of the raw pin based on the 3.3V rail

//This allows us to ignore what VCC might be (an Arduino plugged into USB has VCC of 4.5 to 5.2V)

//Battery level is connected to the RAW pin on Arduino and is fed through two 5% resistors:

//3.9K on the high side (R1), and 1K on the low side (R2)

float get_battery_level()

{

	float operatingVoltage = analogRead(REFERENCE_3V3);



	float rawVoltage = analogRead(BATT);



	operatingVoltage = 3.30 / operatingVoltage; //The reference voltage is 3.3V



	rawVoltage = operatingVoltage * rawVoltage; //Convert the 0 to 1023 int to actual voltage on BATT pin



	rawVoltage *= 4.90; //(3.9k+1k)/1k - multiple BATT voltage by the voltage divider to get actual system voltage



	return(rawVoltage);

}


//Prints the various variables directly to the port

//I don't like the way this function is written but Arduino doesn't support floats under sprintf

void printWeather()

{

	calcWeather(); //Go calc all the various sensors

        
//	Serial.print("humidity=");

	impSerial.write(tempf);
        impSerial.write(pressure);
        impSerial.write(humidity);
        impSerial.write(light_lvl);

	
	//Serial.println(tempf, 1);

//	Serial.print(",pressure=");

//	Serial.print(pressure, 2);

//	Serial.print(",light_lvl=");

//	Serial.print(light_lvl, 2);

//	Serial.print(",batt_lvl=");

//	Serial.println(batt_lvl, 2);



}
