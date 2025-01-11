#pragma once

struct data_struct {
  long mgPerDl;
  int propaneLevel;
  double temperature;
};

extern volatile struct data_struct data;
