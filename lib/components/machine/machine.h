#ifndef MACHINE_H
#define MACHINE_H

#include <stdint.h>

#include "api.h"

class Machine {
 public:
  Machine();
  ~Machine() = default;

  // Reset machine state
  void reset();
  // Set machine type
  void setType(MachineType type);
  // get machine type
  MachineType getType() ;
  // CHeck if machine is defined
  bool isDefined();
  // Return number of needles for the machine
  uint8_t getNumberofNeedles();

 private:
  MachineType _type;
};

#endif