#include <stdint.h>

#include "FastAccelStepper.h"
#include "RampGenerator.h"
#include "StepperISR.h"

// This define in order to not shoot myself.
#ifndef TEST
#define printf DO_NOT_USE_PRINTF
#define puts DO_NOT_USE_PRINTF
#endif

void RampGenerator::init() {
  _parameters.init();
  _ro.init();
  _rw.init();
  init_ramp_module();
}
int8_t RampGenerator::setAcceleration(int32_t accel) {
  if (accel <= 0) {
    return -1;
  }
  acceleration = (uint32_t)accel;
  _parameters.setAcceleration(accel);
  return 0;
}
void RampGenerator::applySpeedAcceleration() {
	_parameters.applyParameters();
}
int8_t RampGenerator::startRun(bool countUp) {
  uint8_t res = _parameters.checkValidConfig();
  if (res != MOVE_OK) {
    return res;
  }
    _parameters.setTargetPosition(0);
    _ro.force_stop = false;
    //force_immediate_stop = false;
    //incomplete_immediate_stop = false;
    _parameters.keep_running = true;
    _parameters.keep_running_count_up = countUp;
	_parameters.applyParameters();

  fasDisableInterrupts();
  _rw.startRampIfNotRunning();
  //if (_ro.isImmediateStopInitiated()) {
  //  new_ramp.markIncompleteImmediateStop();
  //}
  //_ro = new_ramp;
  fasEnableInterrupts();
#ifdef DEBUG
  char buf[256];
  sprintf(buf, "Ramp data: curr_ticks = %lu travel_ticks = %lu\n",
          _rw.curr_ticks, _config->min_travel_ticks);
  Serial.println(buf);
#endif
  return MOVE_OK;
}

int8_t RampGenerator::_startMove(int32_t target_pos, bool position_changed) {
  uint8_t res = _parameters.checkValidConfig();
  if (res != MOVE_OK) {
    return res;
  }

    _parameters.setTargetPosition(target_pos);
    _ro.force_stop = false;
    //force_immediate_stop = false;
    //incomplete_immediate_stop = false;
    _parameters.keep_running = false;
    _parameters.keep_running_count_up = true;
	_parameters.applyParameters();

  //fasDisableInterrupts();
  if (position_changed) {
    // Only start the ramp generator, if the target position is different
    _rw.startRampIfNotRunning();
  }
  //if (_ro.isImmediateStopInitiated()) {
  //  new_ramp.markIncompleteImmediateStop();
  //}
  //_ro = new_ramp;
  //fasEnableInterrupts();

#ifdef TEST
  printf("Ramp data: go to %d  curr_ticks = %u travel_ticks = %u\n", target_pos,
         _rw.curr_ticks, _ro.config.parameters.min_travel_ticks);
#endif
#ifdef DEBUG
  char buf[256];
  sprintf(buf, "Ramp data: go to = %ld  curr_ticks = %lu travel_ticks = %lu\n",
          target_pos, _rw.curr_ticks, _ro.config.parameters.min_travel_ticks);
  Serial.println(buf);
#endif
  return MOVE_OK;
}

int8_t RampGenerator::moveTo(int32_t position,
                             const struct queue_end_s *queue_end) {
  int32_t curr_pos;
  if (isRampGeneratorActive() && !_ro.config.parameters.keep_running) {
    curr_pos = _ro.config.parameters.target_pos;
  } else {
    curr_pos = queue_end->pos;
  }
  inject_fill_interrupt(1);
  int res = _startMove(position, curr_pos != position);
  inject_fill_interrupt(2);
  return res;
}
int8_t RampGenerator::move(int32_t move, const struct queue_end_s *queue_end) {
  int32_t curr_pos;
  if (isRampGeneratorActive() && !_ro.config.parameters.keep_running) {
    curr_pos = _ro.config.parameters.target_pos;
  } else {
    curr_pos = queue_end->pos;
  }
  int32_t new_pos = curr_pos + move;
  return _startMove(new_pos, curr_pos != new_pos);
}
void RampGenerator::advanceTargetPosition(int32_t delta,
                                          const struct queue_end_s *queue) {
  if (isRampGeneratorActive() && !_ro.config.parameters.keep_running) {
    int32_t new_pos = _ro.config.parameters.target_pos + delta;
    _startMove(new_pos, true);
  }
}

void RampGenerator::afterCommandEnqueued(NextCommand *command) {
#ifdef TEST
  printf(
      "after Command Enqueued: performed ramp up steps = %u, pause left = %u, "
      "curr_ticks = %u\n",
      command->rw.performed_ramp_up_steps, command->rw.pause_ticks_left,
      command->rw.curr_ticks);
#endif
  _rw = command->rw;
}
void RampGenerator::getNextCommand(const struct queue_end_s *queue_end,
                                   NextCommand *command) {
  // we are running in higher priority than the application
  // so we can just read the config without disable interrupts
  // copy consistent ramp state
  if (_parameters.apply) {
	  _ro.config.parameters = _parameters;
	  _parameters.apply = false;
	  _parameters.any_change = false;
	  _parameters.recalc_ramp_steps = false;
	  _ro.config.update();
  }

  fasDisableInterrupts();
  struct queue_end_s qe = *queue_end;
  fasEnableInterrupts();

  _ro.clearImmediateStop();

  if (_ro.isImmediateStopInitiated()) {
    // no more commands
    command->command.ticks = 0;
    _ro.clearImmediateStop();
    command->rw.stopRamp();
    return;
  }
  if (_ro.isImmediateStopIncomplete()) {
    _rw.stopRamp();
    _ro.clearImmediateStop();
  }
  _getNextCommand(&_ro, &_rw, &qe, command);
}
void RampGenerator::stopRamp() { _rw.stopRamp(); }
int32_t RampGenerator::getCurrentAcceleration() {
  switch (_rw.rampState() &
          (RAMP_STATE_ACCELERATING_FLAG | RAMP_STATE_DECELERATING_FLAG |
           RAMP_DIRECTION_MASK)) {
    case RAMP_STATE_ACCELERATING_FLAG | RAMP_DIRECTION_COUNT_UP:
    case RAMP_STATE_DECELERATING_FLAG | RAMP_DIRECTION_COUNT_DOWN:
      return acceleration;
    case RAMP_STATE_DECELERATING_FLAG | RAMP_DIRECTION_COUNT_UP:
    case RAMP_STATE_ACCELERATING_FLAG | RAMP_DIRECTION_COUNT_DOWN:
      return -acceleration;
  }
  return 0;
}
