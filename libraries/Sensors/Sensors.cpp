#include "Sensors.h"

void minmax_f(Sensor *sensor) {
  // calculate min and max values of a float sensor
  if (sensor->current == 0) {
    return;
  }

  if (sensor->min == 0 || sensor->current < sensor->min) {
    sensor->min = sensor->current;
  }
  
  if (sensor->max == 0 || sensor->current > sensor->max) {
    sensor->max = sensor->current;
  }
}

void minmax_d(EmonSensor *sensor) {
  // calculate min and max values of a double sensor
  if (sensor->AmpereCurrent == 0) {
    return;
  }

  if (sensor->AmpereMin == 0 || sensor->AmpereCurrent < sensor->AmpereMin) {
    sensor->AmpereMin = sensor->AmpereCurrent;
  }
  
  if (sensor->AmpereMax == 0 || sensor->AmpereCurrent > sensor->AmpereMax) {
    sensor->AmpereMax = sensor->AmpereCurrent;
  }

  sensor->WattsCurrent = sensor->AmpereCurrent * 230.0;
  sensor->WattsMin     = sensor->AmpereMin     * 230.0;
  sensor->WattsMax     = sensor->AmpereMax     * 230.0;
}

