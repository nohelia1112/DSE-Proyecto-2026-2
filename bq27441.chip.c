#include "wokwi-api.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#define BQ27441_ADDRESS 0x55

// ======================================================
// CONSUMOS DEL SISTEMA
// ======================================================

#define I_TEST_MA        200.0f
#define I_INA296_MA        2.5f
#define I_ADS1115_MA       0.150f
#define I_BQ27441_MA       0.093f
#define I_TPS63031_MA      0.025f

#define VOUT_SYSTEM        3.3f


// ======================================================
// ESTADO DEL CHIP
// ======================================================

typedef struct {

  i2c_dev_t i2c_dev;
  timer_t battery_timer;

  uint8_t pointer_register;
  uint8_t write_index;

  pin_t vbat;

  // Atributos
  uint32_t attr_initial_soc;
  uint32_t attr_full_capacity;
  uint32_t attr_esp_current;
  uint32_t attr_efficiency;
  uint32_t attr_internal_resistance;

  uint32_t attr_charger_connected;
  uint32_t attr_charge_current;

  uint32_t attr_sim_speed;
  uint32_t attr_temperature;

  // Estado dinámico
  float remaining_mah;

} chip_state_t;


// ======================================================
// FUNCIONES AUXILIARES
// ======================================================

static float clampf_local(
  float value,
  float min_value,
  float max_value
) {

  if (value < min_value) {
    return min_value;
  }

  if (value > max_value) {
    return max_value;
  }

  return value;
}


// ======================================================
// CAPACIDAD DE LA BATERÍA
// ======================================================

static float get_full_capacity(
  chip_state_t *chip
) {

  float capacity =
    attr_read_float(
      chip->attr_full_capacity
    );

  return clampf_local(
    capacity,
    200.0f,
    8000.0f
  );
}


static float get_soc_float(
  chip_state_t *chip
) {

  float full =
    get_full_capacity(chip);

  if (full <= 0) {
    return 0;
  }

  float soc =
    (chip->remaining_mah / full)
    * 100.0f;

  return clampf_local(
    soc,
    0.0f,
    100.0f
  );
}


static uint16_t get_soc(
  chip_state_t *chip
) {

  return (uint16_t)(
    get_soc_float(chip)
    + 0.5f
  );
}


static uint16_t get_remaining_capacity(
  chip_state_t *chip
) {

  return (uint16_t)(
    chip->remaining_mah
    + 0.5f
  );
}


// ======================================================
// MODELO DE VOLTAJE LiPo
// ======================================================

static float interpolate(
  float x,
  float x1,
  float x2,
  float y1,
  float y2
) {

  return y1 +
    (x - x1)
    * (y2 - y1)
    / (x2 - x1);
}


static float get_ocv(
  chip_state_t *chip
) {

  float soc =
    get_soc_float(chip);

  if (soc <= 10) {

    return interpolate(
      soc,
      0,
      10,
      3.20f,
      3.50f
    );

  } else if (soc <= 20) {

    return interpolate(
      soc,
      10,
      20,
      3.50f,
      3.65f
    );

  } else if (soc <= 50) {

    return interpolate(
      soc,
      20,
      50,
      3.65f,
      3.80f
    );

  } else if (soc <= 80) {

    return interpolate(
      soc,
      50,
      80,
      3.80f,
      4.00f
    );

  } else {

    return interpolate(
      soc,
      80,
      100,
      4.00f,
      4.20f
    );
  }
}


// ======================================================
// CONSUMO DEL SISTEMA
// ======================================================

static int16_t get_battery_current(
  chip_state_t *chip
) {

  float soc =
    get_soc_float(chip);


  bool charging =
    attr_read_float(
      chip->attr_charger_connected
    ) > 0.5f;


  // ----------------------------------------------------
  // CARGANDO
  // ----------------------------------------------------

  if (charging) {

    if (soc >= 100.0f) {
      return 0;
    }

    float charge_current =
      attr_read_float(
        chip->attr_charge_current
      );

    charge_current =
      clampf_local(
        charge_current,
        0.0f,
        1500.0f
      );

    // Positivo = cargando
    float esp_current =
        attr_read_float(
            chip->attr_esp_current
        );


    return (int16_t)(
        charge_current
        - esp_current
    );
  }


  // ----------------------------------------------------
  // DESCARGANDO
  // ----------------------------------------------------

  if (soc <= 0) {
    return 0;
  }


  float esp_current =
    attr_read_float(
      chip->attr_esp_current
    );


  // Corriente total del riel de 3.3 V
  float rail_current_ma =
      I_TEST_MA
    + I_INA296_MA
    + I_ADS1115_MA
    + esp_current;


  // Potencia consumida por el sistema
  float output_power_w =
      VOUT_SYSTEM
    * rail_current_ma
    / 1000.0f;


  // Eficiencia del TPS63031
  float efficiency =
    attr_read_float(
      chip->attr_efficiency
    ) / 100.0f;

  efficiency =
    clampf_local(
      efficiency,
      0.50f,
      0.99f
    );


  // Voltaje aproximado de la batería
  float battery_voltage =
    get_ocv(chip);


  // Corriente que debe entregar la batería
  float battery_current_ma =
    (
      output_power_w
      /
      (
        battery_voltage
        * efficiency
      )
    )
    * 1000.0f;


  // Consumos adicionales
  battery_current_ma +=
      I_BQ27441_MA
    + I_TPS63031_MA;


  // Negativo = descargando
  return (int16_t)(
    -(battery_current_ma + 0.5f)
  );
}


// ======================================================
// VOLTAJE DE BATERÍA
// ======================================================

static uint16_t get_voltage(
  chip_state_t *chip
) {

  float ocv =
    get_ocv(chip);


  int16_t current_ma =
    get_battery_current(chip);


  float resistance_mohm =
    attr_read_float(
      chip->attr_internal_resistance
    );


  // V = Voc + I*R
  float voltage =
      ocv
    + (
        current_ma
        * resistance_mohm
      )
      / 1000000.0f;


  voltage =
    clampf_local(
      voltage,
      3.0f,
      4.2f
    );


  return (uint16_t)(
    voltage * 1000.0f
  );
}


// ======================================================
// TEMPERATURA
// ======================================================

static uint16_t get_temperature(
  chip_state_t *chip
) {

  float temp_c =
    attr_read_float(
      chip->attr_temperature
    );

  float temp_kelvin =
    temp_c + 273.15f;

  return (uint16_t)(
    temp_kelvin * 10.0f
  );
}


// ======================================================
// FLAGS
// ======================================================

static uint16_t get_flags(
  chip_state_t *chip
) {

  uint16_t flags = 0;

  int16_t current =
    get_battery_current(chip);

  uint16_t soc =
    get_soc(chip);


  // BAT_DET
  flags |= (1 << 3);


  // DSG = descargando
  if (current < 0) {
    flags |= (1 << 0);
  }


  // FC = batería llena
  if (soc >= 100) {
    flags |= (1 << 9);
  }


  return flags;
}


// ======================================================
// ACTUALIZACIÓN DINÁMICA DE LA BATERÍA
// ======================================================

static void battery_tick(
  void *user_data
) {

  chip_state_t *chip =
    user_data;

  float voltage =
      get_voltage(chip);


  pin_dac_write(
      chip->vbat,
      voltage
  );

  int16_t current_ma =
    get_battery_current(chip);


  float speed =
    attr_read_float(
      chip->attr_sim_speed
    );


  if (speed < 1) {
    speed = 1;
  }


  // 1 segundo = 1/3600 horas
  //
  // mAh += mA * horas

  chip->remaining_mah +=
      current_ma
    * (speed / 3600.0f);


  float full =
    get_full_capacity(chip);


  if (chip->remaining_mah < 0) {
    chip->remaining_mah = 0;
  }


  if (chip->remaining_mah > full) {
    chip->remaining_mah = full;
  }


  // DEBUG EN CHIPS CONSOLE
  printf(
    "TICK | Cap=%.2f mAh | SOC=%.2f %% | speed=%.1f\n",
    chip->remaining_mah,
    get_soc_float(chip),
    speed
  );
}


// ======================================================
// REGISTROS DEL BQ27441
// ======================================================

static uint16_t get_command_word(
  chip_state_t *chip,
  uint8_t command
) {

  switch (command) {

    // Temperature()
    case 0x02:
      return get_temperature(chip);


    // Voltage()
    case 0x04:
      return get_voltage(chip);


    // Flags()
    case 0x06:
      return get_flags(chip);


    // NominalAvailableCapacity()
    case 0x08:
      return get_remaining_capacity(chip);


    // FullAvailableCapacity()
    case 0x0A:
      return (uint16_t)
        get_full_capacity(chip);


    // RemainingCapacity()
    case 0x0C:
      return get_remaining_capacity(chip);


    // FullChargeCapacity()
    case 0x0E:
      return (uint16_t)
        get_full_capacity(chip);


    // AverageCurrent()
    case 0x10:
      return (uint16_t)
        get_battery_current(chip);


    // AveragePower()
    case 0x18: {

      int32_t voltage_mv =
        get_voltage(chip);

      int32_t current_ma =
        get_battery_current(chip);

      int32_t power_mw =
        (
          voltage_mv
          * current_ma
        ) / 1000;

      return (uint16_t)(
        (int16_t)power_mw
      );
    }


    // StateOfCharge()
    case 0x1C:
      return get_soc(chip);


    // InternalTemperature()
    case 0x1E:
      return get_temperature(chip);


    default:
      return 0;
  }
}


// ======================================================
// LECTURA DE REGISTROS
// ======================================================

static uint8_t read_register_byte(
  chip_state_t *chip,
  uint8_t reg
) {

  uint8_t base =
    reg & 0xFE;


  uint16_t value =
    get_command_word(
      chip,
      base
    );


  // BQ27441:
  // primero byte bajo (LSB)
  if ((reg & 0x01) == 0) {

    return value & 0xFF;
  }


  // después byte alto (MSB)
  return (value >> 8)
    & 0xFF;
}


// ======================================================
// CALLBACKS I2C
// ======================================================

bool on_i2c_connect(
  void *user_data,
  uint32_t address,
  bool read
) {

  chip_state_t *chip =
    user_data;


  if (!read) {
    chip->write_index = 0;
  }


  return address ==
    BQ27441_ADDRESS;
}


bool on_i2c_write(
  void *user_data,
  uint8_t data
) {

  chip_state_t *chip =
    user_data;


  // Primer byte:
  // registro que quiere consultar el ESP32
  if (chip->write_index == 0) {

    chip->pointer_register =
      data;

    chip->write_index++;
  }


  return true;
}


uint8_t on_i2c_read(
  void *user_data
) {

  chip_state_t *chip =
    user_data;


  uint8_t data =
    read_register_byte(
      chip,
      chip->pointer_register
    );


  // Lectura incremental
  chip->pointer_register++;


  return data;
}


void on_i2c_disconnect(
  void *user_data
) {
}


// ======================================================
// INICIALIZACIÓN
// ======================================================

void chip_init() {

  chip_state_t *chip =
    calloc(
      1,
      sizeof(chip_state_t)
    );


  // ----------------------------------------------------
  // Parámetros iniciales
  // ----------------------------------------------------

  chip->vbat =
      pin_init(
          "VBAT",
          ANALOG
      );

  chip->attr_initial_soc =
    attr_init_float(
      "initialSOC",
      67.0f
    );


  chip->attr_full_capacity =
    attr_init_float(
      "fullCapacityMah",
      1000.0f
    );


  chip->attr_esp_current =
    attr_init_float(
      "espCurrentMa",
      84.0f
    );


  chip->attr_efficiency =
    attr_init_float(
      "converterEfficiency",
      90.0f
    );


  chip->attr_internal_resistance =
    attr_init_float(
      "internalResistanceMohm",
      100.0f
    );


  chip->attr_charger_connected =
    attr_init_float(
      "chargerConnected",
      0.0f // 0 (no cargando) / 1 (cargando)
    );


  chip->attr_charge_current =
    attr_init_float(
      "chargeCurrentMa",
      500.0f
    );


  chip->attr_sim_speed =
    attr_init_float(
      "simulationSpeed",
      60.0f
    );


  chip->attr_temperature =
    attr_init_float(
      "temperatureC",
      25.0f
    );


  // ----------------------------------------------------
  // Capacidad inicial
  // ----------------------------------------------------

  float initial_soc =
    attr_read_float(
      chip->attr_initial_soc
    );


  float full =
    get_full_capacity(chip);


  chip->remaining_mah =
      full
    * initial_soc
    / 100.0f;


  // ----------------------------------------------------
  // I2C
  // ----------------------------------------------------

  const i2c_config_t i2c_config = {

    .address =
      BQ27441_ADDRESS,

    .scl =
      pin_init(
        "SCL",
        INPUT_PULLUP
      ),

    .sda =
      pin_init(
        "SDA",
        INPUT_PULLUP
      ),

    .connect =
      on_i2c_connect,

    .read =
      on_i2c_read,

    .write =
      on_i2c_write,

    .disconnect =
      on_i2c_disconnect,

    .user_data =
      chip
  };


  chip->i2c_dev =
    i2c_init(
      (i2c_config_t *)
      &i2c_config
    );


  // ----------------------------------------------------
  // TIMER DE LA BATERÍA
  // ----------------------------------------------------

  const timer_config_t timer_config = {

    .callback =
      battery_tick,

    .user_data =
      chip
  };


  chip->battery_timer =
    timer_init(
      (timer_config_t *)
      &timer_config
    );


  // Actualizar batería cada segundo
  timer_start(
    chip->battery_timer,
    1000000,
    true
  );
}