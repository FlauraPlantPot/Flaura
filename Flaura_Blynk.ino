
// Fill-in information from your Blynk Template here
#define BLYNK_TEMPLATE_ID "???"
#define BLYNK_DEVICE_NAME "Flaura Template Test"

#define BLYNK_FIRMWARE_VERSION        "0.1.0"

#define BLYNK_PRINT Serial
//#define BLYNK_DEBUG

//#define APP_DEBUG

// Uncomment your board, or configure a custom board in Settings.h
//#define USE_WROVER_BOARD

#include "BlynkEdgent.h"
#include <driver/adc.h>

//Global variables for unit conversion
#define hoursToSeconds 3600LL  //conversion factor from hours to seconds --> suffix LL for definition as 64bit constant
#define secondsToMikroseconds  1000000LL //conversion factor from seconds to mikroseconds --> suffix LL for definition as 64bit constant

//PIN Configuration
//Conflict with analog ADC2 Pins and Wifi --> they stop working forever after wifi is startet --> only use ADC1 Pins (32,33,34,35,36,39) for analogread
const byte buttonPin = 0;  //If changed, the pin also has to be changed in the Sleep function
const byte batteryLevelPin = 32;  //voltage divider with 1MOhm and 30KOhm Resistor used to reduce maximum volatge at this pin (3.4V to 3.33V) --> at minimum operating voltage of 2.55V(???) readings at this pin would be 2,47V  
const byte pumpPowerPin = 23; 
const byte moistureSensorPowerPin = 19;
const byte moistureSensorSignalPin = 33;
const byte waterLevelGroundPin = 35; //lowest cable in tank, connect 100kohm Resistor to Ground parallel as pulldown resitor
const byte waterLevelPin[] = {13, 14, 27, 26, 25}; //Pins for Sensores in Tank --> Order: 100%, 75%, 50%, 25%, 10% 



//Config values --> can be changed in the Blynk app
//RTC Memory values --> these are preserved during deepsleep but lost when reseting or powering off the esp32
RTC_DATA_ATTR int bootCount = 0; 
RTC_DATA_ATTR int sleepDuration = 24;  //sleep time in minutes
RTC_DATA_ATTR int soilMoistureCritical = 25;  //critical soil moisture in % that initiates watering process
RTC_DATA_ATTR int waterAmount = 20; 
RTC_DATA_ATTR int pumpPowerMin = 150; 
RTC_DATA_ATTR int pumpPowerMax = 230; 
RTC_DATA_ATTR int waterFlowCalibration = 250; 
RTC_DATA_ATTR int soilMoistureCalibrationAir = 3180; 
RTC_DATA_ATTR int soilMoistureCalibrationWater = 1320; 
RTC_DATA_ATTR int waterLevelSensorThreshold = 400; 


//Flag values (boolean) that are received from the Blynk app --> get reset after reboot or deepsleep
int pumpPowerMinCalibrationFlag = 0; 
int pumpPowerMaxCalibrationFlag = 0;
int waterFlowCalibrationFlag = 0; 
int soilMoistureCalibrationAirFlag = 0; 
int soilMoistureCalibrationWaterFlag = 0;
int waterLevelSensorRawReadingsFlag = 0; 



//Global variables for state machine operation
byte downloadBlynkState = 0;
byte uploadBlynkState = 0;
byte batteryLevelMeasureState = 0;
byte waterLevelMeasureState = 0;
byte soilMoistureMeasureState = 0;
byte pumpOperationState = 0;
byte routineState = 0;


//Global variables (Blynk related)
int blynkSyncCounter = 0;
int blynkSyncNumber = 15; //number of values that have to be downloaded from Blynk server during synchronisation
boolean blynkSyncRequired = false;  //flag if sync with Blynk server is required 
boolean BlynkInitialized = false;


//Other global variables
esp_sleep_wakeup_cause_t deepsleepWakeupReason;
const int sensorMeasureWaitingTime = 200;   //200ms between measurements --> same for all measurement functions
int batteryLevelReading[10];
int batteryLevelAverage = 0;
float batteryLevelVoltage = 0;
int batteryLevelPercentage = 0;
int waterLevelSensorReading[5];
int waterLevelPercentage = 0;
const int waterLevelAssociated[] = {100, 75, 50, 25, 10, 0}; //water levels in % associated with each pin
int soilMoistureReading[10];
int soilMoistureAverage = 0;
int soilMoistureCalibrated = 0; 
int soilMoisturePercentage = 0;
int pumpActivityFlag = 0;



void setup(){  
  delay(500); //delay here is required to wake up reliably
  pinMode(buttonPin, INPUT); 
  pinMode(batteryLevelPin, INPUT);
  pinMode(pumpPowerPin, OUTPUT);
  pinMode(moistureSensorSignalPin, INPUT);
  pinMode(moistureSensorPowerPin, OUTPUT);
  pinMode(waterLevelGroundPin, INPUT);    //cathode --> only here no corrosion
  for (int i = 0; i < 5; i++) {
    pinMode(waterLevelPin[i], INPUT);    //corrosion on these anodic pins
  }
  pinMode(LED_BUILTIN, OUTPUT);
  //digitalWrite(LED_BUILTIN, LOW);   // turn the LED ON (strangely Builddin LED is active LOW) --> turns off when entering deepsleep
  Serial.begin(115200);
  delay(100); //wait for serial monitor to open
  bootCount++;
  BLYNK_LOG("Bootcount: %i", bootCount);
  timer.setTimeout(120000L, DeepSleep);  // Setup DeepSleep function to be called after 120000 second --> go to deep sleep if anything takes to long
  routineState = 1;
}



void routine(){
  switch(routineState){
    case 1:  //download new config values from Blynk if sync is required
      downloadBlynkState = 1;
      routineState++;     
      break;
    case 2:  //start measuring processes
      if(downloadBlynkState == 100){
        batteryLevelMeasureState = 1;
        waterLevelMeasureState = 1;
        soilMoistureMeasureState = 1;
        routineState++;
      }
      break;
    case 3:
      if(batteryLevelMeasureState == 100 && waterLevelMeasureState == 100 && soilMoistureMeasureState == 100){
        serialPrintValues();  //print measurement values to serial monitor
        pumpOperationState = 1; //start the pump
        routineState++;
      }
      break;
    case 4:
      if(pumpOperationState == 100){
        uploadBlynkState = 1;  //upload new values
        routineState++;
      }
      break;
    case 5:
      if(uploadBlynkState == 100){
        DeepSleep();
      }
      break;
  } 
}





void downloadBlynk(){
  switch(downloadBlynkState){
    case 1:
     // esp_sleep_wakeup_cause_t deepsleepWakeupReason = esp_sleep_get_wakeup_cause(); //get wakeup reason 
     deepsleepWakeupReason = esp_sleep_get_wakeup_cause(); //get wakeup reason 
      if(deepsleepWakeupReason == ESP_SLEEP_WAKEUP_EXT0 || bootCount == 1){ //if this is the first boot or if button was used to wake up manually
        BLYNK_LOG("This is the first boot of the microcontroller or it was manually woken up from standby mode by button press");
        blynkSyncRequired = true;
        BlynkEdgent.begin(); 
        BlynkInitialized = true;
        downloadBlynkState++;
      }
      else{ 
        BLYNK_LOG("The microcontroller was regularly woken up from standby mode by the timer - synchronisation with Blynk server is not necassary");
        downloadBlynkState = 100; //mark task as finished
      }
      break;
    case 2:
      if(blynkSyncCounter == blynkSyncNumber){  //if all values have been updated
        BLYNK_LOG("Synchronisation finished!");
        blynkSyncRequired = false; //reset flag
        blynkSyncCounter = 0; //reset counter
        Blynk.disconnect();
        WiFi.disconnect();
        BlynkInitialized = false;
        downloadBlynkState = 100; //mark task as finished
      }
      else{
        BLYNK_LOG("Waiting for synchronisation to complete...");
      }
      break;
  }
}



void uploadBlynk(){
  static unsigned long previousUploadCheckTime = 0;
  switch(uploadBlynkState){
    case 1:
      BLYNK_LOG("Uploading new values to Blynk...");
      BlynkEdgent.begin();
      BlynkInitialized = true;
      uploadBlynkState++;
      break;
    case 2:
      if (millis() - previousUploadCheckTime >= 3000){ //check every 3 seconds if boot count has been uploaded yet
        previousUploadCheckTime = millis();
        Blynk.syncVirtual(V104); //needed to trigger BLYNK_WRITE(V104)
      }
      break;
  }
}





BLYNK_CONNECTED() {  //gets called as soon as connection to Blynk server is established
  if(blynkSyncRequired == true){
    BLYNK_LOG("Synchronisation with Blynk server started");
    Blynk.syncVirtual(V105, V106, V107, V0, V1, V2, V10, V3, V4, V5, V6, V7, V8, V9, V11); //get latest config values from Blynk server    
  }
  else{
    BLYNK_LOG("Upload of new values to Blynk server started");
    Blynk.virtualWrite(V102, batteryLevelPercentage);
    Blynk.virtualWrite(V101, waterLevelPercentage);   
    if(waterLevelSensorRawReadingsFlag == 1){
      Blynk.virtualWrite(V12, waterLevelSensorReading[0]); //upload raw reading on 100% pin
      Blynk.virtualWrite(V13, waterLevelSensorReading[1]); //upload raw reading on 75% pin
      Blynk.virtualWrite(V14, waterLevelSensorReading[2]); //upload raw reading on 50% pin
      Blynk.virtualWrite(V15, waterLevelSensorReading[3]); //upload raw reading on 25% pin
      Blynk.virtualWrite(V16, waterLevelSensorReading[4]); //upload raw reading on 10% pin
      Blynk.virtualWrite(V11, 0); //upload zero to reset flag on server if it was set 
    }
    Blynk.virtualWrite(V100, soilMoisturePercentage);
    if(soilMoistureCalibrationAirFlag == 1){
      Blynk.virtualWrite(V5, soilMoistureCalibrationAir);
      Blynk.virtualWrite(V7, 0); //upload zero to reset flag on server if it was set     
    }
    if(soilMoistureCalibrationWaterFlag == 1){
      Blynk.virtualWrite(V6, soilMoistureCalibrationWater);
      Blynk.virtualWrite(V8, 0); //upload zero to reset flag on server if it was set  
    }
    Blynk.virtualWrite(V103, pumpActivityFlag);
    if(pumpPowerMinCalibrationFlag == 1 || pumpPowerMaxCalibrationFlag == 1 || waterFlowCalibrationFlag == 1){ //upload zero to reset flags on server if any of them was set
      Blynk.virtualWrite(V4, 0); 
      Blynk.virtualWrite(V2, 0); 
      Blynk.virtualWrite(V10, 0);
    }
    Blynk.virtualWrite(V104, bootCount);  
  }
}






BLYNK_WRITE(V104){   //gets called by Blynk.syncVirtual function
  int bootCountServer = param.asInt(); // Get value as int
  if(bootCount == bootCountServer){ //if boot count on device and server are matching --> upload has finished
    BLYNK_LOG("Upload of new values to Blynk server confirmed");
    uploadBlynkState = 100; //mark upload task as finsihed
  }
}

BLYNK_WRITE(V105){   //gets called by Blynk.syncVirtual function
  sleepDuration = param.asInt(); // Get value as int
  BLYNK_LOG("New value for sleep duration received: %i", sleepDuration);
  blynkSyncCounter++;
}

BLYNK_WRITE(V106){   //gets called by Blynk.syncVirtual function
  soilMoistureCritical = param.asInt(); // Get value as int
  BLYNK_LOG("New value for critical soil moisture received: %i", soilMoistureCritical);
  blynkSyncCounter++;
}

BLYNK_WRITE(V107){   //gets called by Blynk.syncVirtual function
  waterAmount = param.asInt(); // Get value as int
  BLYNK_LOG("New value for water amount received: %i", waterAmount);
  blynkSyncCounter++;
}


BLYNK_WRITE(V0){   //gets called by Blynk.syncVirtual function
  pumpPowerMin = param.asInt(); // Get value as int
  BLYNK_LOG("New value for minimal pump power received: %i", pumpPowerMin);
  blynkSyncCounter++;
}

BLYNK_WRITE(V1){   //gets called by Blynk.syncVirtual function
  pumpPowerMax = param.asInt(); // Get value as int
  BLYNK_LOG("New value for maximum pump power received: %i", pumpPowerMax);
  blynkSyncCounter++;
}

BLYNK_WRITE(V2){   //gets called by Blynk.syncVirtual function
  pumpPowerMinCalibrationFlag = param.asInt(); // Get value as int
  BLYNK_LOG("New flag for minimum pump power calibration received: %i", pumpPowerMinCalibrationFlag);
  blynkSyncCounter++;
}

BLYNK_WRITE(V10){   //gets called by Blynk.syncVirtual function
  pumpPowerMaxCalibrationFlag = param.asInt(); // Get value as int
  BLYNK_LOG("New flag for maximum pump power calibration received: %i", pumpPowerMaxCalibrationFlag);
  blynkSyncCounter++;
}

BLYNK_WRITE(V3){   //gets called by Blynk.syncVirtual function
  waterFlowCalibration= param.asInt(); // Get value as int
  BLYNK_LOG("New value for water flow calibration received: %i", waterFlowCalibration);
  blynkSyncCounter++;
}

BLYNK_WRITE(V4){   //gets called by Blynk.syncVirtual function
  waterFlowCalibrationFlag= param.asInt(); // Get value as int
  BLYNK_LOG("New flag for water flow calibration received: %i", waterFlowCalibrationFlag);
  blynkSyncCounter++;
}

BLYNK_WRITE(V5){   //gets called by Blynk.syncVirtual function
  soilMoistureCalibrationAir= param.asInt(); // Get value as int
  BLYNK_LOG("New value for soil moisture calibration in air received: %i", soilMoistureCalibrationAir);
  blynkSyncCounter++;
}

BLYNK_WRITE(V6){   //gets called by Blynk.syncVirtual function
  soilMoistureCalibrationWater= param.asInt(); // Get value as int
  BLYNK_LOG("New value for soil moisture calibration in water received: %i", soilMoistureCalibrationWater);
  blynkSyncCounter++;
}

BLYNK_WRITE(V7){   //gets called by Blynk.syncVirtual function
  soilMoistureCalibrationAirFlag= param.asInt(); // Get value as int
  BLYNK_LOG("New flag for soil moisture calibration in air received: %i", soilMoistureCalibrationAirFlag);
  blynkSyncCounter++;
}

BLYNK_WRITE(V8){   //gets called by Blynk.syncVirtual function
  soilMoistureCalibrationWaterFlag= param.asInt(); // Get value as int
  BLYNK_LOG("New flag for soil moisture calibration in water received: %i", soilMoistureCalibrationWaterFlag);
  blynkSyncCounter++;
}

BLYNK_WRITE(V9){   //gets called by Blynk.syncVirtual function
  waterLevelSensorThreshold= param.asInt(); // Get value as int
  BLYNK_LOG("New value for water level sensor threshold received: %i", waterLevelSensorThreshold);
  blynkSyncCounter++;
}

BLYNK_WRITE(V11){   //gets called by Blynk.syncVirtual function
  waterLevelSensorRawReadingsFlag= param.asInt(); // Get value as int
  BLYNK_LOG("New flag for raw water level sensor readings received: %i", waterLevelSensorRawReadingsFlag);
  blynkSyncCounter++;
}









void batteryLevelMeasure() {
  static int batteryReadingCounter = 0;
  static unsigned long previousBatteryLevelMeasureTime = 0;
  switch(batteryLevelMeasureState){
    case 1:  //do this on first run of this task -> reset values
      batteryLevelAverage = 0; 
      batteryReadingCounter = 0; 
      batteryLevelMeasureState++;
      break;
    case 2: 
      if(batteryReadingCounter < 10 && millis() - previousBatteryLevelMeasureTime >= sensorMeasureWaitingTime){ //do 10 measurement repetitions every few milliseconds
        previousBatteryLevelMeasureTime = millis();  //reset measure timer
        batteryLevelReading[batteryReadingCounter] = analogRead(batteryLevelPin);
        batteryLevelAverage += batteryLevelReading[batteryReadingCounter]; //add current sensor reading to average
        batteryReadingCounter++;
      }
      if(batteryReadingCounter == 10){
        batteryLevelMeasureState++;
      }
      break;  
    case 3: //after all measurement repetitions have been performed
      batteryLevelAverage = batteryLevelAverage / 10;
      batteryLevelVoltage = batteryLevelAverage / 4096.0 * 4.62;  //conversion of raw reading to voltage -> multiply with 3.3 by default because of 3.3V basic voltage of the ESP32 --> here its multiplyed by 1.33 (=4.39) because of the voltage divider in the circuit and corrected with an additional correction factor => 4.62   
      batteryLevelPercentage = 2808.3808 * pow(batteryLevelVoltage, 4) - 43560.9157 * pow(batteryLevelVoltage, 3) + 252848.5888 * pow(batteryLevelVoltage, 2) - 650767.4615 * batteryLevelVoltage + 626532.9;   //conversion from voltage to percentage by using a fitting function for the charge-voltage-curve of an LiPo battery
      if (batteryLevelVoltage > 4.2){
        batteryLevelPercentage = 100;
      }
      if (batteryLevelVoltage < 3.5){
        batteryLevelPercentage = 0;
      }
      batteryLevelMeasureState = 100; //mark task as finsihed
      break;
  }
}








void waterLevelMeasure() {
  static int waterLevelPinIndex = 0;
  static unsigned long previousWaterLevelMeasureTime = 0;
  switch(waterLevelMeasureState){
    case 1:  //do this on first run of this task -> reset values
      waterLevelPinIndex = 0;
      for (int n = 0; n < 5; n++) {
        waterLevelSensorReading[n] = 0;
      }
      waterLevelPercentage = 0;
      waterLevelMeasureState++;
      break;
    case 2: 
      if (waterLevelPinIndex < 5){  //do this up to 5 times (once for each pin) --> Start measuring from top pins and ends at bottom pins, so pins under water are tested less frequently --> less corrosion on these pins
        pinMode(waterLevelPin[waterLevelPinIndex], OUTPUT); //set specific water level pin under test to output
        digitalWrite(waterLevelPin[waterLevelPinIndex], HIGH);  //activate power on specific water level pin under test
        if (millis() - previousWaterLevelMeasureTime >= sensorMeasureWaitingTime) { //wait for the voltage on the pin to rise
          previousWaterLevelMeasureTime = millis();  //reset timer
          waterLevelSensorReading[waterLevelPinIndex] = analogRead(waterLevelGroundPin);  //read voltage on ground pin -> if ground and the specific pin under test are connected by water in the tank -> high value
          digitalWrite(waterLevelPin[waterLevelPinIndex], LOW);  //cut off power to pin
          pinMode(waterLevelPin[waterLevelPinIndex], INPUT);  //set pin to input
          if(waterLevelSensorReading[waterLevelPinIndex] >= waterLevelSensorThreshold && waterLevelPercentage == 0){  // if the specific pin under test and ground pin ARE connectet AND current water level hasent been found yet
            waterLevelPercentage = waterLevelAssociated[waterLevelPinIndex]; //this is the current Water level
          }
          if(waterLevelSensorRawReadingsFlag == 0 && waterLevelPercentage != 0){  //if raw values should NOT be printed AND current water level has been found
              waterLevelMeasureState = 100; //mark task as finished
          }
          waterLevelPinIndex++; //check next pin
        }
      }
      else{
        waterLevelMeasureState = 100; //mark task as finished
      }
      break;
  }
}








void soilMoistureMeasure() {
  static int soilMoistureReadingCounter = 0;
  static unsigned long previousSoilMoistureSensorMeasureTime = 0;
  switch(soilMoistureMeasureState){
    case 1:  //do this on first run of this task -> reset values
      digitalWrite(moistureSensorPowerPin, HIGH);  //power up the sensor
      soilMoistureReadingCounter = 0; 
      soilMoistureAverage = 0; 
      previousSoilMoistureSensorMeasureTime = millis(); //reset measurement timer -> to wait a short time before measuring for the voltage on the sensor to rise
      soilMoistureMeasureState++;
      break;
    case 2:
      if(soilMoistureReadingCounter < 10 && millis() - previousSoilMoistureSensorMeasureTime >= sensorMeasureWaitingTime){  //do 10 measurement repetitions every few milliseconds
        previousSoilMoistureSensorMeasureTime = millis();  //reset measure timer
        soilMoistureReading[soilMoistureReadingCounter] = analogRead(moistureSensorSignalPin);
        soilMoistureAverage += soilMoistureReading[soilMoistureReadingCounter]; //add current sensor reading to average   
        soilMoistureReadingCounter++;
      }
      if(soilMoistureReadingCounter == 10){
        soilMoistureMeasureState++;
      }
      break;  
    case 3: //after all measurement repetitions have been performed
      digitalWrite(moistureSensorPowerPin, LOW); //cut off power to the sensor
      soilMoistureAverage = soilMoistureAverage / 10; //divide the sum of the readings by the number of measurement repetitions
      if(soilMoistureCalibrationAirFlag == 1){
        soilMoistureCalibrationAir = soilMoistureAverage;
      }
      if(soilMoistureCalibrationWaterFlag == 1){
        soilMoistureCalibrationWater = soilMoistureAverage;
      }
      soilMoistureCalibrated = map(soilMoistureAverage, soilMoistureCalibrationWater, soilMoistureCalibrationAir, 1320, 3173);    //calibrate new value with map function
      soilMoisturePercentage = (178147020.5 - 52879.727 * soilMoistureCalibrated) / (1 - 428.814 * soilMoistureCalibrated + 0.9414 * pow(soilMoistureCalibrated, 2)); //Fitting function to calculate %-value
      if (soilMoisturePercentage > 100) {
        soilMoisturePercentage = 100; 
      }
      if (soilMoisturePercentage < 0) {
        soilMoisturePercentage = 0; 
      }
      soilMoistureMeasureState = 100; //mark task as finsihed
      break;
  }
}







void serialPrintValues() {
  for (int n = 0; n < 10; n++) {
    BLYNK_LOG("Raw battery level reading %i: %i", n + 1, batteryLevelReading[n]);
  }
  BLYNK_LOG("Average battery level reading: %i", batteryLevelAverage);
  BLYNK_LOG("Battery level voltage: %f V", batteryLevelVoltage);
  BLYNK_LOG("Battery level percentage: %i %%", batteryLevelPercentage);
  if(waterLevelSensorRawReadingsFlag == 1){
    for (int n = 0; n < 5; n++) {
      BLYNK_LOG("Raw reading for pin on water level %i: %i", waterLevelAssociated[n], waterLevelSensorReading[n]);
    }
  }
  BLYNK_LOG("Water level percentage: %i %%", waterLevelPercentage);
  for (int n = 0; n < 10; n++) {
    BLYNK_LOG("Raw soil moisture reading %i: %i", n + 1, soilMoistureReading[n]);
  }
  BLYNK_LOG("Average soil moisture reading: %i", soilMoistureAverage);
  BLYNK_LOG("Calibrated soil moisture reading: %i", soilMoistureCalibrated);
  BLYNK_LOG("Soil moisture percentage: %i", soilMoisturePercentage);
}











  

void pumpOperation(){
  static const int pumpPwmFrequency = 490;      //frequency of the PWM signal in Hertz -> maximum resolution depends on frequency (higher frequency means lower possible resolution -> max freq [in Hz] = 80000000/(2^resolution) )  -> frequency effects noise of the pump            
  static const int pumpPwmChannel = 0;       //choose a PWM channel -> There are 16 channels from 0 to 15
  static const int pumpPwmResolution = 8;   //resolution for the duty cyle of the PWM signal in bit, higher resolution means finer adjustment -> duty cycle range from 0 to (2^resolution) - 1) -> e.g. 8 bit => duty cycle between 0 and 255
  static float batteryVoltageCompensationValue;  //compensation value to always power the pump with 3.5V (lowest possible battery voltage), independet of the actual battery voltage 
  static int pumpOperationDuration = 10000; // set default pump duration to 10000 milliseconds (10 seconds)
  static unsigned long pumpOperationTime;  //starting time of the pump operation
  static int pumpPwmDutyCycle;          //duty cycle of the PWM signal -> controls power of the pump
  static int pumpPwmDutyCycleMin;     //starting duty cycle
  static int pumpPwmDutyCycleMax;     //ending duty cycle
  static int dutyCycleRestTime;       //time after that duty cycle is increased by 1
  static unsigned long previousDutyCycleIncreaseTime;  //last time the duty cycle was increased 
  switch(pumpOperationState){
    case 1:
      if(soilMoisturePercentage <= soilMoistureCritical || pumpPowerMinCalibrationFlag == 1 || pumpPowerMaxCalibrationFlag == 1 || waterFlowCalibrationFlag == 1){ //check if pump needs to be started
        if(waterLevelPercentage < 10){  //if the water level is less then 10% 
          BLYNK_LOG("Water level is too low - minimum water level for pump operation is 10%");
          pumpOperationState = 100; //mark task as finished
        }
        if(batteryLevelPercentage < 10){  //if the battery level is less then 10% 
          BLYNK_LOG("Battery level is too low - minimum battery level for pump operation is 10%");
          pumpOperationState = 100; //mark task as finished     
        }
        if(waterLevelPercentage >= 10 && batteryLevelPercentage >= 10){
          pumpOperationState++;
        }
      }
      else{
        BLYNK_LOG("No need to start water pump"); 
        pumpOperationState = 100; //mark task as finished 
      }
      break;
    case 2:
      ledcSetup(pumpPwmChannel, pumpPwmFrequency, pumpPwmResolution);   // configure the PWM signal functionalitites for controling the pump
      ledcAttachPin(pumpPowerPin, pumpPwmChannel);     // attach a channel to one Pin for generating the PWM signal for controling the pump
      batteryVoltageCompensationValue = -0.238 * batteryLevelVoltage + 1.833;  //calculate compensation value to always power the pump with 3.5V (lowest possible battery voltage), independet of the actual battery voltage 
      if(pumpPowerMinCalibrationFlag == 1){ //dont change pump PWM while calibrating pump (min and max are almost the same -> +1 to avoid dividing by zero later)
        pumpPwmDutyCycleMin = pumpPowerMin * batteryVoltageCompensationValue;
        pumpPwmDutyCycleMax = pumpPowerMin * batteryVoltageCompensationValue + 1;   
      }
      else if(pumpPowerMaxCalibrationFlag == 1){  //dont change pump PWM while calibrating pump (min and max are almost the same -> +1 to avoid dividing by zero later)
        pumpPwmDutyCycleMin = pumpPowerMax * batteryVoltageCompensationValue;  
        pumpPwmDutyCycleMax = pumpPowerMax * batteryVoltageCompensationValue + 1;   
      }
      else{ //increase pump PWM during operation
        pumpPwmDutyCycleMin = pumpPowerMin * batteryVoltageCompensationValue;  
        pumpPwmDutyCycleMax = pumpPowerMax * batteryVoltageCompensationValue; 
        if (waterFlowCalibrationFlag == 1){
          pumpOperationDuration = 60000; // set pump duration to 60000 milliseconds (60 seconds)
        }
        else{
          pumpOperationDuration = 60000 * waterAmount / waterFlowCalibration;   //calculate how long the pump has to operate to pump the desired amount of water  --> mulitply by 60000 to get result in milliseconds
          BLYNK_LOG("Amount of water to pump: %i mL", waterAmount);
        }
      }
      dutyCycleRestTime = pumpOperationDuration / (pumpPwmDutyCycleMax - pumpPwmDutyCycleMin);  //calculate after how many milliseconds the duty cycle has to be increased in steps of 1, so that it reaches the maximum duty cycle at the end of the pump duration
      BLYNK_LOG("Pumping duration: %i milliseconds", pumpOperationDuration);
      BLYNK_LOG("Starting pump PWM duty cycle at: %i", pumpPwmDutyCycleMin);
      BLYNK_LOG("Ending pump PWM duty cycle at: %i", pumpPwmDutyCycleMax);
      BLYNK_LOG("Increasing PWM duty cycle by 1 every: %i milliseconds", dutyCycleRestTime);
      pumpPwmDutyCycle = pumpPwmDutyCycleMin;  //set duty cycle to the starting value
      pumpOperationTime = millis();  //reset the pump timer to current time 
      previousDutyCycleIncreaseTime = millis();  //reset the duty cycle increase timer to current time
      pumpActivityFlag = 1; 
      BLYNK_LOG("Starting water pump... (Push the hardware button to cancel)");
      pumpOperationState++;
      break;
    case 3:
      if (millis() - pumpOperationTime < pumpOperationDuration){    //as long as the pump duration has not been reached...
        ledcWrite(pumpPwmChannel, pumpPwmDutyCycle);  //send PWM signal with current duty cycle to operate the pump
        if(pumpPwmDutyCycle < pumpPwmDutyCycleMax && millis() - previousDutyCycleIncreaseTime >= dutyCycleRestTime){  //if the maximum duty cycle hasnt been reached and its time for a new increase...
          previousDutyCycleIncreaseTime = millis();   //reset the duty cyle increase timer to current time 
          BLYNK_LOG("Current pump PWM duty cycle: %i", pumpPwmDutyCycle);
          pumpPwmDutyCycle++;  //slowly increase the duty cycle
        }     
      }
      if (millis() - pumpOperationTime >= pumpOperationDuration || digitalRead(buttonPin) == LOW){  //after the pump duration has been reached OR if the pump was stopped manually by pushing the button
        ledcWrite(pumpPwmChannel, 0);  //turn off the pump by sending a PWM signal with a duty cycle of zero
        BLYNK_LOG("Water pumping has finished or was canceled manually by pushing the hardware button");
        pumpOperationState = 100; //mark task as finished
      }
      break;
  }
}






void DeepSleep() {
  Blynk.disconnect(); 
  WiFi.mode(WIFI_OFF); //this is needed to reduce power consumption during deep sleep if there is a ext0 wakeup source defined (ext1 would work without this line)
  adc_power_off();  //this is needed to reduce power consumption during deep sleep if there is a ext0 wakeup source defined (ext1 would work without this line)
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, LOW); //wakup after timer runs out or pin gets pulled low by pressing the wakeup-button (GPI0_NUM_PinNumber has to be written this way)
  esp_sleep_enable_timer_wakeup(sleepDuration * secondsToMikroseconds * 60); //set sleep timer (in minutes)
  BLYNK_LOG("Going to sleep for %i seconds or until hardware button is pressed...", sleepDuration);
  esp_deep_sleep_start(); //start deepsleep
}










void loop() {
  downloadBlynk();
  uploadBlynk();
  batteryLevelMeasure();
  waterLevelMeasure();
  soilMoistureMeasure();
  pumpOperation();
  routine();
  if(BlynkInitialized == true){
    BlynkEdgent.run();
  }
  timer.run(); // Initiates BlynkTimer
}
