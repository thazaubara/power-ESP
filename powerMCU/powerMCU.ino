#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <ADS1115_lite.h>

#define BTN_L D6
#define BTN_R D7

LiquidCrystal_I2C lcd(0x27, 8, 2); // I2C address 0x27, 16 column and 2 rows
ADS1115_lite adc(ADS1115_DEFAULT_ADDRESS);
WiFiClient espClient;
PubSubClient client(espClient);

const char* SSID = "MartinRouterPing";
const char* PSK = "_1HaveAStream";
const char* MQTT_BROKER = "192.168.0.3";
const int PORT = 1883;
const int DELAY = 2000;
char msg[50];

float final_watt[3];
float final_amps[3];
float final_mv[3];

void printlcd(String s){
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print(s.substring(0,8));
    lcd.setCursor(0,1);
    lcd.print(s.substring(8,16));
}

void setup(){
  Serial.begin(115200);
  lcd.init(); // initialize the lcd
  lcd.backlight();
  if (!adc.testConnection()) {
    Serial.println("ADS1115 Connection failed"); //oh man...something is wrong
    printlcd("ADS1115 failed.");
    return;
  }

  pinMode(BTN_L, INPUT_PULLUP);
  pinMode(BTN_R, INPUT_PULLUP);

  pinMode(LED_BUILTIN, OUTPUT);     // Initialize the LED_BUILTIN pin as an output
  digitalWrite(LED_BUILTIN, LOW);
  printlcd("WiFi setup ...");
  setup_wifi();
  client.setServer(MQTT_BROKER, PORT);
  printlcd("ready ...");
}

void setup_wifi() {
	delay(10);
	Serial.println();
	Serial.print("Connecting to ");
	Serial.println(SSID);

	WiFi.begin(SSID, PSK);

	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.print(".");
	}

	Serial.println("");
	Serial.println("WiFi connected");
	Serial.println("IP address: ");
	Serial.println(WiFi.localIP());
}

void reconnect() {
	while (!client.connected()) {
		Serial.print("Reconnecting...");
		if (!client.connect("ESP8266Client")) {
			Serial.print("failed, rc=");
			Serial.print(client.state());
			Serial.println(" retrying in 5 seconds");
			delay(5000);
		}
	}
}



void readTrueWatt(int channel){
  int millis = 500;
  Serial.println("----- MEASURING CHANNEL " + String(channel) + " -----");
  Serial.println("True Watt. Average over all read Values");
  // SETUP
  switch (channel){
    case 0:
      adc.setMux(ADS1115_REG_CONFIG_MUX_SINGLE_0);
      break;
    case 1:
      adc.setMux(ADS1115_REG_CONFIG_MUX_SINGLE_1);
      break;
    case 2:
      adc.setMux(ADS1115_REG_CONFIG_MUX_SINGLE_2);
      break;

  }
  //adc.setMux(ADS1115_REG_CONFIG_MUX_SINGLE_0);          // welcher messmodus?
  adc.setSampleRate(ADS1115_REG_CONFIG_DR_860SPS);      // wie schnell?
  //adc.setSampleRate(ADS1115_REG_CONFIG_DR_475SPS);
  //adc.setSampleRate(ADS1115_REG_CONFIG_DR_128SPS);
  adc.setGain(ADS1115_REG_CONFIG_PGA_1_024V);           // welche range? +-1,024V
  int16_t ADS1115raw[500];

  // AQUIRE
  unsigned long startloop = micros();
  unsigned long endloop = micros() + millis * 1000;
  int samples = 0;
  while(micros() < endloop){
    adc.triggerConversion(); //Start a conversion.  This immediatly returns
    ADS1115raw[samples] = adc.getConversion(); //This polls the ADS1115 and wait for conversion to finish, THEN returns the value
    samples++;
  }

  double sum = 0;
  // TRUE RMS BERECHNUNG
  Serial.println("samples: " + String(samples) + ", time: " + String(millis/1000.0) + "s, " + String(samples/((millis/1000.0)*50)) + " samples/period");
  //Serial.println("first 10:");
  float max=0;
  float min=0;
  for (int j = 0; j < samples; j++){
    // 1. in mv umrechnen
    float mv = float(ADS1115raw[j])/32; // for 16 bit conversion. 
    if (mv > max){
      max = mv;
    }
    if (mv < min){
      min = mv;
    } 
    
    // 2. hochklappen
    if (mv < 0){
      mv = -mv; // leichter als mit sqrt
    }
    sum += mv;
  }

  if (min == 0 && max>100){
    final_amps[channel] = 0;
    final_watt[channel] = 0;
    final_mv[channel] = 0;
    return;
  }

  Serial.println("vpp: " + String(max-min,3) + "mv, min: " + String(min,3) + "mV, max: " + String(max, 3) + "mV");
  // 3. AVERAGE RECHNEN
  float average = sum/float(samples);
  Serial.println("average: " + String(average, 3) + "mV " + "sum: " + String(sum));

  // 4. AMPS BERECHNEN
  float amps = average * 0.020;
  Serial.println("amps: " + String(amps, 3) + "A");

  // 5. WATT RMS BERECHNEN
  float watt = amps * 236;
  Serial.println("watt: " + String(watt, 3) + "W");

  // PRINT & STORE VALUES
  final_amps[channel] = amps;
  final_watt[channel] = watt;
  final_mv[channel] = average;
}

void readForTime(){
  Serial.println("----- MEASURING -----");
  // SETUP
  adc.setMux(ADS1115_REG_CONFIG_MUX_SINGLE_0);          // welcher messmodus?
  adc.setSampleRate(ADS1115_REG_CONFIG_DR_860SPS);      // wie schnell?
  //adc.setSampleRate(ADS1115_REG_CONFIG_DR_475SPS);
  adc.setGain(ADS1115_REG_CONFIG_PGA_1_024V);           // welche range? +-1,024V
  int16_t ADS1115raw[500];

  // AQUIRE
  unsigned long startloop = micros();
  unsigned long endloop = micros() + 1000000;
  int samples = 0;
  while(micros() < endloop){
    adc.triggerConversion(); //Start a conversion.  This immediatly returns
    ADS1115raw[samples] = adc.getConversion(); //This polls the ADS1115 and wait for conversion to finish, THEN returns the value
    samples++;
  }

  double sum = 0;
  // TRUE RMS BERECHNUNG
  Serial.println("samples: " + String(samples));
  Serial.println("first 10:");
  float max=0;
  float min=0;
  for (int j = 0; j < samples; j++){
    // 1. in mv umrechnen
    float mv = float(ADS1115raw[j])/32; // for 16 bit conversion. 
    if (mv > max){
      max = mv;
    }
    if (mv < min){
      min = mv;
    } 
    if (j < 10){
      Serial.println("__" + String(ADS1115raw[j]) + " -> " + String(mv,2) + "mV");
    }
    
    //Serial.println(String(mv));
    // 2. hochklappen
    if (mv < 0){
      mv = -mv; // leichter als mit sqrt
    }
    //Serial.println(String(mv));
    sum += mv;
  }
  Serial.println("min: " + String(min,3) + "mV, max:" + String(max, 3) + "mV");
  // 3. AVERAGE RECHNEN
  Serial.println("sum: " + String(sum));
  float average = sum/float(samples);
  Serial.println("average: " + String(average, 3) + "mV");

  // 4. AMPS BERECHNEN
  float amps = average * 0.020;
  Serial.println("amps: " + String(amps, 3) + "A");

  // 5. WATT RMS BERECHNEN
  float watt = amps * 236;
  Serial.println("watt: " + String(watt, 3) + "W");

  // 6. RMS minmax
  float vrms = ((max-min) / 2) * 0.707;
  float amprms = vrms * 0.020;
  float wattrms = amprms *236;
  Serial.println("simple_rms, amps: " + String(amprms, 3) + "A");
  Serial.println("simple_watt: " + String(wattrms, 3) + "W");

  printlcd(String(wattrms,2) + "W " + String(amprms,3) + "A");

}

void singleShot(){
  adc.setMux(ADS1115_REG_CONFIG_MUX_SINGLE_0);          // welcher messmodus?
  adc.setSampleRate(ADS1115_REG_CONFIG_DR_860SPS);      // wie schnell?
  adc.setGain(ADS1115_REG_CONFIG_PGA_1_024V);           // welche range? +-1,024V
  
  int16_t value;
  unsigned long start;
  unsigned long end ;

  start = micros(); //Record a start time for demonstration
  adc.triggerConversion(); //Start a conversion.  This immediatly returns
  value = adc.getConversion(); //This polls the ADS1115 and wait for conversion to finish, THEN returns the value
  end = micros();
  printlcd(String(value, DEC) + ": " + String(end-start, DEC) + "us");
}

void publish(String topic, String message){
  char t[50];
  char m[50];
  message.toCharArray(m,50);
  topic.toCharArray(t,50);
  client.publish(t,m);
}

void sendAlive(){
  digitalWrite(LED_BUILTIN, LOW);

  // SEND REAL STUFF
  snprintf (msg, 50, "Alive since %ld milliseconds", millis());
  //Serial.print("Publish message: ");
  Serial.println(msg);
  client.publish("homesens/power/message", msg);

  // SEND IP ADDRESS
  char bufIp [20];
  IPAddress ipaddr = WiFi.localIP();
  sprintf(bufIp, "%d.%d.%d.%d", ipaddr[0], ipaddr[1], ipaddr[2], ipaddr[3]);
  client.publish("homesens/power/ip", bufIp);

  // SEND MAC ADDRESS
  String mac = String(WiFi.macAddress());
  char bufMac [20];
  mac.toCharArray(bufMac, 20);
  client.publish("homesens/power/mac", bufMac);

  digitalWrite(LED_BUILTIN, HIGH);
}

void loop()
{
  if (!client.connected()) {
    digitalWrite(LED_BUILTIN, LOW);
    printlcd("reconnecting ...");
		reconnect();
	}


  
  // MQTT STUFF
  client.loop();
 
  sendAlive();

  
  readTrueWatt(0);
  digitalWrite(LED_BUILTIN, LOW);
  publish("homesens/power/sum_watt", String(final_watt[0] + final_watt[1] + final_watt[2],0));
  publish("homesens/power/L1_watt", String(final_watt[0],0));
  publish("homesens/power/L1_amp", String(final_amps[0],2));
  publish("homesens/power/L1_sensor_mV", String(final_mv[0],3));
  printlcd("[" + String(final_watt[0] + final_watt[1] + final_watt[2],0) + "] " + String(final_watt[2],0) + " " + String(final_watt[1],0) + " " + String(final_watt[0],0));
  digitalWrite(LED_BUILTIN, HIGH);

  readTrueWatt(1);
  digitalWrite(LED_BUILTIN, LOW);
  publish("homesens/power/sum_watt", String(final_watt[0] + final_watt[1] + final_watt[2],0));
  publish("homesens/power/L2_watt", String(final_watt[1],0));
  publish("homesens/power/L2_amp", String(final_amps[1],2));
  publish("homesens/power/L2_sensor_mV", String(final_mv[1],3));
  printlcd("[" + String(final_watt[0] + final_watt[1] + final_watt[2],0) + "] " + String(final_watt[2],0) + " " + String(final_watt[1],0) + " " + String(final_watt[0],0));
  digitalWrite(LED_BUILTIN, HIGH);
  
  readTrueWatt(2);
  digitalWrite(LED_BUILTIN, LOW);
  publish("homesens/power/sum_watt", String(final_watt[0] + final_watt[1] + final_watt[2],0));
  publish("homesens/power/L3_watt", String(final_watt[2],0));
  publish("homesens/power/L3_amp", String(final_amps[2],2));
  publish("homesens/power/L3_sensor_mV", String(final_mv[2],3));
  printlcd("[" + String(final_watt[0] + final_watt[1] + final_watt[2],0) + "] " + String(final_watt[2],0) + " " + String(final_watt[1],0) + " " + String(final_watt[0],0));
  digitalWrite(LED_BUILTIN, HIGH);



  // FINISH

}



