#include <SimpleTimer.h> //lib voor de timer
#include <ESP8266WiFi.h> //lib voor de ESP8266 (wifi)
#include <PubSubClient.h> //lib voor MQTT

#define DUST_SENSOR_DIGITAL_PIN_PM10  D3        // DSM501 Pin 2
#define DUST_SENSOR_DIGITAL_PIN_PM25  D5        // DSM501 Pin 4 
#define EXCELLENT                     "Excellent"
#define GOOD                          "Good"
#define ACCEPTABLE                    "Acceptable"
#define MODERATE                      "Moderate"
#define HEAVY                         "Heavy"
#define SEVERE                        "Severe"
#define HAZARDOUS                     "Hazardous"

#define wifi_ssid "HRSSID" //SSID van mijn hotspot
#define wifi_password "wifipass" //wachtwoord van mijn hotspot

#define mqtt_server "broker.hivemq.com" //MQTT server (hivemq)
#define mqtt_user "tobiashivemq" //hivemq inlognaam
#define mqtt_password "Hivemq123" //hivemq wachtwoord

#define PM25_topic "sensor/PM25"  //Topic PM25
#define PM10_topic "sensor/PM10"  //Topic PM10

int mqtt_port = 1883; //MQTT port

char message_buff[100]; //Buffer om MQTT berichten te decoderen

long lastMsg = 0;   
long lastRecu = 0;
bool debug = false;  //Als deze op True gaat laat hij zien wat hij precies stuurt op MQTT

unsigned long   duration;
unsigned long   starttime;
unsigned long   endtime;
unsigned long   lowpulseoccupancy = 0;
float           ratio = 0;
unsigned long   SLEEP_TIME    = 2 * 1000;       // Slaap tijd (hoe lang hij slaapt tussen metingen)
unsigned long   sampletime_ms = 5 * 60 * 1000;  // Sample tijd (hoe lang hij iedere keer meet)

struct structAQI{
  // variable enregistreur - recorder variables
  unsigned long   durationPM10;
  unsigned long   lowpulseoccupancyPM10 = 0;
  unsigned long   durationPM25;
  unsigned long   lowpulseoccupancyPM25 = 0;
  unsigned long   starttime;
  unsigned long   endtime;
  // Sensor AQI data
  float         concentrationPM25 = 0;
  float         concentrationPM10  = 0;
  int           AqiPM10            = -1;
  int           AqiPM25            = -1;
  // Indicateurs AQI - AQI display
  int           AQI                = 0;
  String        AqiString          = "";
  int           AqiColor           = 0;
};
struct structAQI AQI;

SimpleTimer timer; //start timer lib
WiFiClient espClient; //start wifi lib
PubSubClient client(espClient); //start MQTT lib

void updateAQILevel(){
  AQI.AQI = AQI.AqiPM10; //Update de AQI
}

void updateAQI() {
  AQI.endtime = millis(); //Sla de tijd op
  float ratio = AQI.lowpulseoccupancyPM10 / (sampletime_ms * 10.0); //Berekenen hoeveel deeltjes na hoeveel tijd gezien worden
  float concentration = 1.1 * pow( ratio, 3) - 3.8 *pow(ratio, 2) + 520 * ratio + 0.62; //Berekenen hoeveel deeltjes in de lucht
  if ( sampletime_ms < 3600000 ) { concentration = concentration * ( sampletime_ms / 3600000.0 ); } //Laat hem een tijdje meten
  AQI.lowpulseoccupancyPM10 = 0; //Reset de sample
  AQI.concentrationPM10 = concentration; //Lees PM10 uit
  
  ratio = AQI.lowpulseoccupancyPM25 / (sampletime_ms * 10.0); //Berekenen hoeveel deeltjes na hoeveel tijd gezien worden
  concentration = 1.1 * pow( ratio, 3) - 3.8 *pow(ratio, 2) + 520 * ratio + 0.62; //Berekenen hoeveel deeltjes in de lucht
  if ( sampletime_ms < 3600000 ) { concentration = concentration * ( sampletime_ms / 3600000.0 ); } //Laat hem een tijdje meten
  AQI.lowpulseoccupancyPM25 = 0; //Reset de sample
  AQI.concentrationPM25 = concentration; //Lees PM25 uit

  Serial.print("Concentrations => PM2.5: "); Serial.print(AQI.concentrationPM25); Serial.print(" | PM10: "); Serial.println(AQI.concentrationPM10);
  
  AQI.starttime = millis(); //Update de tijd
    AQI.AqiPM25 = getACQI( 0, AQI.concentrationPM25 ); //Bereken ACQI waarde van PM25
    AQI.AqiPM10 = getACQI( 1, AQI.concentrationPM10 ); //Bereken ACQI waarde van PM10

  //Update de AQI index zoals aangegeven in https://en.wikipedia.org/wiki/Air_quality_index
  updateAQILevel();
  updateAQIDisplay();
  
  Serial.print("AQIs => PM25: "); Serial.print(AQI.AqiPM25); Serial.print(" | PM10: "); Serial.println(AQI.AqiPM10);
  Serial.print(" | AQI: "); Serial.println(AQI.AQI); Serial.print(" | Message: "); Serial.println(AQI.AqiString);
  

}

void setup() {
  Serial.begin(115200); //Start seriele verbinding met pc
  pinMode(DUST_SENSOR_DIGITAL_PIN_PM10,INPUT); //PM10 pin is input
  pinMode(DUST_SENSOR_DIGITAL_PIN_PM25,INPUT); //PM25 pin is input

  setup_wifi();           //Zet wifi chip aan
  client.setServer(mqtt_server, mqtt_port);    //Configureer MQTT server
  client.setCallback(callback);           //Stel een callback in. Dit is een "confirmation" dat dingen goed aan komen

  for (int i = 1; i <= 10; i++) // wacht 10s zodat DSM501 opwarmt
  {
    delay(1000); // 1s
    Serial.print(i);
    Serial.println(" s (wacht 10s zodat de sensor opwarmt)");
  }
  
  Serial.println("Ready!"); //Als de sensor warm is
  
  AQI.starttime = millis(); //Start de klok op de huidige tijd
  timer.setInterval(sampletime_ms); //Set de interval
}

void loop() {
  if (!client.connected()) { //Blijf reconnecten zolang de verbinding er nog niet is
    reconnect();
  }
  client.loop(); //Ik snap niet exact wat dit doet, maar is nodig om de verbinding tot stand te houden

  AQI.lowpulseoccupancyPM10 += pulseIn(DUST_SENSOR_DIGITAL_PIN_PM10, LOW); //Lees sensor uit voor PM10
  AQI.lowpulseoccupancyPM25 += pulseIn(DUST_SENSOR_DIGITAL_PIN_PM25, LOW); //Lees sensor uit voor PM25

  updateAQI(); //Update de metingen
  
  timer.reset(); //Reset de timer

  long now = millis(); //Zet de klok op de huidige tijd
  if (now - lastMsg > 1000 * 60) { //Stuur elke minuut
    lastMsg = now;

    if ( isnan(AQI.concentrationPM25) || isnan(AQI.concentrationPM10)) { //Als de waardes niet kloppen stuur niks
      Serial.println("Check sensor verbinding");
      return;
    }
    client.publish(PM25_topic, String(AQI.concentrationPM25).c_str(), true); //Stuur PM25 naar server
    client.publish(PM10_topic, String(AQI.concentrationPM10).c_str(), true); //Stuur PM10 naar server
  }
  if (now - lastRecu > 100 ) {
    lastRecu = now;
    client.subscribe("homeassistant/switch1"); //Check of data goed over komt
  }
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(wifi_ssid);

  WiFi.begin(wifi_ssid, wifi_password); //Maak verbinding met hotspot

  while (WiFi.status() != WL_CONNECTED) { //Print "." zolang de verbinding gemaakt wordt
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi OK ");
  Serial.print("=> ESP8266 IP address: ");
  Serial.print(WiFi.localIP()); //Print het IP adres van de ESP
}

void reconnect() {

  while (!client.connected()) { //Als de verbinding wegvalt
    Serial.print("Connecting to MQTT broker ...");
    if (client.connect("ESP8266Client", mqtt_user, mqtt_password)) { //Probeer weer te verbinden met username en password
      Serial.println("OK");
    } else {
      Serial.print("KO, error : ");
      Serial.print(client.state()); //Als de verbinding niet werkt geef de error
      Serial.println(" Wait 5 secondes before to retry");
      delay(5000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {

  int i = 0;
  if ( debug ) {
    Serial.println("Message =>  topic: " + String(topic)); //Zeg de boodschap
    Serial.print(" | lengte: " + String(length,DEC)); //Zeg lengte van de boodschap
  }
  for(i=0; i<length; i++) {
    message_buff[i] = payload[i]; //Boodschap in stukjes opdelen, een buffer maken
  }
  message_buff[i] = '\0';
  
  String msgString = String(message_buff);
  if ( debug ) {
    Serial.println("Boodschap: " + msgString); //Print de opgedeelde boodschap
  }
}

int getATMO( int sensor, float density ){ //Laat ATMO AQI indicator zien
  if ( sensor == 0 ) { //PM25
    if ( density <= 11 ) {
      return 1; 
    } else if ( density > 11 && density <= 24 ) {
      return 2;
    } else if ( density > 24 && density <= 36 ) {
      return 3;
    } else if ( density > 36 && density <= 41 ) {
      return 4;
    } else if ( density > 41 && density <= 47 ) {
      return 5;
    } else if ( density > 47 && density <= 53 ) {
      return 6;
    } else if ( density > 53 && density <= 58 ) {
      return 7;
    } else if ( density > 58 && density <= 64 ) {
      return 8;
    } else if ( density > 64 && density <= 69 ) {
      return 9;
    } else {
      return 10;
    }
  } else {
    if ( density <= 6 ) {
      return 1; 
    } else if ( density > 6 && density <= 13 ) {
      return 2;
    } else if ( density > 13 && density <= 20 ) {
      return 3;
    } else if ( density > 20 && density <= 27 ) {
      return 4;
    } else if ( density > 27 && density <= 34 ) {
      return 5;
    } else if ( density > 34 && density <= 41 ) {
      return 6;
    } else if ( density > 41 && density <= 49 ) {
      return 7;
    } else if ( density > 49 && density <= 64 ) {
      return 8;
    } else if ( density > 64 && density <= 79 ) {
      return 9;
    } else {
      return 10;
    }  
  }
}

void updateAQIDisplay(){
  /*
   * 1 EXCELLENT                    
   * 2 GOOD                         
   * 3 ACCEPTABLE               
   * 4 MODERATE            
   * 5 HEAVY               
   * 6 SEVERE
   * 7 HAZARDOUS
   */
    switch ( AQI.AQI) { //Maak de "AQI" index een niveau zoals aangegeven in https://en.wikipedia.org/wiki/Air_quality_index
      case 25: 
        AQI.AqiString = GOOD;
        break;
      case 50:
        AQI.AqiString = ACCEPTABLE;
        break;
      case 75:
        AQI.AqiString = MODERATE;
        break;
      case 100:
        AQI.AqiString = HEAVY;
        break;         
      default:
        AQI.AqiString = SEVERE;
      }  
}
 
int getACQI( int sensor, float density ){  //Maak van de meting een ACQI waarde
  if ( sensor == 0 ) {  //PM2,5
    if ( density == 0 ) {
      return 0; 
    } else if ( density <= 15 ) {
      return 25 ;
    } else if ( density > 15 && density <= 30 ) {
      return 50;
    } else if ( density > 30 && density <= 55 ) {
      return 75;
    } else if ( density > 55 && density <= 110 ) {
      return 100;
    } else {
      return 150;
    }
  } else {              //PM10
    if ( density == 0 ) {
      return 0; 
    } else if ( density <= 25 ) {
      return 25 ;
    } else if ( density > 25 && density <= 50 ) {
      return 50;
    } else if ( density > 50 && density <= 90 ) {
      return 75;
    } else if ( density > 90 && density <= 180 ) {
      return 100;
    } else {
      return 150;
    }
  }
}

float calcAQI(float I_high, float I_low, float C_high, float C_low, float C) {
  return (I_high - I_low) * (C - C_low) / (C_high - C_low) + I_low; //AQI formule: https://en.wikipedia.org/wiki/Air_Quality_Index#United_States
}

int getAQI(int sensor, float density) { //Maak van de meting een AQI waarde
  int d10 = (int)(density * 10);
  if ( sensor == 0 ) {
    if (d10 <= 0) {
      return 0;
    }
    else if(d10 <= 120) {
      return calcAQI(50, 0, 120, 0, d10);
    }
    else if (d10 <= 354) {
      return calcAQI(100, 51, 354, 121, d10);
    }
    else if (d10 <= 554) {
      return calcAQI(150, 101, 554, 355, d10);
    }
    else if (d10 <= 1504) {
      return calcAQI(200, 151, 1504, 555, d10);
    }
    else if (d10 <= 2504) {
      return calcAQI(300, 201, 2504, 1505, d10);
    }
    else if (d10 <= 3504) {
      return calcAQI(400, 301, 3504, 2505, d10);
    }
    else if (d10 <= 5004) {
      return calcAQI(500, 401, 5004, 3505, d10);
    }
    else if (d10 <= 10000) {
      return calcAQI(1000, 501, 10000, 5005, d10);
    }
    else {
      return 1001;
    }
  } else {
    if (d10 <= 0) {
      return 0;
    }
    else if(d10 <= 540) {
      return calcAQI(50, 0, 540, 0, d10);
    }
    else if (d10 <= 1540) {
      return calcAQI(100, 51, 1540, 541, d10);
    }
    else if (d10 <= 2540) {
      return calcAQI(150, 101, 2540, 1541, d10);
    }
    else if (d10 <= 3550) {
      return calcAQI(200, 151, 3550, 2541, d10);
    }
    else if (d10 <= 4250) {
      return calcAQI(300, 201, 4250, 3551, d10);
    }
    else if (d10 <= 5050) {
      return calcAQI(400, 301, 5050, 4251, d10);
    }
    else if (d10 <= 6050) {
      return calcAQI(500, 401, 6050, 5051, d10);
    }
    else {
      return 1001;
    }
  }   
}
