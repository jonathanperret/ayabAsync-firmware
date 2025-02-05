#include "machine.h"

Machine::Machine() { reset(); }

void Machine::reset() { _type = MachineType::NoMachine; }

void Machine::setType(MachineType type) { _type = type; }

MachineType Machine::getType() { return _type; }

bool Machine::isDefined() { return _type != MachineType::NoMachine; }

uint8_t Machine::getNumberofNeedles() {
  switch (_type) {
    case MachineType::Kh270:
      return 112;
    default:
      return 200;
  }
}