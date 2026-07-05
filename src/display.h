#pragma once
// LovyanGFX config for ES3N28P: ILI9341 240x320 over SPI2.
// Pins + BGR + inversion carried over from the (working) Rust/mipidsi setup:
//   SCK12 MOSI11 MISO13 CS10 DC46, backlight GPIO45 (active high), RST via EN.
//   color_order = BGR, invert = true. Mirror/rotation tuned on device.
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel;
  lgfx::Bus_SPI       _bus;
  lgfx::Light_PWM     _light;

 public:
  LGFX() {
    {
      auto c = _bus.config();
      c.spi_host   = SPI2_HOST;
      c.spi_mode   = 0;
      c.freq_write = 40000000;
      c.freq_read  = 16000000;
      c.pin_sclk   = 12;
      c.pin_mosi   = 11;
      c.pin_miso   = 13;
      c.pin_dc     = 46;
      _bus.config(c);
      _panel.setBus(&_bus);
    }
    {
      auto c = _panel.config();
      c.pin_cs       = 10;
      c.pin_rst      = -1;   // shared with EN (software reset)
      c.pin_busy     = -1;
      c.panel_width  = 240;
      c.panel_height = 320;
      c.offset_x     = 0;
      c.offset_y     = 0;
      c.readable     = true;
      c.invert       = true;   // bg renders white (correct) -> keep
      c.rgb_order    = false;  // diagnostic: color565 red/blue were swapped with BGR
      c.bus_shared   = false;
      _panel.config(c);
    }
    {
      auto c = _light.config();
      c.pin_bl      = 45;
      c.invert      = false;
      c.freq        = 12000;
      c.pwm_channel = 7;
      _light.config(c);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};
