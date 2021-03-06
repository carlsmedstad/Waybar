#ifdef HAVE_GTK_LAYER_SHELL
#include <gtk-layer-shell.h>
#endif

#include "bar.hpp"
#include "client.hpp"
#include "factory.hpp"
#include <spdlog/spdlog.h>

waybar::Bar::Bar(struct waybar_output* w_output, const Json::Value& w_config)
    : output(w_output),
      config(w_config),
      surface(nullptr),
      window{Gtk::WindowType::WINDOW_TOPLEVEL},
      layer_surface_(nullptr),
      anchor_(ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP),
      left_(Gtk::ORIENTATION_HORIZONTAL, 0),
      center_(Gtk::ORIENTATION_HORIZONTAL, 0),
      right_(Gtk::ORIENTATION_HORIZONTAL, 0),
      box_(Gtk::ORIENTATION_HORIZONTAL, 0) {
  window.set_title("waybar");
  window.set_name("waybar");
  window.set_decorated(false);
  window.get_style_context()->add_class(output->name);
  window.get_style_context()->add_class(config["name"].asString());
  window.get_style_context()->add_class(config["position"].asString());

  if (config["position"] == "right" || config["position"] == "left") {
    height_ = 0;
    width_ = 1;
  }
  height_ = config["height"].isUInt() ? config["height"].asUInt() : height_;
  width_ = config["width"].isUInt() ? config["width"].asUInt() : width_;

  if (config["position"] == "bottom") {
    anchor_ = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
  } else if (config["position"] == "left") {
    anchor_ = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
  } else if (config["position"] == "right") {
    anchor_ = ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
  }
  if (anchor_ == ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM ||
      anchor_ == ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) {
    anchor_ |= ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
  } else if (anchor_ == ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT ||
             anchor_ == ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT) {
    anchor_ |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
    left_ = Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0);
    center_ = Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0);
    right_ = Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0);
    box_ = Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0);
    vertical = true;
  }

  if (config["margin-top"].isInt() || config["margin-right"].isInt() ||
      config["margin-bottom"].isInt() || config["margin-left"].isInt()) {
    margins_ = {
        config["margin-top"].isInt() ? config["margin-top"].asInt() : 0,
        config["margin-right"].isInt() ? config["margin-right"].asInt() : 0,
        config["margin-bottom"].isInt() ? config["margin-bottom"].asInt() : 0,
        config["margin-left"].isInt() ? config["margin-left"].asInt() : 0,
    };
  } else if (config["margin"].isString()) {
    std::istringstream       iss(config["margin"].asString());
    std::vector<std::string> margins{std::istream_iterator<std::string>(iss), {}};
    try {
      if (margins.size() == 1) {
        auto gaps = std::stoi(margins[0], nullptr, 10);
        margins_ = {.top = gaps, .right = gaps, .bottom = gaps, .left = gaps};
      }
      if (margins.size() == 2) {
        auto vertical_margins = std::stoi(margins[0], nullptr, 10);
        auto horizontal_margins = std::stoi(margins[1], nullptr, 10);
        margins_ = {.top = vertical_margins,
                    .right = horizontal_margins,
                    .bottom = vertical_margins,
                    .left = horizontal_margins};
      }
      if (margins.size() == 3) {
        auto horizontal_margins = std::stoi(margins[1], nullptr, 10);
        margins_ = {.top = std::stoi(margins[0], nullptr, 10),
                    .right = horizontal_margins,
                    .bottom = std::stoi(margins[2], nullptr, 10),
                    .left = horizontal_margins};
      }
      if (margins.size() == 4) {
        margins_ = {.top = std::stoi(margins[0], nullptr, 10),
                    .right = std::stoi(margins[1], nullptr, 10),
                    .bottom = std::stoi(margins[2], nullptr, 10),
                    .left = std::stoi(margins[3], nullptr, 10)};
      }
    } catch (...) {
      spdlog::warn("Invalid margins: {}", config["margin"].asString());
    }
  } else if (config["margin"].isInt()) {
    auto gaps = config["margin"].asInt();
    margins_ = {.top = gaps, .right = gaps, .bottom = gaps, .left = gaps};
  }

#ifdef HAVE_GTK_LAYER_SHELL
  use_gls_ = config["gtk-layer-shell"].isBool() ? config["gtk-layer-shell"].asBool() : true;
  if (use_gls_) {
    initGtkLayerShell();
  }
#endif

  window.signal_realize().connect_notify(sigc::mem_fun(*this, &Bar::onRealize));
  window.signal_map_event().connect_notify(sigc::mem_fun(*this, &Bar::onMap));
  window.signal_configure_event().connect_notify(sigc::mem_fun(*this, &Bar::onConfigure));
  window.set_size_request(width_, height_);
  setupWidgets();

  if (window.get_realized()) {
    onRealize();
  }
  window.show_all();
}

void waybar::Bar::onConfigure(GdkEventConfigure* ev) {
  auto tmp_height = height_;
  auto tmp_width = width_;
  if (ev->height > static_cast<int>(height_)) {
    // Default minimal value
    if (height_ > 1) {
      spdlog::warn(MIN_HEIGHT_MSG, height_, ev->height);
    }
    if (config["height"].isUInt()) {
      spdlog::info(SIZE_DEFINED, "Height");
    } else {
      tmp_height = ev->height;
    }
  }
  if (ev->width > static_cast<int>(width_)) {
    // Default minimal value
    if (width_ > 1) {
      spdlog::warn(MIN_WIDTH_MSG, width_, ev->width);
    }
    if (config["width"].isUInt()) {
      spdlog::info(SIZE_DEFINED, "Width");
    } else {
      tmp_width = ev->width;
    }
  }
  if (use_gls_) {
    width_ = tmp_width;
    height_ = tmp_height;
    spdlog::debug("Set surface size {}x{} for output {}", width_, height_, output->name);
    setExclusiveZone(tmp_width, tmp_height);
  } else if (tmp_width != width_ || tmp_height != height_) {
    setSurfaceSize(tmp_width, tmp_height);
  }
}

#ifdef HAVE_GTK_LAYER_SHELL
void waybar::Bar::initGtkLayerShell() {
  auto gtk_window = window.gobj();
  // this has to be executed before GtkWindow.realize
  gtk_layer_init_for_window(gtk_window);
  gtk_layer_set_keyboard_interactivity(gtk_window, FALSE);
  auto layer = config["layer"] == "top" ? GTK_LAYER_SHELL_LAYER_TOP : GTK_LAYER_SHELL_LAYER_BOTTOM;
  gtk_layer_set_layer(gtk_window, layer);
  gtk_layer_set_monitor(gtk_window, output->monitor->gobj());
  gtk_layer_set_namespace(gtk_window, "waybar");

  gtk_layer_set_anchor(
      gtk_window, GTK_LAYER_SHELL_EDGE_LEFT, anchor_ & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
  gtk_layer_set_anchor(
      gtk_window, GTK_LAYER_SHELL_EDGE_RIGHT, anchor_ & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
  gtk_layer_set_anchor(
      gtk_window, GTK_LAYER_SHELL_EDGE_TOP, anchor_ & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP);
  gtk_layer_set_anchor(
      gtk_window, GTK_LAYER_SHELL_EDGE_BOTTOM, anchor_ & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM);

  gtk_layer_set_margin(gtk_window, GTK_LAYER_SHELL_EDGE_LEFT, margins_.left);
  gtk_layer_set_margin(gtk_window, GTK_LAYER_SHELL_EDGE_RIGHT, margins_.right);
  gtk_layer_set_margin(gtk_window, GTK_LAYER_SHELL_EDGE_TOP, margins_.top);
  gtk_layer_set_margin(gtk_window, GTK_LAYER_SHELL_EDGE_BOTTOM, margins_.bottom);

  if (width_ > 1 && height_ > 1) {
    /* configure events are not emitted if the bar is using initial size */
    setExclusiveZone(width_, height_);
  }
}
#endif

void waybar::Bar::onRealize() {
  auto gdk_window = window.get_window()->gobj();
  gdk_wayland_window_set_use_custom_surface(gdk_window);
}

void waybar::Bar::onMap(GdkEventAny* ev) {
  auto gdk_window = window.get_window()->gobj();
  surface = gdk_wayland_window_get_wl_surface(gdk_window);

  if (use_gls_) {
    return;
  }

  auto client = waybar::Client::inst();
  // owned by output->monitor; no need to destroy
  auto wl_output = gdk_wayland_monitor_get_wl_output(output->monitor->gobj());
  auto layer =
      config["layer"] == "top" ? ZWLR_LAYER_SHELL_V1_LAYER_TOP : ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;
  layer_surface_ = zwlr_layer_shell_v1_get_layer_surface(
      client->layer_shell, surface, wl_output, layer, "waybar");

  zwlr_layer_surface_v1_set_keyboard_interactivity(layer_surface_, false);
  zwlr_layer_surface_v1_set_anchor(layer_surface_, anchor_);
  zwlr_layer_surface_v1_set_margin(
      layer_surface_, margins_.top, margins_.right, margins_.bottom, margins_.left);
  setSurfaceSize(width_, height_);
  setExclusiveZone(width_, height_);

  static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
      .configure = layerSurfaceHandleConfigure,
      .closed = layerSurfaceHandleClosed,
  };
  zwlr_layer_surface_v1_add_listener(layer_surface_, &layer_surface_listener, this);

  wl_surface_commit(surface);
  wl_display_roundtrip(client->wl_display);
}

void waybar::Bar::setExclusiveZone(uint32_t width, uint32_t height) {
  auto zone = 0;
  if (visible) {
    // exclusive zone already includes margin for anchored edge,
    // only opposite margin should be added
    if (vertical) {
      zone += width;
      zone += (anchor_ & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT) ? margins_.right : margins_.left;
    } else {
      zone += height;
      zone += (anchor_ & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) ? margins_.bottom : margins_.top;
    }
  }
  spdlog::debug("Set exclusive zone {} for output {}", zone, output->name);

#ifdef HAVE_GTK_LAYER_SHELL
  if (use_gls_) {
    gtk_layer_set_exclusive_zone(window.gobj(), zone);
  } else
#endif
  {
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface_, zone);
  }
}

void waybar::Bar::setSurfaceSize(uint32_t width, uint32_t height) {
  /* If the client is anchored to two opposite edges, layer_surface.configure will return
   * size without margins for the axis.
   * layer_surface.set_size, however, expects size with margins for the anchored axis.
   * This is not specified by wlr-layer-shell and based on actual behavior of sway.
   */
  if (vertical && height > 1) {
    height += margins_.top + margins_.bottom;
  }
  if (!vertical && width > 1) {
    width += margins_.right + margins_.left;
  }
  spdlog::debug("Set surface size {}x{} for output {}", width, height, output->name);
  zwlr_layer_surface_v1_set_size(layer_surface_, width, height);
}

// Converting string to button code rn as to avoid doing it later
void waybar::Bar::setupAltFormatKeyForModule(const std::string& module_name) {
  if (config.isMember(module_name)) {
    Json::Value& module = config[module_name];
    if (module.isMember("format-alt")) {
      if (module.isMember("format-alt-click")) {
        Json::Value& click = module["format-alt-click"];
        if (click.isString()) {
          if (click == "click-right") {
            module["format-alt-click"] = 3U;
          } else if (click == "click-middle") {
            module["format-alt-click"] = 2U;
          } else if (click == "click-backward") {
            module["format-alt-click"] = 8U;
          } else if (click == "click-forward") {
            module["format-alt-click"] = 9U;
          } else {
            module["format-alt-click"] = 1U;  // default click-left
          }
        } else {
          module["format-alt-click"] = 1U;
        }
      } else {
        module["format-alt-click"] = 1U;
      }
    }
  }
}

void waybar::Bar::setupAltFormatKeyForModuleList(const char* module_list_name) {
  if (config.isMember(module_list_name)) {
    Json::Value& modules = config[module_list_name];
    for (const Json::Value& module_name : modules) {
      if (module_name.isString()) {
        setupAltFormatKeyForModule(module_name.asString());
      }
    }
  }
}

void waybar::Bar::handleSignal(int signal) {
  for (auto& module : modules_left_) {
    auto* custom = dynamic_cast<waybar::modules::Custom*>(module.get());
    if (custom != nullptr) {
      custom->refresh(signal);
    }
  }
  for (auto& module : modules_center_) {
    auto* custom = dynamic_cast<waybar::modules::Custom*>(module.get());
    if (custom != nullptr) {
      custom->refresh(signal);
    }
  }
  for (auto& module : modules_right_) {
    auto* custom = dynamic_cast<waybar::modules::Custom*>(module.get());
    if (custom != nullptr) {
      custom->refresh(signal);
    }
  }
}

void waybar::Bar::layerSurfaceHandleConfigure(void* data, struct zwlr_layer_surface_v1* surface,
                                              uint32_t serial, uint32_t width, uint32_t height) {
  auto o = static_cast<waybar::Bar*>(data);
  if (width != o->width_ || height != o->height_) {
    o->width_ = width;
    o->height_ = height;
    o->window.set_size_request(o->width_, o->height_);
    o->window.resize(o->width_, o->height_);
    o->setExclusiveZone(width, height);
    spdlog::info(BAR_SIZE_MSG,
                 o->width_ == 1 ? "auto" : std::to_string(o->width_),
                 o->height_ == 1 ? "auto" : std::to_string(o->height_),
                 o->output->name);
    wl_surface_commit(o->surface);
  }
  zwlr_layer_surface_v1_ack_configure(surface, serial);
}

void waybar::Bar::layerSurfaceHandleClosed(void* data, struct zwlr_layer_surface_v1* /*surface*/) {
  auto o = static_cast<waybar::Bar*>(data);
  if (o->layer_surface_) {
    zwlr_layer_surface_v1_destroy(o->layer_surface_);
    o->layer_surface_ = nullptr;
  }
  o->modules_left_.clear();
  o->modules_center_.clear();
  o->modules_right_.clear();
}

auto waybar::Bar::toggle() -> void {
  visible = !visible;
  if (!visible) {
    window.get_style_context()->add_class("hidden");
    window.set_opacity(0);
  } else {
    window.get_style_context()->remove_class("hidden");
    window.set_opacity(1);
  }
  setExclusiveZone(width_, height_);
  wl_surface_commit(surface);
}

void waybar::Bar::getModules(const Factory& factory, const std::string& pos) {
  if (config[pos].isArray()) {
    for (const auto& name : config[pos]) {
      try {
        auto module = factory.makeModule(name.asString());
        if (pos == "modules-left") {
          modules_left_.emplace_back(module);
        }
        if (pos == "modules-center") {
          modules_center_.emplace_back(module);
        }
        if (pos == "modules-right") {
          modules_right_.emplace_back(module);
        }
        module->dp.connect([module, &name] {
          try {
            module->update();
          } catch (const std::exception& e) {
            spdlog::error("{}: {}", name.asString(), e.what());
          }
        });
      } catch (const std::exception& e) {
        spdlog::warn("module {}: {}", name.asString(), e.what());
      }
    }
  }
}

auto waybar::Bar::setupWidgets() -> void {
  window.add(box_);
  box_.pack_start(left_, false, false);
  box_.set_center_widget(center_);
  box_.pack_end(right_, false, false);

  // Convert to button code for every module that is used.
  setupAltFormatKeyForModuleList("modules-left");
  setupAltFormatKeyForModuleList("modules-right");
  setupAltFormatKeyForModuleList("modules-center");

  Factory factory(*this, config);
  getModules(factory, "modules-left");
  getModules(factory, "modules-center");
  getModules(factory, "modules-right");
  for (auto const& module : modules_left_) {
    left_.pack_start(*module, false, false);
  }
  for (auto const& module : modules_center_) {
    center_.pack_start(*module, false, false);
  }
  std::reverse(modules_right_.begin(), modules_right_.end());
  for (auto const& module : modules_right_) {
    right_.pack_end(*module, false, false);
  }
}
