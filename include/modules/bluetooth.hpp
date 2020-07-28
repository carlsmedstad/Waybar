#pragma once

#include <fmt/chrono.h>
#include <fmt/format.h>

#include "ALabel.hpp"
#include "util/rfkill.hpp"
#include "util/sleeper_thread.hpp"

namespace waybar::modules {

class Bluetooth : public ALabel {
 public:
  Bluetooth(const std::string&, const Json::Value&);
  ~Bluetooth() = default;
  auto update() -> void;

 private:
  std::string         status_;
  util::SleeperThread thread_;
  util::SleeperThread intervall_thread_;

  util::Rfkill rfkill_;
};

}  // namespace waybar::modules
