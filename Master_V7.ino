#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Configuración de la pantalla OLED I2C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Definición de pines para el bus SPI del módulo LoRa
#define SCK_PIN     18
#define MISO_PIN    19
#define MOSI_PIN    23
#define SS_PIN      5
#define RST_PIN     14
#define DIO0_PIN    26

// Definición de pines para los LEDs indicadores de estado
#define LED_VERDE    25  
#define LED_AMARILLO 33  
#define LED_AZUL     27  
#define LED_ROJO     32  

// Definición de pines para los pulsadores de navegación del menú
#define PIN_BTN_UP     13
#define PIN_BTN_DOWN   4
#define PIN_BTN_SELECT 16

// Credenciales para el punto de acceso AP local del ESP32
const char* ssid_ap = "Estacion Meteorologica";
const char* password_ap = "12345678Password";

// Credenciales de redes Wi-Fi externas para los modos de conexión
const char* ssid1 = "El Cuartico";
const char* pass1 = "cC577EXT";
const char* ssid_hotspot = "WolFi de Perro";
const char* pass_hotspot = "28158650";

// URL y parámetros para el envío de datos a la plataforma ThingSpeak
String serverName = "http://api.thingspeak.com/update?api_key=HLC0PYQ3MWIXKI7T";
unsigned long lastThingSpeakTime = 0;
const unsigned long thingSpeakDelay = 15000;

// Enumeración de los modos de operación del sistema Master
enum ModoOperacion { MODO_AP_LOCAL, MODO_WIFI_EXTERNO, MODO_THINGSPEAK };
ModoOperacion modoActual = MODO_AP_LOCAL;

// Instancia del servidor web en el puerto 80
WebServer server(80);

// Estructura para almacenar las variables y estado de cada nodo esclavo
struct NodoEsclavo {
  String temp = "0";
  String hum = "0";
  int rssi = 0;
  unsigned long ultimaRecepcion = 0;
};

// Cantidad total de nodos esclavos configurados en el sistema
const int TOTAL_NODOS = 3;
NodoEsclavo esclavos[TOTAL_NODOS];

// Tiempo límite de inactividad para considerar un nodo como desconectado (10 segundos)
const unsigned long TIMEOUT_CONEXION = 10000;

// Variables para el control de la interfaz de usuario en la pantalla OLED y antirrebote
int posicionMenu = 0;
const int TOTAL_MENU_ITEMS = 6; 
bool lastStateUp = HIGH;
bool lastStateDown = HIGH;
bool lastStateSelect = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;
unsigned long lastOledUpdate = 0;

// Página HTML almacenada en la memoria flash (PROGMEM) que carga chart.js de forma local desde el ESP32
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <title>Monitor Trinodo LoRa</title>
  <script src="/chart.js"></script>
  <style>
    body{font-family:Arial,sans-serif;background:#f0f2f5;text-align:center;padding:15px;margin:0;}
    h1{color:#333;font-size:22px;margin-bottom:15px;}
    .container{display:flex;flex-wrap:wrap;justify-content:center;gap:15px;}
    .card{background:white;padding:15px;border-radius:12px;box-shadow:0 4px 10px rgba(0,0,0,0.1);width:360px;box-sizing:border-box;}
    h2{color:#444;border-bottom:2px solid #eee;padding-bottom:5px;font-size:18px;}
    .dato{font-size:15px;margin:6px 0;color:#007bff;} .rssi{color:#6c757d;font-size:12px;}
    .status{display:inline-block;padding:4px 10px;border-radius:12px;font-size:11px;font-weight:bold;color:white;}
    .online{background:#28a745;} .offline{background:#dc3545;}
    .chart-container{position:relative;margin-top:10px;height:190px;}
  </style>
</head>
<body>
  <h1>Estacion Central LoRa Master</h1>
  <div class='container'>
    <div class='card'><h2>Nodo Esclavo 1</h2>
      <span id='status1' class='status offline'>Desconectado</span>
      <div class='dato'>��️ Temp: <b id='t1'>0</b> °C</div>
      <div class='dato'>�� Humedad: <b id='h1'>0</b> %</div>
      <p id='rssi1' class='rssi'>Señal RSSI: 0 dBm</p>
      <div class='chart-container'><canvas id='chart1'></canvas></div>
    </div>
    <div class='card'><h2>Nodo Esclavo 2</h2>
      <span id='status2' class='status offline'>Desconectado</span>
      <div class='dato'>��️ Temp: <b id='t2'>0</b> °C</div>
      <div class='dato'>�� Humedad: <b id='h2'>0</b> %</div>
      <p id='rssi2' class='rssi'>Señal RSSI: 0 dBm</p>
      <div class='chart-container'><canvas id='chart2'></canvas></div>
    </div>
    <div class='card'><h2>Nodo Esclavo 3</h2>
      <span id='status3' class='status offline'>Desconectado</span>
      <div class='dato'>��️ Temp: <b id='t3'>0</b> °C</div>
      <div class='dato'>�� Humedad: <b id='h3'>0</b> %</div>
      <p id='rssi3' class='rssi'>Señal RSSI: 0 dBm</p>
      <div class='chart-container'><canvas id='chart3'></canvas></div>
    </div>
  </div>
  <script>
    function crearGrafica(canvasId) {
      const ctx = document.getElementById(canvasId).getContext('2d');
      return new Chart(ctx, {
        type: 'line',
        data: {
          labels: [],
          datasets: [
            { label: 'Temp (°C)', data: [], borderColor: '#ff6384', backgroundColor: 'rgba(255,99,132,0.1)', tension: 0.3, fill: true },
            { label: 'Hum (%)', data: [], borderColor: '#36a2eb', backgroundColor: 'rgba(54,162,235,0.1)', tension: 0.3, fill: true }
          ]
        },
        options: { 
          responsive: true, 
          maintainAspectRatio: false, 
          scales: { y: { beginAtZero: true, max: 100 } } 
        }
      });
    }

    const chart1 = crearGrafica('chart1');
    const chart2 = crearGrafica('chart2');
    const chart3 = crearGrafica('chart3');

    function actualizarGrafica(chart, time, temp, hum) {
      chart.data.labels.push(time);
      chart.data.datasets[0].data.push(temp);
      chart.data.datasets[1].data.push(hum);
      if(chart.data.labels.length > 10) {
        chart.data.labels.shift();
        chart.data.datasets[0].data.shift();
        chart.data.datasets[1].data.shift();
      }
      chart.update();
    }

    function actualizarDatos() {
      fetch('/datos-json').then(r => r.json()).then(d => {
        const hora = new Date().toLocaleTimeString([], {hour: '2-digit', minute:'2-digit', second:'2-digit'});
        function procesar(obj, id, chart) {
          document.getElementById('t' + id).innerText = obj.temp;
          document.getElementById('h' + id).innerText = obj.hum;
          document.getElementById('rssi' + id).innerText = 'Señal RSSI: ' + obj.rssi + ' dBm';
          let st = document.getElementById('status' + id);
          st.className = 'status ' + (obj.activo ? 'online' : 'offline');
          st.innerText = obj.activo ? 'En Línea' : 'Desconectado';
          actualizarGrafica(chart, hora, parseFloat(obj.temp), parseFloat(obj.hum));
        }
        procesar(d.e1, 1, chart1);
        procesar(d.e2, 2, chart2);
        procesar(d.e3, 3, chart3);
      }).catch(err => console.log(err));
    }
    setInterval(actualizarDatos, 2000);
  </script>
</body>
</html>
)rawliteral";

// Función para conectar el ESP32 a una red Wi-Fi externa en modo Estación (STA)
void conectarWifiExterno() {
  WiFi.mode(WIFI_STA);
  Serial.print("Conectando a Hotspot: "); Serial.println(ssid_hotspot);
  WiFi.begin(ssid_hotspot, pass_hotspot);
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - inicio) < 12000) {
    delay(500);
  }
  Serial.print("IP asignada: "); Serial.println(WiFi.localIP());
}

// Función para intentar la conexión multired secuencialmente con respaldo
void conectarMultired() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid1, pass1);
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - inicio) < 6000) { delay(500); }

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect();
    WiFi.begin(ssid_hotspot, pass_hotspot);
    inicio = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - inicio) < 6000) { delay(500); }
  }
}

// Función para cambiar de forma cíclica entre los modos de operación del sistema
void cambiarSiguienteModo() {
  display.clearDisplay();
  display.setCursor(0, 15);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  server.stop();
  WiFi.disconnect(true);
  WiFi.softAPdisconnect(true);

  if (modoActual == MODO_AP_LOCAL) {
    modoActual = MODO_WIFI_EXTERNO;
    display.println("Cambiando a:");
    display.println("WIFI EXTERNO");
    display.display();
    conectarWifiExterno();
    server.begin(); 
  } else if (modoActual == MODO_WIFI_EXTERNO) {
    modoActual = MODO_THINGSPEAK;
    display.println("Cambiando a:");
    display.println("MODO THINGSPEAK");
    display.display();
    conectarMultired(); 
  } else {
    modoActual = MODO_AP_LOCAL;
    display.println("Cambiando a:");
    display.println("MODO AP LOCAL");
    display.display();
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid_ap, password_ap);
    server.begin(); 
  }
  delay(1500);
}

// Función para actualizar los contenidos mostrados en la pantalla OLED según el menú activo
void actualizarPantalla() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextColor(SSD1306_WHITE);
  display.println("--- MENU MASTER ---");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
  display.setCursor(0, 15);

  unsigned long ahora = millis();
  bool e1C = (esclavos[0].ultimaRecepcion != 0) && (ahora - esclavos[0].ultimaRecepcion < TIMEOUT_CONEXION);
  bool e2C = (esclavos[1].ultimaRecepcion != 0) && (ahora - esclavos[1].ultimaRecepcion < TIMEOUT_CONEXION);
  bool e3C = (esclavos[2].ultimaRecepcion != 0) && (ahora - esclavos[2].ultimaRecepcion < TIMEOUT_CONEXION);

  switch (posicionMenu) {
    case 0:
      display.println("> 1. ESTADO RED <");
      display.printf("N1: %s\n", e1C ? "OK" : "OFF");
      display.printf("N2: %s\n", e2C ? "OK" : "OFF");
      display.printf("N3: %s\n", e3C ? "OK" : "OFF");
      break;
    case 1:
      display.println("> 2. DETALLE NODO 1 <");
      if (e1C) { display.printf("Temp: %s C\nHum: %s %%\nRSSI: %d dBm", esclavos[0].temp.c_str(), esclavos[0].hum.c_str(), esclavos[0].rssi); }
      else { display.println("\n ** DESCONECTADO **"); }
      break;
    case 2:
      display.println("> 3. DETALLE NODO 2 <");
      if (e2C) { display.printf("Temp: %s C\nHum: %s %%\nRSSI: %d dBm", esclavos[1].temp.c_str(), esclavos[1].hum.c_str(), esclavos[1].rssi); }
      else { display.println("\n ** DESCONECTADO **"); }
      break;
    case 3:
      display.println("> 4. DETALLE NODO 3 <");
      if (e3C) { display.printf("Temp: %s C\nHum: %s %%\nRSSI: %d dBm", esclavos[2].temp.c_str(), esclavos[2].hum.c_str(), esclavos[2].rssi); }
      else { display.println("\n ** DESCONECTADO **"); }
      break;
    case 4:
      display.println("> 5. INFO SERVIDOR <");
      if (modoActual == MODO_AP_LOCAL) { display.println("Modo: AP Local\nIP: 192.168.4.1"); }
      else if (modoActual == MODO_WIFI_EXTERNO) { display.println("Modo: Wifi Ext\nIP: "); display.print(WiFi.localIP()); }
      else { display.println("Servidor: APAGADO\n(Modo ThingSpeak)"); }
      break;
    case 5:
      display.println("> 6. MODO DE TRABAJO <");
      if (modoActual == MODO_AP_LOCAL) { display.println("Actual: AP LOCAL"); }
      else if (modoActual == MODO_WIFI_EXTERNO) { display.println("Actual: WIFI EXTERNO"); }
      else { display.println("Actual: THINGSPEAK"); }
      display.println("\n[Sel] Cambiar Modo");
      break;
  }
  display.display();
}

// Función para gestionar la lectura de los pulsadores físicos con lógica de antirrebote
void handleNavegacion() {
  bool readUp = digitalRead(PIN_BTN_UP);
  bool readDown = digitalRead(PIN_BTN_DOWN);
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
      if (posicionMenu == 5) {
        cambiarSiguienteModo();
        actualizarPantalla();
      }
      lastDebounceTime = millis();
    }
  }
  lastStateUp = readUp;
  lastStateDown = readDown;
  lastStateSelect = readSelect;
}

// Configuración inicial del sistema, periféricos, radio LoRa y servidor web
void setup() {
  Serial.begin(115200);

  pinMode(PIN_BTN_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
  pinMode(PIN_BTN_SELECT, INPUT_PULLUP);

  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_AMARILLO, OUTPUT);
  pinMode(LED_AZUL, OUTPUT);
  pinMode(LED_ROJO, OUTPUT);

  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_AMARILLO, HIGH);
  digitalWrite(LED_AZUL, LOW);
  digitalWrite(LED_ROJO, LOW);

  Wire.begin(21, 22);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  // Inicializar el sistema de archivos LittleFS
  if(!LittleFS.begin(true)){
    Serial.println("Error al montar LittleFS");
  }

  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  LoRa.setPins(SS_PIN, RST_PIN, DIO0_PIN);
  LoRa.begin(433E6);
  LoRa.enableCrc();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid_ap, password_ap);

  // Enlace de la ruta principal del servidor web con la página HTML estática
  server.on("/", []() { 
    server.send_P(200, "text/html", index_html); 
  });

  // Enlace de la ruta para servir el archivo chart.js directamente desde la memoria LittleFS del ESP32
  server.on("/chart.js", []() {
    File file = LittleFS.open("/chart.js", "r");
    if (!file) {
      server.send(404, "text/plain", "chart.js no encontrado en LittleFS");
      return;
    }
    server.streamFile(file, "application/javascript");
    file.close();
  });

  // Enlace de la ruta para suministrar los datos estructurados en formato JSON a la interfaz web
  server.on("/datos-json", []() {
    unsigned long ahora = millis();
    String json = "{";
    for(int i=0; i<3; i++) {
      bool activo = (esclavos[i].ultimaRecepcion != 0) && (ahora - esclavos[i].ultimaRecepcion < TIMEOUT_CONEXION);
      json += "\"e" + String(i+1) + "\":{\"temp\":" + esclavos[i].temp + 
              ",\"hum\":" + esclavos[i].hum + 
              ",\"rssi\":" + String(esclavos[i].rssi) + 
              ",\"activo\":" + (activo ? "true" : "false") + "}";
      if(i < 2) json += ",";
    }
    json += "}";
    server.send(200, "application/json", json);
  });
  
  server.begin();
  actualizarPantalla();
}

// Bucle principal del programa para gestión de peticiones, radiofrecuencia y lógica general
void loop() {
  if (modoActual != MODO_THINGSPEAK) {
    server.handleClient();
  }
  
  handleNavegacion();

  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String paquete = "";
    while (LoRa.available()) { paquete += (char)LoRa.read(); }
    int rssi = LoRa.packetRssi();

    digitalWrite(LED_AZUL, HIGH); delay(100); digitalWrite(LED_AZUL, LOW);

    int posId = paquete.indexOf("\"id\":");
    int posTemp = paquete.indexOf("\"temp\":");
    int posHum = paquete.indexOf("\"hum\":");
    int posFin = paquete.indexOf("}");

    if (posTemp != -1 && posHum != -1) {
      int idNodo = (posId != -1) ? paquete.substring(posId + 5, paquete.indexOf(',', posId)).toInt() : 1;
      
      int inicioTemp = posTemp + 7;
      int finTemp = paquete.indexOf(',', inicioTemp);
      String tStr = paquete.substring(inicioTemp, finTemp);

      int inicioHum = posHum + 6;
      int finHum = posFin;
      String hStr = paquete.substring(inicioHum, finHum);

      if (idNodo >= 1 && idNodo <= TOTAL_NODOS) {
        int idx = idNodo - 1;
        esclavos[idx].temp = tStr;
        esclavos[idx].hum = hStr;
        esclavos[idx].rssi = rssi;
        esclavos[idx].ultimaRecepcion = millis();
      }

      Serial.println("--------------------------------------------------");
      Serial.printf("[LoRa] Paquete recibido de Nodo %d\n", idNodo);
      Serial.printf("[LoRa] Temperatura: %s °C | Humedad: %s %% | RSSI: %d dBm\n", tStr.c_str(), hStr.c_str(), rssi);
      Serial.println("--------------------------------------------------");

      float t = tStr.toFloat();
      float h = hStr.toFloat();
      if ((t == 0.0 && h == 0.0) || t < -10.0 || t > 50.0) {
        digitalWrite(LED_ROJO, HIGH);
      } else {
        digitalWrite(LED_ROJO, LOW);
      }
    }
  }

  // Lógica para el envío automático de telemetría a la plataforma ThingSpeak según el intervalo configurado
  if (modoActual == MODO_THINGSPEAK && (millis() - lastThingSpeakTime >= thingSpeakDelay)) {
    if (WiFi.status() == WL_CONNECTED) {
      WiFiClient client;
      HTTPClient http;
      String serverPath = serverName + "&field1=" + esclavos[0].temp + "&field2=" + esclavos[0].hum + 
                                       "&field3=" + esclavos[1].temp + "&field4=" + esclavos[1].hum +
                                       "&field5=" + esclavos[2].temp + "&field6=" + esclavos[2].hum;
      http.begin(client, serverPath.c_str());
      http.GET();
      http.end();
    }
    lastThingSpeakTime = millis();
  }

  unsigned long ahora = millis();
  bool e1C = (esclavos[0].ultimaRecepcion != 0) && (ahora - esclavos[0].ultimaRecepcion < TIMEOUT_CONEXION);
  bool e2C = (esclavos[1].ultimaRecepcion != 0) && (ahora - esclavos[1].ultimaRecepcion < TIMEOUT_CONEXION);
  bool e3C = (esclavos[2].ultimaRecepcion != 0) && (ahora - esclavos[2].ultimaRecepcion < TIMEOUT_CONEXION);

  if (e1C || e2C || e3C) {
    digitalWrite(LED_VERDE, HIGH); digitalWrite(LED_AMARILLO, LOW);
  } else {
    digitalWrite(LED_VERDE, LOW); digitalWrite(LED_AMARILLO, HIGH);
  }

  if ((millis() - lastOledUpdate) > 2000) {
    actualizarPantalla();
    lastOledUpdate = millis();
  }

  delay(50);
}
