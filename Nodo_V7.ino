#include <SPI.h>
#include <LoRa.h>
#include <DHT.h>

const int NODO_ID = 1; // Cambiar a 2 para el Nodo 2 y a 3 para el Nodo 3

// Configuración del sensor DHT11
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

// Pines para los LEDs Indicadores
#define LED_AZUL     27  
#define LED_VERDE    25  
#define LED_AMARILLO 33  
#define LED_ROJO     32  

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Configuración de pines de LEDs
  pinMode(LED_AZUL, OUTPUT);
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_AMARILLO, OUTPUT);
  pinMode(LED_ROJO, OUTPUT);

  digitalWrite(LED_AZUL, LOW);
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_AMARILLO, LOW);
  digitalWrite(LED_ROJO, LOW);

  // Inicializar sensor DHT11
  dht.begin();

  // Inicializar bus SPI para LoRa
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  LoRa.setPins(SS_PIN, RST_PIN, DIO0_PIN);

  if (!LoRa.begin(433E6)) {
    Serial.println("¡Error al iniciar LoRa!");
    while (1);
  }
  LoRa.enableCrc(); // Habilitar verificación de redundancia cíclica
  
  Serial.println("Nodo esclavo inicializado correctamente.");
}

void loop() {
  // Leer temperatura y humedad del DHT11
  float h = dht.readHumidity();
  float t = dht.readTemperature(); 

  // Verificar si las lecturas del sensor son válidas
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

    if ((t == 0.0 && h == 0.0) || t < -10.0 || t > 50.0) {
      digitalWrite(LED_ROJO, HIGH);  
    } else {
      digitalWrite(LED_ROJO, LOW);   
    }
  }

  // Construir el paquete JSON estándar (solo id, temp y hum)
  String payload = "{\"id\": " + String(NODO_ID) + 
                   ", \"temp\": " + String(t, 1) + 
                   ", \"hum\": " + String(h, 1) + "}";

  // Enviar por LoRa
  LoRa.beginPacket();
  LoRa.print(payload);
  LoRa.endPacket();

  // Parpadeo del LED Azul indicando transmisión RF
  digitalWrite(LED_AZUL, HIGH);
  delay(100);
  digitalWrite(LED_AZUL, LOW);

  Serial.print("Trama RF transmitida -> ");
  Serial.println(payload);

  delay(5000); // Intervalo de muestreo de 5 segundos
}
