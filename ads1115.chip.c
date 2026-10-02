#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define I2C_ADDRESS 0x48

typedef struct {
  i2c_dev_t i2c_dev;
  i2c_config_t i2c_config;

  pin_t a0;
  pin_t a1;
  pin_t a2;
  pin_t a3;

  uint16_t config_register;
  int16_t conversion_register;

  uint8_t pointer_register;

  uint8_t write_index;
  uint8_t read_index;

  uint8_t temp_msb;

} chip_state_t;


// ======================================================
// Calcula la conversión ADC
// ======================================================

void simulate_adc_conversion(chip_state_t *chip) {

  float va0 = pin_adc_read(chip->a0);
  float va1 = pin_adc_read(chip->a1);
  float va2 = pin_adc_read(chip->a2);
  float va3 = pin_adc_read(chip->a3);

  // Bits MUX = config[14:12]
  uint8_t mux = (chip->config_register >> 12) & 0x07;

  float vin = 0.0;

  switch (mux) {

    // Diferenciales
    case 0:
      vin = va0 - va1;   // AIN0 - AIN1
      break;

    case 1:
      vin = va0 - va3;   // AIN0 - AIN3
      break;

    case 2:
      vin = va1 - va3;   // AIN1 - AIN3
      break;

    case 3:
      vin = va2 - va3;   // AIN2 - AIN3
      break;

    // Single-ended
    case 4:
      vin = va0;
      break;

    case 5:
      vin = va1;
      break;

    case 6:
      vin = va2;
      break;

    case 7:
      vin = va3;
      break;
  }


  // Bits PGA = config[11:9]
  uint8_t pga = (chip->config_register >> 9) & 0x07;

  float full_scale;

  switch (pga) {

    case 0:
      full_scale = 6.144;
      break;

    case 1:
      full_scale = 4.096;
      break;

    case 2:
      full_scale = 2.048;
      break;

    case 3:
      full_scale = 1.024;
      break;

    case 4:
      full_scale = 0.512;
      break;

    default:
      full_scale = 0.256;
      break;
  }


  // Saturación
  if (vin >= full_scale) {
    chip->conversion_register = 32767;
    return;
  }

  if (vin <= -full_scale) {
    chip->conversion_register = -32768;
    return;
  }


  // Conversión a código signed 16 bits
  float scaled = (vin / full_scale) * 32768.0;

  chip->conversion_register = (int16_t)scaled;
}


// ======================================================
// I2C CONNECT
// ======================================================

bool on_i2c_connect(void *user_data,
                    uint32_t address,
                    bool read) {

  chip_state_t *chip = user_data;

  chip->write_index = 0;
  chip->read_index = 0;

  return address == I2C_ADDRESS;
}


// ======================================================
// I2C WRITE
// ======================================================

bool on_i2c_write(void *user_data, uint8_t data) {

  chip_state_t *chip = user_data;

  // Primer byte = pointer register
  if (chip->write_index == 0) {

    chip->pointer_register = data;
    chip->write_index++;

    return true;
  }


  // Registro CONFIG = 0x01
  if (chip->pointer_register == 0x01) {

    // Segundo byte = MSB
    if (chip->write_index == 1) {

      chip->temp_msb = data;
      chip->write_index++;

      return true;
    }

    // Tercer byte = LSB
    if (chip->write_index == 2) {

      chip->config_register =
          ((uint16_t)chip->temp_msb << 8) | data;

      chip->write_index++;

      return true;
    }
  }

  return true;
}


// ======================================================
// I2C READ
// ======================================================

uint8_t on_i2c_read(void *user_data) {

  chip_state_t *chip = user_data;


  // Registro de conversión
  if (chip->pointer_register == 0x00) {

    // Actualizar ADC antes de leer
    if (chip->read_index == 0) {
      simulate_adc_conversion(chip);
    }

    uint16_t value =
        (uint16_t)chip->conversion_register;


    // Primer byte = MSB
    if (chip->read_index == 0) {

      chip->read_index++;

      return (value >> 8) & 0xFF;
    }

    // Segundo byte = LSB
    else {

      chip->read_index++;

      return value & 0xFF;
    }
  }


  // Registro CONFIG
  if (chip->pointer_register == 0x01) {

    if (chip->read_index == 0) {

      chip->read_index++;

      return (chip->config_register >> 8) & 0xFF;
    }

    else {

      chip->read_index++;

      return chip->config_register & 0xFF;
    }
  }

  return 0;
}


// ======================================================
// Inicialización
// ======================================================

void chip_init() {

  chip_state_t *chip =
      malloc(sizeof(chip_state_t));

  memset(chip, 0, sizeof(chip_state_t));


  // Entradas analógicas reales
  chip->a0 = pin_init("A0", ANALOG);
  chip->a1 = pin_init("A1", ANALOG);
  chip->a2 = pin_init("A2", ANALOG);
  chip->a3 = pin_init("A3", ANALOG);


  // Configuración típica ADS1115:
  // AIN0-AIN1
  // PGA ±2.048V
  // single-shot
  // 128 SPS
  chip->config_register = 0x8583;

  chip->pointer_register = 0x00;


  // I2C
  chip->i2c_config.address = I2C_ADDRESS;

  chip->i2c_config.scl =
      pin_init("SCL", INPUT_PULLUP);

  chip->i2c_config.sda =
      pin_init("SDA", INPUT_PULLUP);

  chip->i2c_config.connect =
      on_i2c_connect;

  chip->i2c_config.read =
      on_i2c_read;

  chip->i2c_config.write =
      on_i2c_write;

  chip->i2c_config.user_data =
      chip;


  chip->i2c_dev =
      i2c_init(&(chip->i2c_config));
}