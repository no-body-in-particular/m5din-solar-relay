#define SENSE_PIN 2
#define POWER_RELAY_PIN 1
#define NUM_CHARGE_HOURS 4
#define DISPLAY_UPDATE 3
#define MIN_BATT_VOLTAGE 20
#define VOLTAGE_FACTOR 150.0f

#include <HTTPClient.h>
#include <NTPClient.h>
#include <SPI.h>
#include <Timezone.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiUdp.h>
#include "M5DinMeter.h"
#include <ArduinoJson.h>

TimeChangeRule AMS_SUMMER = {"CEST", Last, Sun, Mar, 2, 120};  
TimeChangeRule AMS_WINTER = {"CET ", Last, Sun, Oct, 3, 60}; 
Timezone AMS(AMS_SUMMER, AMS_WINTER);

WiFiMulti wifiMulti;
HTTPClient http;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600, 30 * 60 * 1000);
DynamicJsonDocument JSONdoc(16384);

struct price_index {
  uint8_t index = 0;
  uint32_t price = 0;
  float purchase_price=0;
};

void SetupWiFi() {
  wifiMulti.addAP("ap_name", "ap_password");
  WiFi.begin();
}

void setup() {
  auto cfg = M5.config();
  DinMeter.begin(cfg, true);
  DinMeter.Display.setRotation(1);
  DinMeter.Display.setTextColor(GREEN);
  DinMeter.Display.setTextDatum(top_left);
  DinMeter.Display.setTextFont(&fonts::FreeMono9pt7b);

  DinMeter.Display.setTextSize(1);
  DinMeter.update();

  digitalWrite(POWER_RELAY_PIN, LOW);
  pinMode(SENSE_PIN, INPUT);
  pinMode(POWER_RELAY_PIN, OUTPUT);

  Serial.begin(115200);
  SetupWiFi();
  timeClient.begin();
}

price_index prices[24];
int price_day = -99;
String retrieve_failure="None";

bool connectWifi() {
  if(WiFi.isConnected()){
    return true;
  }

  for (int i = 0; i < 10; i++) {
    if (wifiMulti.run() != WL_CONNECTED) {
      delay(500);
    } else {
      return true;
    }
  }

retrieve_failure="No WiFi";
return false;
}

void sortPrices(){
        price_index prices_a[24];
        price_index prices_b[24];
        uint8_t idx_a = 0;
        uint8_t idx_b = 0;

        for (uint32_t i = 1; i != 0; i <<= 1) {
          for (uint8_t n = 0; n < 24; n++) {
            if (prices[n].price & i) {
              prices_b[idx_b] = prices[n];
              idx_b++;
            } else {
              prices_a[idx_a] = prices[n];
              idx_a++;
            }
          }

          memcpy(prices, prices_a, idx_a * sizeof(price_index));
          memcpy(prices + idx_a, prices_b, idx_b * sizeof(price_index));
          idx_a = 0;
          idx_b = 0;
        }
}

bool sameDay(){
  timeClient.update();
  time_t epochTime = timeClient.getEpochTime();
  bool dst = AMS.utcIsDST(epochTime);

  if (dst) {
    timeClient.setTimeOffset(7200);
  } else {
    timeClient.setTimeOffset(3600);
  }

  epochTime = timeClient.getEpochTime();
  struct tm *ptm = gmtime((time_t *)&epochTime);
  int monthDay = ptm->tm_mday;

  if (price_day == monthDay) {
    return true;
  }

  return false;
}

bool fillPrices() {
 time_t epochTime = timeClient.getEpochTime();
  struct tm *ptm = gmtime((time_t *)&epochTime);
    int monthDay = ptm->tm_mday;

    http.begin(
        "https://www.stroomperuur.nl/ajax/tarieven.php?leverancier=17&datum=" +
        String(ptm->tm_year + 1900) + "-" + String(ptm->tm_mon + 1) + "-" +
        String(monthDay));    
    int httpCode = http.GET(); 

   if(httpCode <0 || httpCode != HTTP_CODE_OK){
     Serial.print("HTTP error: ");
      Serial.println(httpCode);
      Serial.println(http.getString());
      retrieve_failure="HTTP Error";
      return false;
   }
        String payload = http.getString();
        DeserializationError JSONerr = deserializeJson(JSONdoc, payload);

   if (JSONerr) {
        Serial.printf("JSON DeserializationError %s\r\n", JSONerr.c_str());
        retrieve_failure="JSON parse";
        return false;
    } 

   JsonArray arr = JSONdoc.as<JsonArray>();
   if(arr.isNull() | arr.size()<4){
    retrieve_failure="Array sizer";
    return false;
   }

   JsonArray purchase_prices = arr[0].as<JsonArray>();

   if(purchase_prices.isNull()){
        retrieve_failure="purchase price.";

    return false;
   }

   JsonArray vat_array = arr[1].as<JsonArray>();
   if(vat_array.isNull()){
            retrieve_failure="JSON: No vat prices.";

    return false;
   }
   JsonVariant purchase_energy_cost = arr[3];
   if(purchase_energy_cost.isNull()){
            retrieve_failure="JSON: No network cost.";

    return false;
   }


    float purchase_cost = purchase_energy_cost.as<float>();

        for (int i = 0; i < 24; i++) {
          prices[i].index = i;
          prices[i].purchase_price=purchase_prices[i].as<float>() ;
          prices[i].price = (purchase_prices[i].as<float>() +
                             vat_array[i].as<float>() + purchase_cost) *
                            10000.0f;
        }

        sortPrices();

        for (int i = 0; i < 24; i++) {
          Serial.print(" [");
          Serial.print(prices[i].index);
          Serial.print(" , ");
          Serial.print(prices[i].price / 10000.0f);
          Serial.println("]");
        }  

        price_day=monthDay;
        retrieve_failure="None";
}



String getHoursPlanned(){
  String ret="";

   if(!sameDay()){
    for(int i=13;(i-13)<NUM_CHARGE_HOURS;i++){
      ret+=i;
      ret+=",";
    }

    return ret;
   }

   for(int i=0;i<24;i++){
    if(prices[i].purchase_price<0 || i< NUM_CHARGE_HOURS){
      ret+=prices[i].index;
      ret+=",";
    }
   }
  return ret;
}

bool force_enable=false;

void setEnable() {
  force_enable=!force_enable;
  DinMeter.Display.clear();
  DinMeter.Display.drawString(String("Force on : ")+(force_enable?"On":"Off"), 20, 20);
  delay(2000);
}


bool enableRelay(){

  if(force_enable ){
    return true;
  }

  //battery voltage too low
  if((analogRead(SENSE_PIN)/VOLTAGE_FACTOR)<MIN_BATT_VOLTAGE){
    return true;
  }


   time_t epochTime = timeClient.getEpochTime();
    struct tm *ptm = gmtime((time_t *)&epochTime);

  if(!sameDay()){
    return (ptm->tm_hour >= 13 && ptm->tm_hour<(13+NUM_CHARGE_HOURS));
  }

     for(int i=0;i<24;i++){
        if( (i<NUM_CHARGE_HOURS || prices[i].purchase_price<0) && prices[i].index==ptm->tm_hour){
          return true;
        }
     }

     return false;
}

void loop() {
  static long old_position = DinMeter.Encoder.read();
  static long update_time = millis();

  DinMeter.update();

    long  new_position = DinMeter.Encoder.read();
  if (DinMeter.BtnA.pressedFor(5000)) {
    DinMeter.Power.powerOff();
    return;
  }

  if (abs(new_position - old_position) > 2) {
     setEnable();
     old_position=new_position;
  }

  digitalWrite(POWER_RELAY_PIN,enableRelay()?HIGH:LOW);

   if((millis() - update_time)> (DISPLAY_UPDATE*1000)){
        DinMeter.Display.clear();
        DinMeter.Display.drawString(String("WiFi: ") + (WiFi.isConnected()?"Connected":"Disconnected") ,5 , 3);
        DinMeter.Display.drawString(String("Data retrieved: ") + (sameDay()?String("True"):String("False")), 5, 18);
        DinMeter.Display.drawString("Error: " + retrieve_failure, 5, 33);
        DinMeter.Display.drawString("Date/time: " + timeClient.getFormattedTime(), 5, 48);
        DinMeter.Display.drawString("Hours: ", 5, 63);
        DinMeter.Display.drawString( " " + getHoursPlanned(), 5, 78);
        DinMeter.Display.drawString(String("Relay enabled: ")+(enableRelay()?"True":"False") , 5, 93);
        DinMeter.Display.drawString("Battery voltage: "  +String(analogRead(SENSE_PIN)/VOLTAGE_FACTOR), 5, 108);
        update_time=millis();
   }

   if(!connectWifi()){
    return;
   }

   if(sameDay()){
    return;
   }

   fillPrices();
   
}
