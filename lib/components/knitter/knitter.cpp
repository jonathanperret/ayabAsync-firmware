#include "knitter.h"

#include "api.h"
#include "shield.h"
#include "mcp23008.h"
#include "pcf8574.h"

//----------------------------------------------------------------------------
// Knitter class
//----------------------------------------------------------------------------

Knitter::Knitter(hardwareAbstraction::HalInterface *hal) : API(hal) {
  // Platform
  _hal = hal;

  // Knitter objects
  _machine = new Machine();
  _carriage = new Carriage();
  _direction = Direction::Unknown;

  // Ayab hardware
  _beeper = new Beeper(_hal, Shield::Piezo::PIEZO_PIN);

  _led_a = new Led(_hal, Shield::Leds::LED_A_PIN, HIGH, LOW);
  _led_b = new Led(_hal, Shield::Leds::LED_B_PIN, HIGH, LOW);

  _resetFromOperate = false;

  _kh970Client.setEventHandler(this);

  reset();
}

void Knitter::reset() {
  _state = KnitterState::Reset;
  _kh970Client.begin();
  _lastRequestedRow = 0;
}

void Knitter::schedule() {
  API::schedule();
  _beeper->schedule();
  _led_a->schedule();
  _led_b->schedule();

  _runMachine();

  // Finite State Machine of this knitting machine
  bool isStateChange = _state != _lastState;
  _lastState = _state;
  switch (_state) {
    case KnitterState::Reset:
      // Skip machine reset when initiated from reqInit
      if (_resetFromOperate) {
        _resetFromOperate = false;
      } else {
        _machine->reset();
      }
      _carriage->reset();
      _carriage->setPosition(0);

      _led_a->on();
      _led_b->on();

      _config.valid = false;
      _config.continuousReporting = false;
      _state = KnitterState::Init;

      break;

    case KnitterState::Init:
      if (isStateChange) {
        _led_a->blink(LED_SLOW_ON, LED_SLOW_OFF);
        _beeper->beep(BEEPER_INIT);
      }
      _carriage->setType(CarriageType::Knit);
      if (_machine->isDefined() && _carriage->isDefined()) {
        _state = KnitterState::Ready;
        _apiRxIndicateState();
        _config.valid = false;
      }
      break;

    case KnitterState::Ready:
      if (isStateChange) {
        _led_a->blink(LED_FAST_ON, LED_FAST_OFF);
      }
      if (_config.valid) {
        _state = KnitterState::Operate;
        _currentLine.reset();
      }
      break;

    case KnitterState::Operate:
      if (isStateChange) {
        _led_a->off();  // turn off, used for API Rx indication
        _led_b->off();  // turn off, used for API Tx indication
      }
      if (_currentLine.finished) {
        if (_currentLine.isLastLine()) {
          _state = KnitterState::Reset;
        } else {
          if (!_currentLine.requested) {
            // TODO: Implement a timeout/retry mechanism ?
            _apiRequestLine(_currentLine.getNextLineNumber(), ErrorCode::Success);
            _currentLine.requested = true;
          }
        }
      }
      break;

    default:
      _state = KnitterState::Reset;
      break;
  }
}

void Knitter::_apiRxTrafficIndication() { _led_a->flash(LED_FLASH_DURATION); }

void Knitter::_apiTxTrafficIndication() { _led_b->flash(LED_FLASH_DURATION); }

void Knitter::_apiRequestReset() { reset(); }

ErrorCode Knitter::_apiRequestInit(MachineType machine) {
  if (_state == KnitterState::Init || _state == KnitterState::Operate) {
    _machine->setType(machine);
    // Reset machine upon reception of a new reqInit while in Operate state
    // because there is no reqReset API call from ayab-desktop as of today
    // and there is no hardware reset when the serial is open on all platforms
    // e.g. UNO R4
    if (_state == KnitterState::Operate) {
      _resetFromOperate = true;
      reset();
    }
    return ErrorCode::Success;
  }
  return ErrorCode::MachineInvalidState;
}

ErrorCode Knitter::_apiRxSetConfig(uint8_t startNeedle, uint8_t stopNeedle,
                                   bool continuousReporting,
                                   bool beeperEnabled) {
  _config.valid = false;
  if (_state == KnitterState::Ready) {
    if ((startNeedle >= 0) && (stopNeedle < _machine->getNumberofNeedles()) &&
        (startNeedle < stopNeedle)) {
      _config = {.startNeedle = startNeedle,
                 .stopNeedle = stopNeedle,
                 .continuousReporting = continuousReporting};
      _beeper->config(beeperEnabled);
      _config.valid = true;
      return ErrorCode::Success;
    }
    return ErrorCode::MessageInvalidArguments;
  }
  return ErrorCode::MachineInvalidState;
}

ErrorCode Knitter::_apiRxSetLine(uint8_t lineNumber, const uint8_t *pattern,
                                 uint8_t size, bool isLastLine) {
  if (_state == KnitterState::Operate) {
    if (size != 25) {
      return ErrorCode::MessageIncorrectLenght;
    }

    uint8_t reversedPattern[size]; // KH970 byte order does not match AYAB's
    for (int i=0; i<size; i++) {
      reversedPattern[i] = pattern[size-i-1];
    }
    _kh970Client.setPattern(reversedPattern);

    _beeper->beep(BEEPER_NEXT_LINE);
    return ErrorCode::Success;
  }
  return ErrorCode::MachineInvalidState;
}

void Knitter::_apiRxIndicateState() {
  MachineSide hallActive = MachineSide::None;
  CarriageType carriage = CarriageType::Knit;
  _apiIndicateState(_state, 0, 0, carriage,
                    50, _direction, hallActive,
                    BeltShift::Regular);
}

void Knitter::debugLog(const char *msg)
{
  _apiDebugLog(msg);
}

void Knitter::rowCounterHit()
{
  _apiRowCounterHit();
}

void Knitter::_runMachine() {
  if (_machine->isDefined()) {
    _carriage->setType(CarriageType::Knit);
    _kh970Client.update();
    if (_kh970Client.pattern_row != _lastRequestedRow) {
      _lastRequestedRow = _kh970Client.pattern_row;
      _apiRequestLine(_lastRequestedRow, ErrorCode::Success);
    }

    // Update host SW
    if (_config.continuousReporting) {
      _apiRxIndicateState();
    }
  }
}
