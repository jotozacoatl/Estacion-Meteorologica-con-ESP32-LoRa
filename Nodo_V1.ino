#include <SPI.h>
#include <LoRa.h>
#include <DHT.h>

// --- CONFIGURACIÓN DEL NODO ---
const int NODO_ID = 1; // Cambia a 2 para el segundo esclavo

#define DHTPIN 4    
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// Pines LoRa-02
#define SCK_PIN   18
#define MISO_PIN  19
#define MOSI_PIN  23
#define SS_PIN    5
#define RST_PIN   14
#define DIO0_PIN  26

// Pines LEDs
#define LED_AZUL     27  
#define LED_VERDE    25  
#define LED_AMARILLO 33  

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_AZUL, OUTPUT);
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_AMARILLO, OUTPUT);

  digitalWrite(LED_AZUL, LOW);
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_AMARILLO, LOW);

  dht.begin();

  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  LoRa.setPins(SS_PIN, RST_PIN, DIO0_PIN);

  if (!LoRa.begin(433E6)) {
    Serial.println("Error LoRa");
    while (1);
  }
}

void loop() {
  float h = dht.readHumidity();
  float t = dht.readTemperature(); 

  if (isnan(h) || isnan(t)) {
    digitalWrite(LED_AMARILLO, HIGH);
    digitalWrite(LED_VERDE, LOW);
    t = 0.0; h = 0.0;
  } else {
    digitalWrite(LED_AMARILLO, LOW);
    digitalWrite(LED_VERDE, HIGH);   
  }

  // Empaquetado JSON exacto para el Master_BASE
  String payload = "{\"id\":" + String(NODO_ID) + ",\"temp\":" + String(t, 1) + ",\"hum\":" + String(h, 1) + "}";

  LoRa.beginPacket();
  LoRa.print(payload);
  LoRa.endPacket();

  digitalWrite(LED_AZUL, HIGH);
  delay(100);
  digitalWrite(LED_AZUL, LOW);

  Serial.println("Enviado -> " + payload);
  delay(5000); 
}