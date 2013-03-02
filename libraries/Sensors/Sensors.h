// Storage class for sensor values, names and stats
struct Sensor {
  float current;
  float min;
  float max;
};

struct EmonSensor {
  double AmpereCurrent;
  double AmpereMin;
  double AmpereMax;
  double WattsCurrent;
  double WattsMin;
  double WattsMax;
};

void minmax_f(Sensor *sensor);

void minmax_d(EmonSensor *sensor);
