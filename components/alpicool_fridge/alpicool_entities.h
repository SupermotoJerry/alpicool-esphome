#pragma once

#ifdef USE_ESP32

#include "esphome/components/climate/climate.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"

#include "alpicool_fridge.h"

namespace esphome::alpicool_fridge {

// One thermostat per zone. On/off and the Max/Eco preset apply to the whole
// fridge, so changing them on one zone changes them on the other as well.
class AlpicoolClimate : public climate::Climate, public Component {
 public:
  AlpicoolClimate(AlpicoolFridge *parent, Zone zone) : parent_(parent), zone_(zone) {}
  void dump_config() override;
  void update_state(const FridgeStatus *status);

 protected:
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

  AlpicoolFridge *parent_;
  Zone zone_;
};

class AlpicoolSwitch : public switch_::Switch, public Component {
 public:
  AlpicoolSwitch(AlpicoolFridge *parent, SwitchType type) : parent_(parent), type_(type) {}
  void dump_config() override;

 protected:
  void write_state(bool state) override;

  AlpicoolFridge *parent_;
  SwitchType type_;
};

// Options map to the fridge's raw value by index (run mode 0/1, battery saver 0/1/2).
class AlpicoolSelect : public select::Select, public Component {
 public:
  AlpicoolSelect(AlpicoolFridge *parent, SelectType type) : parent_(parent), type_(type) {}
  void dump_config() override;

 protected:
  void control(size_t index) override;

  AlpicoolFridge *parent_;
  SelectType type_;
};

class AlpicoolNumber : public number::Number, public Component {
 public:
  AlpicoolNumber(AlpicoolFridge *parent, NumberType type) : parent_(parent), type_(type) {}
  void dump_config() override;

 protected:
  void control(float value) override;

  AlpicoolFridge *parent_;
  NumberType type_;
};

}  // namespace esphome::alpicool_fridge

#endif  // USE_ESP32
