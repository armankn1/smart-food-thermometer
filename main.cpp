#include <Arduino.h>
#include <Firebase_ESP_Client.h>
#include <math.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WifiManager.h>

#define API_KEY "AIzaSyByNvR6tLrEyt0JEJYeut3PVLMPyywkulI"
#define URL "smart-meat-thermometer-d33ff-default-rtdb.firebaseio.com"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

WiFiManager wm;

float coeffA = 1.327237201e-3;
float coeffB = 1.264398719e-4;
float coeffC = 3.702410876e-7;

unsigned long previousMillis = 0;
const long interval = 5000;

#define SCREEN_WIDTH 128 
#define SCREEN_HEIGHT 64 
#define OLED_RESET    -1    // -1 means the screen shares the ESP32 reset pin
#define SCREEN_ADDRESS 0x3C // Standard I2C address for 0.96" OLEDs
#define BATPIN A13 // BATTERY PIN BROTHER

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- ADC & Thermistor Settings ---
Adafruit_ADS1115 ads;  
#define R1 101300      
#define VCC 3.33       


float getBatteryPercentage(){

  float batt_mv = analogReadMilliVolts(BATPIN);
  batt_mv *= 2.0;

  float batt_v = batt_mv / 1000.0;

  float percentage = ((batt_v - 3.2) / (4.2 - 3.2)) * 100.0;

  if (percentage > 100.0) 
    percentage = 100;
  else if (percentage < 0.0)
    percentage = 0;

  return percentage;
}

void drawBatteryIcon(int x, int y, int percentage){

  display.drawRect(x, y, 25, 10, SSD1306_WHITE);
  display.fillRect(x + 25, y + 2, 3, 6, SSD1306_WHITE);

  int fillWidth = (percentage * 21) / 100 ;

  if (fillWidth > 0)
    display.fillRect(x + 2, y + 2, fillWidth, 6, SSD1306_WHITE);

}

void setup() {

  Serial.begin(115200);

  wm.setConfigPortalBlocking(false);

  bool res = wm.autoConnect("Smart_Thermometer");


  if(!res){

    Serial.println("Failed to connect to Wifi");

    
  }

  else {

      Serial.println("Connected to Wifi successfully");

        config.api_key = API_KEY;
  config.database_url = URL;

  config.signer.test_mode = true;

  Firebase.begin(&config, &auth);

  Firebase.reconnectWiFi(true);

    }

  
  // 1. Initialize the OLED Display
  // SSD1306_SWITCHCAPVCC tells the screen to generate its own 3.3V internal display voltage
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("SSD1306 allocation failed");
    while(1); 
  }
  
  // Show a quick startup boot screen
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 25);
  display.println("Booting System...");
  display.display();
  delay(1000);

  // 2. Initialize the ADS1115 ADC
  ads.setGain(GAIN_ONE); 
  if (!ads.begin()) {
    Serial.println("Failed to initialize ADS.");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("ADC Hardware Error!");
    display.display();
    while (1); 
  }
}

void loop() {

  wm.process();
  // --- Read the Sensor ---
  long adcSum = 0;
  int numSamples = 100; 

  int currentBattery = (int)getBatteryPercentage();

  for (int i = 0; i < numSamples; i++) {
    adcSum += ads.readADC_SingleEnded(0); 
    delay(2); 
  }

  float avgADC = (float)adcSum / numSamples;
  float Vmid = ads.computeVolts(avgADC);
  float Rprobe = (Vmid * R1) / (VCC - Vmid);
  
  float lnR1 = log(Rprobe);
  float lnR3 = pow(lnR1, 3);
  float inv_temp = coeffA + (coeffB *lnR1) + (coeffC *lnR3), temp;

  if (inv_temp == 0){
    temp = -99; // Error state
  } else {
    temp = (1/inv_temp) - 273.15; // Convert Kelvin to Celsius
  }

  // --- Update the OLED Display ---
  display.clearDisplay(); // Wipe the previous frame



  
  display.setTextSize(2); 
  display.setCursor(10, 25);

  drawBatteryIcon(95, 0, currentBattery);
  
  
  if (temp == -99) {
    display.print("ERR");
  } else {
    Serial.println(temp, 1);
    display.print(temp, 1);    
    display.print((char)247);  
    display.print("C");
  }

  
  display.display(); 


  if (Firebase.ready() && millis() - previousMillis >= interval){

    previousMillis = millis();

    if (Firebase.RTDB.getFloat(&fbdo, "/SmartMeatThermometer/calibration/A") && fbdo.floatData() != 0) {
      coeffA = fbdo.floatData();
    }

    if (Firebase.RTDB.getFloat(&fbdo, "/SmartMeatThermometer/calibration/B") && fbdo.floatData() != 0) {
      coeffB = fbdo.floatData();
    }

    if (Firebase.RTDB.getFloat(&fbdo, "/SmartMeatThermometer/calibration/C") && fbdo.floatData() != 0) {
      coeffC = fbdo.floatData();
    }

  float lnR = log(Rprobe);
  float tempK = 1.0 / (coeffA + coeffB * lnR + coeffC * pow(lnR,3));
  float tempFB = tempK - 273.15;

  if (isnan(tempFB)) {

    Serial.println("Error: Temp is NaN");

  } else {
    float uploadtemp = round(tempFB * 10.0) / 10.0;

    Firebase.RTDB.setFloat(&fbdo, "/SmartMeatThermometer/temperature", uploadtemp);
    Firebase.RTDB.setFloat(&fbdo, "/SmartMeatThermometer/lastPing", millis());
    Firebase.RTDB.setFloat(&fbdo, "/SmartMeatThermometer/resistance", Rprobe);

    Serial.println("Everything pushed to FB");
  }
}

  
 
  delay(800); 
}