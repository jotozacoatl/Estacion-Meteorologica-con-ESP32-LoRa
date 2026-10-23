#include <SPI.h>
#include <LoRa.h>
#include <DHT.h>

// CAMBIAR ESTE ID SEGÚN EL NODO: 1, 2 o 3
const int NODO_ID = 1; 

// Configuración del sensor DHT11
#define DHTPIN 4     
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// Pin analógico para el sensor fotosensible (LDR - sirve tanto para módulos de 3 o 4 pines usando AO)
#define PIN_LDR 34

// Pines LoRa-02
#define SCK_PIN   18
#define MISO_PIN  19
#define MOSI_PIN  23
#define SS_PIN    5
#define RST_PIN   14
#define DIO0_PIN  26

// Pines para los LEDs Indicadores
#define LED_AZUL     27  
#define LED_VERDE    25  
#define LED_AMARILLO 33  
#define LED_ROJO     32  

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(PIN_LDR, INPUT);
  pinMode(LED_AZUL, OUTPUT);
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_AMARILLO, OUTPUT);
  pinMode(LED_ROJO, OUTPUT);

  digitalWrite(LED_AZUL, LOW);
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_AMARILLO, LOW);
  digitalWrite(LED_ROJO, LOW);

  dht.begin();

  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  LoRa.setPins(SS_PIN, RST_PIN, DIO0_PIN);

  if (!LoRa.begin(433E6)) {
    Serial.println("¡Error al iniciar LoRa!");
    while (1);
  }
  LoRa.enableCrc();
  Serial.printf("Nodo esclavo %d inicializado (DHT11 + LDR).\n", NODO_ID);
}

void loop() {
  // Lectura DHT11
  float h = dht.readHumidity();
  float t = dht.readTemperature(); 

  // Lectura LDR mapeada de 1 a 100
  int valorCrudo = analogRead(PIN_LDR);
  int luz = map(valorCrudo, 0, 4095, 1, 100);
  luz = constrain(luz, 1, 100);

  if (isnan(h) || isnan(t)) {
    Serial.println("Error al leer datos del sensor DHT11!");
    digitalWrite(LED_AMARILLO, HIGH); 
    digitalWrite(LED_VERDE, LOW);
    digitalWrite(LED_ROJO, HIGH);    
    t = 0.0;
    h = 0.0;
  } else {
    digitalWrite(LED_AMARILLO, LOW);
    digitalWrite(LED_VERDE, HIGH);   
    digitalWrite(LED_ROJO, LOW);
  }

  // Construir trama JSON enviando Temperatura, Humedad y Luz
  String payload = "{\"id\": " + String(NODO_ID) + 
                   ", \"temp\": " + String(t, 1) + 
                   ", \"hum\": " + String(h, 1) + 
                   ", \"luz\": " + String(luz) + "}";

  LoRa.beginPacket();
  LoRa.print(payload);
  LoRa.endPacket();

  digitalWrite(LED_AZUL, HIGH);
  delay(100);
  digitalWrite(LED_AZUL, LOW);

  Serial.print("Trama RF transmitida -> ");
  Serial.println(payload);

  delay(5000);
}
