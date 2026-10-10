#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- CONFIGURACIÓN PANTALLA OLED ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- PINES LORA ---
#define SCK_PIN   18
#define MISO_PIN  19
#define MOSI_PIN  23
#define SS_PIN    5
#define RST_PIN   14
#define DIO0_PIN  26

// --- PINES LEDS ---
#define LED_VERDE    25  
#define LED_AMARILLO 33  
#define LED_AZUL     27  
#define LED_ROJO     32 

// --- PINES BOTONES (Usan Pull-up interno, conectan a GND) ---
#define PIN_BTN_UP     13
#define PIN_BTN_DOWN   4
#define PIN_BTN_SELECT 16

// --- CREDENCIALES AP (Modo Local) ---
const char* ssid_ap = "Estacion Meteorologica";
const char* password_ap = "12345678Password";
WebServer server(80);

// --- CREDENCIALES MULTICONEXIÓN (Modo Nube / ThingSpeak) ---
const char* ssid1 = "El Cuartico";
const char* pass1 = "cC577EXT";
const char* ssid2 = "WiFi de Perro";
const char* pass2 = "28158650";
const char* ssid3 = "Cubagua";
const char* pass3 = "23048874";

String serverName = "http://api.thingspeak.com/update?api_key=HLC0PYQ3MWIXKI7T";
unsigned long lastThingSpeakTime = 0;
const unsigned long thingSpeakDelay = 15000;

// --- VARIABLES DE CONTROL Y MENÚ ---
enum ModoOperacion { MODO_AP, MODO_THINGSPEAK };
ModoOperacion modoActual = MODO_AP; 

int posicionMenu = 0;
const int TOTAL_MENU_ITEMS = 4;
bool lastStateUp = HIGH;
bool lastStateDown = HIGH;
bool lastStateSelect = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

unsigned long lastOledUpdate = 0;
const unsigned long TIMEOUT_CONEXION = 10000; // 10 segundos sin señal = Desconectado

struct NodoEsclavo {
  String temp = "0";
  String hum = "0";
  int rssi = 0;
  unsigned long ultimaRecepcion = 0;
};
NodoEsclavo esclavo1, esclavo2;

// --- FUNCIONES DE RED ---
void conectarWiFi() {
  WiFi.mode(WIFI_STA);
  Serial.print("Buscando red: "); Serial.println(ssid1);
  WiFi.begin(ssid1, pass1);
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 7000) { delay(500); }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Probando respaldo: "); Serial.println(ssid2);
    WiFi.disconnect(); WiFi.begin(ssid2, pass2);
    inicio = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - inicio < 7000) { delay(500); }
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Probando Cubagua...");
    WiFi.disconnect(); WiFi.begin(ssid3, pass3);
    inicio = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - inicio < 7000) { delay(500); }
  }
}

void alternarModoRed() {
  display.clearDisplay();
  display.setCursor(0, 15);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  if (modoActual == MODO_AP) {
    modoActual = MODO_THINGSPEAK;
    display.println("Modo Nube Activado!");
    display.println("Buscando Internet...");
    display.display();
    
    server.stop();
    WiFi.softAPdisconnect(true);
    conectarWiFi();
  } else {
    modoActual = MODO_AP;
    display.println("Modo Local Activado!");
    display.println("Iniciando AP...");
    display.display();
    
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid_ap, password_ap);
    server.begin();
  }
  delay(1500);
}

// --- INTERFAZ WEB ---
String generarPaginaHTML() {
  unsigned long ahora = millis();
  bool e1Activo = (esclavo1.ultimaRecepcion != 0) && (ahora - esclavo1.ultimaRecepcion < TIMEOUT_CONEXION);
  bool e2Activo = (esclavo2.ultimaRecepcion != 0) && (ahora - esclavo2.ultimaRecepcion < TIMEOUT_CONEXION);

  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Monitor Multinodo LoRa</title>";
  html += "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;background:#f0f2f5;text-align:center;padding:15px;margin:0;}";
  html += "h1{color:#333;font-size:24px;margin-bottom:15px;}";
  html += ".container{display:flex;flex-wrap:wrap;justify-content:center;gap:15px;}";
  html += ".card{background:white;padding:15px;border-radius:12px;box-shadow:0 4px 10px rgba(0,0,0,0.1);width:340px;box-sizing:border-box;}";
  html += "h2{color:#444;border-bottom:2px solid #eee;padding-bottom:5px;font-size:18px;}";
  html += ".dato{font-size:16px;margin:6px 0;color:#007bff;} .rssi{color:#6c757d;font-size:12px;}";
  html += ".status{display:inline-block;padding:4px 10px;border-radius:12px;font-size:11px;font-weight:bold;color:white;}";
  html += ".online{background:#28a745;} .offline{background:#dc3545;}";
  html += ".chart-container{position:relative;width:100%;height:180px;margin-top:10px;}";
  html += "</style></head><body>";
  
  html += "<h1>Estacion Central LoRa Master</h1>";
  html += "<div class='container'>";

  html += "<div class='card'><h2>Nodo Esclavo 1</h2>";
  html += "<span class='status " + String(e1Activo ? "online'>En Línea" : "offline'>Desconectado") + "</span>";
  html += "<div class='dato'>🌡️ Temp: <b id='t1'>" + esclavo1.temp + "</b> °C</div>";
  html += "<div class='dato'>💧 Humedad: <b id='h1'>" + esclavo1.hum + "</b> %</div>";
  html += "<p class='rssi'>Señal RSSI: " + String(esclavo1.rssi) + " dBm</p>";
  html += "<div class='chart-container'><canvas id='chartE1'></canvas></div></div>";

  html += "<div class='card'><h2>Nodo Esclavo 2</h2>";
  html += "<span class='status " + String(e2Activo ? "online'>En Línea" : "offline'>Desconectado") + "</span>";
  html += "<div class='dato'>🌡️ Temp: <b id='t2'>" + esclavo2.temp + "</b> °C</div>";
  html += "<div class='dato'>💧 Humedad: <b id='h2'>" + esclavo2.hum + "</b> %</div>";
  html += "<p class='rssi'>Señal RSSI: " + String(esclavo2.rssi) + " dBm</p>";
  html += "<div class='chart-container'><canvas id='chartE2'></canvas></div></div>";

  html += "</div>";

  html += "<script>";
  html += "const maxDataPoints = 15;";
  html += "function crearConfig(labelTemp, labelHum) { return { type: 'line', data: { labels: [], datasets: [ { label: labelTemp, data: [], borderColor: '#ff6384', backgroundColor: 'rgba(255,99,132,0.1)', tension: 0.3, fill: true }, { label: labelHum, data: [], borderColor: '#36a2eb', backgroundColor: 'rgba(54,162,235,0.1)', tension: 0.3, fill: true } ]}, options: { responsive: true, maintainAspectRatio: false, scales: { y: { beginAtZero: false } } } }; }";
  html += "const ctx1 = document.getElementById('chartE1').getContext('2d'); const chart1 = new Chart(ctx1, crearConfig('Temp 1 (C)', 'Hum 1 (%)'));";
  html += "const ctx2 = document.getElementById('chartE2').getContext('2d'); const chart2 = new Chart(ctx2, crearConfig('Temp 2 (C)', 'Hum 2 (%)'));";
  html += "function actualizarDatos() { fetch('/datos-json').then(response => response.json()).then(data => {";
  html += "  document.getElementById('t1').innerText = data.e1.temp; document.getElementById('h1').innerText = data.e1.hum;";
  html += "  document.getElementById('t2').innerText = data.e2.temp; document.getElementById('h2').innerText = data.e2.hum;";
  html += "  const hora = new Date().toLocaleTimeString();";
  html += "  if(chart1.data.labels.length >= maxDataPoints) { chart1.data.labels.shift(); chart1.data.datasets[0].data.shift(); chart1.data.datasets[1].data.shift(); }";
  html += "  chart1.data.labels.push(hora); chart1.data.datasets[0].data.push(parseFloat(data.e1.temp)); chart1.data.datasets[1].data.push(parseFloat(data.e1.hum)); chart1.update();";
  html += "  if(chart2.data.labels.length >= maxDataPoints) { chart2.data.labels.shift(); chart2.data.datasets[0].data.shift(); chart2.data.datasets[1].data.shift(); }";
  html += "  chart2.data.labels.push(hora); chart2.data.datasets[0].data.push(parseFloat(data.e2.temp)); chart2.data.datasets[1].data.push(parseFloat(data.e2.hum)); chart2.update();";
  html += "}); } setInterval(actualizarDatos, 3000);";
  html += "</script></body></html>";
  return html;
}

// --- ACTUALIZACIÓN DE PANTALLA OLED ---
void actualizarPantalla() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextColor(SSD1306_WHITE);

  display.println("--- MENU MASTER ---");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
  display.setCursor(0, 15);

  // Evaluar estado de los nodos para actualizar la pantalla dinámica
  unsigned long ahora = millis();
  bool e1Conectado = (esclavo1.ultimaRecepcion != 0) && (ahora - esclavo1.ultimaRecepcion < TIMEOUT_CONEXION);
  bool e2Conectado = (esclavo2.ultimaRecepcion != 0) && (ahora - esclavo2.ultimaRecepcion < TIMEOUT_CONEXION);

  switch (posicionMenu) {
    case 0:
      display.println("> 1. ESTADO RED <");
      display.printf("N1: %s\n", e1Conectado ? "CONECTADO" : "DESCONECTADO");
      display.printf("N2: %s\n", e2Conectado ? "CONECTADO" : "DESCONECTADO");
      break;

    case 1:
      display.println("> 2. DETALLE NODO 1 <");
      if (e1Conectado) {
        display.printf("Temp: %s C\n", esclavo1.temp.c_str());
        display.printf("Hum:  %s %%\n", esclavo1.hum.c_str());
        display.printf("RSSI: %d dBm\n", esclavo1.rssi);
      } else {
        display.println("\n ** DESCONECTADO **");
      }
      break;

    case 2:
      display.println("> 3. DETALLE NODO 2 <");
      if (e2Conectado) {
        display.printf("Temp: %s C\n", esclavo2.temp.c_str());
        display.printf("Hum:  %s %%\n", esclavo2.hum.c_str());
        display.printf("RSSI: %d dBm\n", esclavo2.rssi);
      } else {
        display.println("\n ** DESCONECTADO **");
      }
      break;

    case 3:
      display.println("> 4. MODO TRABAJO <");
      if (modoActual == MODO_AP) {
        display.println("Modo: WEB LOCAL (AP)");
        display.println("Ir a IP: 192.168.4.1");
      } else {
        display.println("Modo: NUBE (THINGSPEAK)");
        if (WiFi.status() == WL_CONNECTED) {
          display.println("WIFI CONECTADO!");
        } else {
          display.println("Buscando Red...");
        }
      }
      display.println("");
      display.println("[Sel] Cambiar Modo");
      break;
  }
  display.display();
}

// --- MANEJO DE BOTONES ---
void handleNavegacion() {
  bool readUp     = digitalRead(PIN_BTN_UP);
  bool readDown   = digitalRead(PIN_BTN_DOWN);
  bool readSelect = digitalRead(PIN_BTN_SELECT);

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (readUp == LOW && lastStateUp == HIGH) {
      posicionMenu--;
      if (posicionMenu < 0) posicionMenu = TOTAL_MENU_ITEMS - 1;
      actualizarPantalla();
      lastDebounceTime = millis();
    }
    if (readDown == LOW && lastStateDown == HIGH) {
      posicionMenu++;
      if (posicionMenu >= TOTAL_MENU_ITEMS) posicionMenu = 0;
      actualizarPantalla();
      lastDebounceTime = millis();
    }
    if (readSelect == LOW && lastStateSelect == HIGH) {
      if (posicionMenu == 3) {
        alternarModoRed();
      }
      actualizarPantalla();
      lastDebounceTime = millis();
    }
  }
  lastStateUp     = readUp;
  lastStateDown   = readDown;
  lastStateSelect = readSelect;
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_BTN_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
  pinMode(PIN_BTN_SELECT, INPUT_PULLUP);
  
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_AZUL, OUTPUT);
  pinMode(LED_AMARILLO, OUTPUT);
  pinMode(LED_ROJO, OUTPUT);

  Wire.begin(21, 22); 
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid_ap, password_ap);

  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  LoRa.setPins(SS_PIN, RST_PIN, DIO0_PIN);
  if (!LoRa.begin(433E6)) { while(1); }
  LoRa.enableCrc();

  server.on("/", []() { server.send(200, "text/html", generarPaginaHTML()); });
  server.on("/datos-json", []() {
    String json = "{\"e1\":{\"temp\":" + esclavo1.temp + ",\"hum\":" + esclavo1.hum + "},\"e2\":{\"temp\":" + esclavo2.temp + ",\"hum\":" + esclavo2.hum + "}}";
    server.send(200, "application/json", json);
  });
  server.begin();

  actualizarPantalla();
}

void loop() {
  if (modoActual == MODO_AP) {
    server.handleClient(); 
  }
  
  handleNavegacion();

  // Recepción de paquetes LoRa JSON
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String paquete = "";
    while (LoRa.available()) {
      paquete += (char)LoRa.read();
    }
    int rssi = LoRa.packetRssi();

    digitalWrite(LED_AZUL, HIGH);
    delay(100);
    digitalWrite(LED_AZUL, LOW);

    int posId = paquete.indexOf("\"id\":");
    int posTemp = paquete.indexOf(",\"temp\":");
    int posHum = paquete.indexOf(",\"hum\":");
    int posFin = paquete.indexOf("}");

    if (posTemp != -1 && posHum != -1) {
      int idNodo = 1;
      if (posId != -1) idNodo = paquete.substring(posId + 5, posTemp).toInt();

      String tStr = paquete.substring(posTemp + 8, posHum);
      String hStr = paquete.substring(posHum + 7, posFin);

      if (idNodo == 1) {
        esclavo1.temp = tStr; esclavo1.hum = hStr;
        esclavo1.rssi = rssi; esclavo1.ultimaRecepcion = millis();
      } else if (idNodo == 2) {
        esclavo2.temp = tStr; esclavo2.hum = hStr;
        esclavo2.rssi = rssi; esclavo2.ultimaRecepcion = millis();
      }

      float t = tStr.toFloat(); float h = hStr.toFloat();
      if ((t == 0.0 && h == 0.0) || t < -10.0 || t > 50.0) {
        digitalWrite(LED_ROJO, HIGH);
      } else {
        digitalWrite(LED_ROJO, LOW);
      }
      actualizarPantalla(); 
    }
  }

  // Rutina ThingSpeak
  if (modoActual == MODO_THINGSPEAK && millis() - lastThingSpeakTime >= thingSpeakDelay) {
    if (WiFi.status() == WL_CONNECTED) {
      WiFiClient client;
      HTTPClient http;
      String serverPath = serverName + "&field1=" + esclavo1.temp + "&field2=" + esclavo1.hum + "&field3=" + esclavo2.temp + "&field4=" + esclavo2.hum;
      http.begin(client, serverPath.c_str());
      int httpResponseCode = http.GET();
      if (httpResponseCode > 0) {
        Serial.print("Enviado a ThingSpeak. Codigo HTTP: "); Serial.println(httpResponseCode);
      }
      http.end();
    }
    lastThingSpeakTime = millis();
  }

  // Comprobar estado de conexión para LEDs Semáforo
  unsigned long ahora = millis();
  bool e1Conectado = (esclavo1.ultimaRecepcion != 0) && (ahora - esclavo1.ultimaRecepcion < TIMEOUT_CONEXION);
  bool e2Conectado = (esclavo2.ultimaRecepcion != 0) && (ahora - esclavo2.ultimaRecepcion < TIMEOUT_CONEXION);

  if (e1Conectado || e2Conectado) {
    digitalWrite(LED_VERDE, HIGH); digitalWrite(LED_AMARILLO, LOW);
  } else {
    digitalWrite(LED_VERDE, LOW); digitalWrite(LED_AMARILLO, HIGH);
  }

  // Refresco de pantalla dinámico (para actualizar los "Desconectado" si se vence el tiempo)
  if (millis() - lastOledUpdate > 2000) {
    actualizarPantalla();
    lastOledUpdate = millis();
  }
}