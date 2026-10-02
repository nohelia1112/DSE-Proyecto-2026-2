#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ======================================================
// PINES Y DIRECCIONES I2C
// ======================================================

#define SDA_PIN 8
#define SCL_PIN 9

#define ADS1115_ADDR  0x48
#define BQ27441_ADDR  0x55


// ======================================================
// PARÁMETROS DEL MILIÓHMETRO
// ======================================================

const float G = 10.0;       // Ganancia INA296A1

// Calibración cero:
//
// LTspice:
// Voffset = 150.415 uV
//
// ADS1115:
// 1 LSB = 62.5 uV

const float ADS_LSB = 0.0000625;

//
// 150.415 / 62.5 = 2.4066
//
// Se toman 2 cuentas como ZERO_RAW
//
const int16_t ZERO_RAW = 2;


// ======================================================
// WiFi
// ======================================================

const char* ssid = "Wokwi-GUEST";
const char* password = "";


// ======================================================
// MQTT
// ======================================================

const char* mqtt_server = "host.wokwi.internal";
const int mqtt_port = 1883;

// Topic donde se publicarán todos los datos
const char* mqtt_topic =
  "uis/miliohmetro/demo01/datos";

WiFiClient espClient;
PubSubClient mqtt(espClient);


// ======================================================
// ADS1115
// ======================================================

void writeRegister(uint8_t reg, uint16_t value) {

  Wire.beginTransmission(ADS1115_ADDR);

  Wire.write(reg);
  Wire.write((value >> 8) & 0xFF);
  Wire.write(value & 0xFF);

  Wire.endTransmission();
}


// ------------------------------------------------------
// Leer conversión diferencial ADS1115
// ------------------------------------------------------

int16_t readConversion(uint16_t config) {

  // Escribir configuración
  writeRegister(0x01, config);

  // Esperar conversión
  delay(10);

  // Seleccionar registro de conversión
  Wire.beginTransmission(ADS1115_ADDR);
  Wire.write(0x00);
  Wire.endTransmission();

  // Pedir 2 bytes
  Wire.requestFrom(ADS1115_ADDR, 2);

  if (Wire.available() < 2) {
    return 0;
  }

  uint8_t msb = Wire.read();
  uint8_t lsb = Wire.read();

  return (int16_t)((msb << 8) | lsb);
}


// ======================================================
// BQ27441
// ======================================================

uint16_t readBQWord(uint8_t reg) {

  // Seleccionar registro
  Wire.beginTransmission(BQ27441_ADDR);
  Wire.write(reg);
  Wire.endTransmission();

  // Solicitar dos bytes
  Wire.requestFrom(BQ27441_ADDR, 2);

  if (Wire.available() < 2) {
    return 0;
  }

  // BQ27441:
  // primero LSB, después MSB
  uint8_t lsb = Wire.read();
  uint8_t msb = Wire.read();

  return ((uint16_t)msb << 8) | lsb;
}


// ======================================================
// CONEXIÓN WiFi
// ======================================================

void conectarWiFi() {

  Serial0.println();
  Serial0.print("Conectando a WiFi");

  WiFi.mode(WIFI_STA);

  WiFi.begin(
    ssid,
    password,
    6
  );

  while (WiFi.status() != WL_CONNECTED) {

    delay(500);

    Serial0.print(".");
  }

  Serial0.println();
  Serial0.println("WiFi conectado");

  Serial0.print("IP: ");
  Serial0.println(WiFi.localIP());
}


// ======================================================
// CONEXIÓN MQTT
// ======================================================

void conectarMQTT() {

  while (!mqtt.connected()) {

    Serial0.print(
      "Conectando al broker MQTT..."
    );

    // Crear ID relativamente único
    String clientId =
      "MiliohmetroESP32C3-";

    clientId +=
      String(
        (uint32_t)
        (ESP.getEfuseMac() & 0xFFFFFFFF),
        HEX
      );

    // Intentar conexión
    if (mqtt.connect(clientId.c_str())) {

      Serial0.println(" conectado");

    } else {

      Serial0.print(" error = ");
      Serial0.println(mqtt.state());

      Serial0.println(
        "Reintentando en 2 segundos..."
      );

      delay(2000);
    }
  }
}


// ======================================================
// SETUP
// ======================================================

void setup() {

  // Serial
  Serial0.begin(115200);

  delay(300);


  // ----------------------------------------------------
  // I2C
  // ----------------------------------------------------

  Wire.begin(
    SDA_PIN,
    SCL_PIN
  );


  // ----------------------------------------------------
  // Mensaje inicial
  // ----------------------------------------------------

  Serial0.println();
  Serial0.println(
    "================================="
  );

  Serial0.println(
    " MILIOHMETRO DIGITAL"
  );

  Serial0.println(
    " ADS1115 + BQ27441 + MQTT"
  );

  Serial0.println(
    "================================="
  );


  // ----------------------------------------------------
  // WiFi
  // ----------------------------------------------------

  conectarWiFi();


  // ----------------------------------------------------
  // MQTT
  // ----------------------------------------------------

  mqtt.setServer(
    mqtt_server,
    mqtt_port
  );

  conectarMQTT();
}


// ======================================================
// LOOP
// ======================================================

void loop() {

  // ====================================================
  // 0. MANTENER CONEXIÓN
  // ====================================================

  // Si se perdió WiFi
  if (WiFi.status() != WL_CONNECTED) {
    conectarWiFi();
  }

  // Si se perdió MQTT
  if (!mqtt.connected()) {
    conectarMQTT();
  }

  // Mantener conexión MQTT activa
  mqtt.loop();

  // ====================================================
  // 1. MEDICIÓN DE RESISTENCIA
  // ====================================================


  // ----------------------------------------------------
  // Lectura de voltaje diferencial
  // ----------------------------------------------------

  // ADS1115:
  // AIN0 - AIN1
  // PGA = ±2.048 V

  int16_t rawDiff =
    readConversion(0x8583);



  // ----------------------------------------------------
  // Lectura de corriente real de prueba
  // ----------------------------------------------------

  // ADS1115:
  // AIN2 - GND
  // PGA = ±2.048 V

  int16_t rawCurrent =
    readConversion(0x6383);



  // Convertir corriente a voltaje

  // 1 LSB = 62.5 uV

  float Vcurrent =
    rawCurrent * ADS_LSB;



  // En LTSpice:
  // 1 V representa 1 A

  float Itest =
    Vcurrent;


  // Evitar división por cero

  if (Itest < 0.001) {

    Itest = 0.001;

  }



  // ----------------------------------------------------
  // Calibración cero
  // ----------------------------------------------------

  int16_t rawCorr =
    rawDiff - ZERO_RAW;



  // Evitar pequeños valores negativos

  if (rawCorr < 0) {

    rawCorr = 0;

  }



  // ----------------------------------------------------
  // Conversión a voltaje
  // ----------------------------------------------------

  float Vadc =
    rawDiff * ADS_LSB;


  float Vcorr =
    rawCorr * ADS_LSB;



  // ----------------------------------------------------
  // Calcular resistencia
  // ----------------------------------------------------

  // Vcorr = G * ITEST * Rx
  //
  // Rx = Vcorr / (G * ITEST)


  float Rx_ohm =
    Vcorr / (G * Itest);



  float Rx_mohm =
    Rx_ohm * 1000.0;

  // ====================================================
  // 2. DATOS DE BATERÍA
  // ====================================================

  // Voltage()
  uint16_t batteryVoltage_mV =
      readBQWord(0x04);

  float batteryVoltage_V =
      batteryVoltage_mV / 1000.0f;


  // RemainingCapacity()
  uint16_t remainingCapacity =
    readBQWord(0x0C);


  // FullChargeCapacity()
  uint16_t fullCapacity =
    readBQWord(0x0E);


  // AverageCurrent()
  int16_t batteryCurrent =
    (int16_t)readBQWord(0x10);


  // StateOfCharge()
  uint16_t batterySOC =
    readBQWord(0x1C);


  // ====================================================
  // 3. ESTADO DE CARGA
  // ====================================================

  bool charging = false;

  float chargeTimeMin = 0.0;


  // En nuestro modelo:
  //
  // corriente positiva = cargando
  // corriente negativa = descargando

  if (batteryCurrent > 0) {

    charging = true;


    // Evitar que aparezca capacidad negativa
    float missingCapacity = 0;

    if (fullCapacity > remainingCapacity) {

      missingCapacity =
        fullCapacity - remainingCapacity;
    }


    // Tiempo aproximado de carga
    chargeTimeMin =
      (
        missingCapacity /
        (float)batteryCurrent
      ) * 60.0;
  }


  // ====================================================
  // 4. MOSTRAR RESISTENCIA
  // ====================================================

  Serial0.println();
  Serial0.println(
    "---------------------------------"
  );

  Serial0.println(
    "MEDICION DE RESISTENCIA"
  );

  Serial0.println(
    "---------------------------------"
  );


  //Serial0.print("RAW: ");
  //Serial0.println(rawDiff);


  //Serial0.print("RAW corregido: ");
  //Serial0.println(rawCorr);


  Serial0.print("V_ADC: ");
  Serial0.print(
    Vadc * 1000.0,
    4
  );

  Serial0.println(" mV");


  Serial0.print("V_CORR: ");
  Serial0.print(
    Vcorr * 1000.0,
    4
  );

  Serial0.println(" mV");


  Serial0.print("R_x: ");
  Serial0.print(
    Rx_mohm,
    8
  );

  Serial0.println(" mOhm");


  // ====================================================
  // 5. MOSTRAR BATERÍA
  // ====================================================

Serial0.println();
Serial0.println("---------------------------------");
Serial0.println("BATERIA");
Serial0.println("---------------------------------");

Serial0.print("Bateria: ");
Serial0.print(batterySOC);
Serial0.println(" %");

Serial0.print("Voltaje Li-Po: ");
Serial0.print(batteryVoltage_V, 3);
Serial0.println(" V");

if (charging) {

  Serial0.println("Estado: CARGANDO");

  Serial0.print("Tiempo estimado de carga: ");
  Serial0.print(chargeTimeMin, 1);
  Serial0.println(" min");

} else {

  Serial0.println("Estado: NO CARGANDO");
}


  // ====================================================
  // 6. TIEMPO DE CARGA
  // ====================================================

  if (charging) {

    Serial0.println(
      "Estado: CARGANDO"
    );

    Serial0.print(
      "Tiempo estimado: "
    );

    Serial0.print(
      chargeTimeMin,
      1
    );

    Serial0.println(" min");

  } else {

    Serial0.println(
      "Estado: DESCARGANDO / NO CARGANDO"
    );

    Serial0.println(
      "Tiempo de carga: --"
    );
  }


// ====================================================
// 7. CREAR JSON PARA LA APP
// ====================================================

  String json = "{";

  json += "\"resistance_mohm\":";
  json += String(Rx_mohm, 8);

  json += ",\"battery_percent\":";
  json += String(batterySOC);

  json += ",\"battery_voltage\":";
  json += String(batteryVoltage_V, 3);

  json += ",\"charging\":";
  json += charging ? "true" : "false";

  json += ",\"charge_time_min\":";
  json += String(chargeTimeMin, 1);

  json += "}";


  // ====================================================
  // 8. PUBLICAR MQTT
  // ====================================================

  bool enviado =
    mqtt.publish(
      mqtt_topic,
      json.c_str(),
      true
    );


  // ====================================================
  // 9. MOSTRAR JSON
  // ====================================================

  Serial0.println();

  Serial0.println(
    "---------------------------------"
  );

  Serial0.println(
    "MQTT"
  );

  Serial0.println(
    "---------------------------------"
  );


  Serial0.print("Topic: ");
  Serial0.println(mqtt_topic);


  Serial0.print("JSON: ");
  Serial0.println(json);


  if (enviado) {

    Serial0.println(
      "Mensaje MQTT enviado"
    );

  } else {

    Serial0.println(
      "ERROR enviando MQTT"
    );
  }


  Serial0.println(
    "================================="
  );


  // Esperar antes de próxima medición
  delay(5000);
}