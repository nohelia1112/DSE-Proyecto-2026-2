// Wokwi Custom Chip - For docs and examples see:
// https://docs.wokwi.com/chips-api/getting-started
//
// SPDX-License-Identifier: MIT
// Copyright 2023 

#include "wokwi-api.h"
#include <stdlib.h>

typedef struct {
  pin_t vamp;
  pin_t vref;
  pin_t itest;
} chip_state_t;

void chip_init() {
  chip_state_t *chip = malloc(sizeof(chip_state_t));

  chip->vamp = pin_init("VAMP", ANALOG);
  chip->vref = pin_init("VREF", ANALOG);
  chip->itest = pin_init("ITEST", ANALOG);

  // Valores obtenidos de LTspice.
  // Este Vamp e Itest es el de Rx = 1 mili Ohm, que fue nuestra prueba.
  pin_dac_write(chip->vamp, 0.0021453);
  pin_dac_write(chip->vref, 0);
  pin_dac_write(chip->itest, 2*(0.199876));
}