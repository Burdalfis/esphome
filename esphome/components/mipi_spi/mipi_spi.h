#pragma once

#include <utility>

#include "esphome/components/spi/spi.h"
#include "esphome/components/display/display.h"
#include "esphome/components/display/display_color_utils.h"
#include "esphome/core/helpers.h"

namespace esphome::mipi_spi {

constexpr static const char *const TAG = "display.mipi_spi";

static constexpr size_t MIPI_SPI_MAX_CMD_LOG_BYTES = 64;

static constexpr uint8_t SW_RESET_CMD = 0x01;
static constexpr uint8_t SLEEP_OUT = 0x11;
static constexpr uint8_t NORON = 0x13;
static constexpr uint8_t INVERT_OFF = 0x20;
static constexpr uint8_t INVERT_ON = 0x21;
static constexpr uint8_t ALL_ON = 0x23;
static constexpr uint8_t WRAM = 0x24;
static constexpr uint8_t MIPI = 0x26;
static constexpr uint8_t DISPLAY_ON = 0x29;
static constexpr uint8_t RASET = 0x2B;
static constexpr uint8_t CASET = 0x2A;
static constexpr uint8_t WDATA = 0x2C;
static constexpr uint8_t TEON = 0x35;
static constexpr uint8_t MADCTL_CMD = 0x36;
static constexpr uint8_t PIXFMT = 0x3A;
static constexpr uint8_t BRIGHTNESS = 0x51;
static constexpr uint8_t SWIRE1 = 0x5A;
static constexpr uint8_t SWIRE2 = 0x5B;
static constexpr uint8_t PAGESEL = 0xFE;

static constexpr uint8_t MADCTL_MY = 0x80;
static constexpr uint8_t MADCTL_MX = 0x40;
static constexpr uint8_t MADCTL_MV = 0x20;
static constexpr uint8_t MADCTL_RGB = 0x00;
static constexpr uint8_t MADCTL_BGR = 0x08;
static constexpr uint8_t MADCTL_XFLIP = 0x02;
static constexpr uint8_t MADCTL_YFLIP = 0x01;
static constexpr uint16_t MADCTL_FLIP_FLAG = 0x100;

static constexpr uint8_t DELAY_FLAG = 0xFF;
static inline void put16_be(uint8_t *buf, uint16_t value) {
  buf[0] = value >> 8;
  buf[1] = value;
}

enum PixelMode {
  PIXEL_MODE_8 = 1,
  PIXEL_MODE_16 = 2,
  PIXEL_MODE_18 = 3,
};

enum BusType {
  BUS_TYPE_SINGLE = 1,
  BUS_TYPE_QUAD = 4,
  BUS_TYPE_OCTAL = 8,
  BUS_TYPE_SINGLE_16 = 16,
};

void internal_dump_config(const char *model, int width, int height, int offset_width, int offset_height, uint8_t madctl,
                          bool invert_colors, int display_bits, bool is_big_endian, const optional<uint8_t> &brightness,
                          GPIOPin *cs, GPIOPin *reset, GPIOPin *dc, int spi_mode, uint32_t data_rate, int bus_width,
                          bool has_hardware_rotation);

template<typename BUFFERTYPE, PixelMode BUFFERPIXEL, bool IS_BIG_ENDIAN, PixelMode DISPLAYPIXEL, BusType BUS_TYPE,
         int WIDTH, int HEIGHT, int OFFSET_WIDTH, int OFFSET_HEIGHT, int PAD_WIDTH, int PAD_HEIGHT, uint16_t MADCTL,
         bool HAS_HARDWARE_ROTATION>
class MipiSpi : public display::Display,
                public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, spi::CLOCK_PHASE_LEADING,
                                      spi::DATA_RATE_1MHZ> {
 public:
  MipiSpi() = default;
  void update() override { this->stop_poller(); }
  void draw_pixel_at(int x, int y, Color color) override {}
  void set_model(const char *model) { this->model_ = model; }
  void set_reset_pin(GPIOPin *reset_pin) { this->reset_pin_ = reset_pin; }
  void set_enable_pins(std::vector<GPIOPin *> enable_pins) { this->enable_pins_ = std::move(enable_pins); }
  void set_dc_pin(GPIOPin *dc_pin) { this->dc_pin_ = dc_pin; }
  void set_invert_colors(bool invert_colors) {
    this->invert_colors_ = invert_colors;
    this->reset_params_();
  }
  void set_brightness(uint8_t brightness) {
    this->brightness_ = brightness;
    this->reset_params_();
  }
  void set_rotation(display::DisplayRotation rotation) override {
    this->rotation_ = rotation;
    if constexpr (HAS_HARDWARE_ROTATION) {
      this->reset_params_();
    }
  }
  display::DisplayType get_display_type() override { return display::DisplayType::DISPLAY_TYPE_COLOR; }

  int get_width() override {
    if (this->rotation_ == display::DISPLAY_ROTATION_90_DEGREES ||
        this->rotation_ == display::DISPLAY_ROTATION_270_DEGREES)
      return HEIGHT;
    return WIDTH;
  }

  int get_height() override {
    if (this->rotation_ == display::DISPLAY_ROTATION_90_DEGREES ||
        this->rotation_ == display::DISPLAY_ROTATION_270_DEGREES)
      return WIDTH;
    return HEIGHT;
  }

  void set_init_sequence(const std::vector<uint8_t> &sequence) { this->init_sequence_ = sequence; }

  void setup() override {
    this->spi_setup();
    if (this->dc_pin_ != nullptr) {
      this->dc_pin_->setup();
      this->dc_pin_->digital_write(false);
    }
    for (auto *pin : this->enable_pins_) {
      pin->setup();
      pin->digital_write(true);
    }
    if (this->reset_pin_ != nullptr) {
      this->reset_pin_->setup();
      this->reset_pin_->digital_write(true);
      delay(5);
      this->reset_pin_->digital_write(false);
      delay(5);
      this->reset_pin_->digital_write(true);
    }

    auto when = millis() + 120;
    size_t index = 0;
    auto &vec = this->init_sequence_;
    while (index != vec.size()) {
      if (vec.size() - index < 2) {
        esph_log_e(TAG, "Malformed init sequence");
        this->mark_failed();
        return;
      }
      uint8_t cmd = vec[index++];
      uint8_t x = vec[index++];
      if (x == DELAY_FLAG) {
        if (cmd == 0) {
          cmd = clamp_at_least((int) (when - millis()), 0);
        }
        esph_log_d(TAG, "Delay %dms", cmd);
        delay(cmd);
      } else {
        uint8_t num_args = x & 0x7F;
        if (vec.size() - index < num_args) {
          esph_log_e(TAG, "Malformed init sequence");
          this->mark_failed();
          return;
        }
        const auto *ptr = vec.data() + index;
        this->write_command_(cmd, ptr, num_args);
        index += num_args;
      }
    }
    this->reset_params_();
    this->init_sequence_.clear();
  }

  void draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr, display::ColorOrder order,
                      display::ColorBitness bitness, bool big_endian, int x_offset, int y_offset, int x_pad) override {
    if (this->is_failed())
      return;
    if (w <= 0 || h <= 0)
      return;
    if (get_pixel_mode(bitness) != BUFFERPIXEL || big_endian != IS_BIG_ENDIAN) {
      esph_log_e(TAG, "Unsupported color depth or bit order");
      return;
    }
    this->write_to_display_(x_start, y_start, w, h, reinterpret_cast<const BUFFERTYPE *>(ptr), x_offset, y_offset,
                            x_pad);
  }

  void dump_config() override {
    internal_dump_config(this->model_, this->get_width(), this->get_height(), this->get_offset_width_(),
                         this->get_offset_height_(), (uint8_t) MADCTL, this->invert_colors_, DISPLAYPIXEL * 8,
                         IS_BIG_ENDIAN, this->brightness_, this->cs_, this->reset_pin_, this->dc_pin_, this->mode_,
                         this->data_rate_, BUS_TYPE, HAS_HARDWARE_ROTATION);
  }

 protected:
  int get_width_internal() override {
    if constexpr (HAS_HARDWARE_ROTATION)
      return get_width();
    return WIDTH;
  }
  int get_height_internal() override {
    if constexpr (HAS_HARDWARE_ROTATION)
      return get_height();
    return HEIGHT;
  }

  void write_command_(uint8_t cmd, uint8_t data) { this->write_command_(cmd, &data, 1); }
  void write_command_(uint8_t cmd) { this->write_command_(cmd, &cmd, 0); }

  void write_command_(uint8_t cmd, const uint8_t *bytes, size_t len) {
    char hex_buf[format_hex_pretty_size(MIPI_SPI_MAX_CMD_LOG_BYTES)];
    if (this->init_sequence_.empty()) {
      esph_log_v(TAG, "Command %02X, length %d, bytes %s", cmd, len, format_hex_pretty_to(hex_buf, bytes, len));
    } else {
      esph_log_d(TAG, "Command %02X, length %d, bytes %s", cmd, len, format_hex_pretty_to(hex_buf, bytes, len));
    }
    if constexpr (BUS_TYPE == BUS_TYPE_QUAD) {
      this->enable();
      this->write_cmd_addr_data(8, 0x02, 24, cmd << 8, bytes, len);
      this->disable();
    } else if constexpr (BUS_TYPE == BUS_TYPE_OCTAL) {
      this->dc_pin_->digital_write(false);
      this->enable();
      this->write_cmd_addr_data(0, 0, 0, 0, &cmd, 1, 8);
      this->disable();
      this->dc_pin_->digital_write(true);
      if (len != 0) {
        this->enable();
        this->write_cmd_addr_data(0, 0, 0, 0, bytes, len, 8);
        this->disable();
      }
    } else if constexpr (BUS_TYPE == BUS_TYPE_SINGLE) {
      this->dc_pin_->digital_write(false);
      this->enable();
      this->write_byte(cmd);
      this->disable();
      this->dc_pin_->digital_write(true);
      if (len != 0) {
        this->enable();
        this->write_array(bytes, len);
        this->disable();
      }
    } else if constexpr (BUS_TYPE == BUS_TYPE_SINGLE_16) {
      this->dc_pin_->digital_write(false);
      this->enable();
      this->write_byte(cmd);
      this->disable();
      this->dc_pin_->digital_write(true);
      for (size_t i = 0; i != len; i++) {
        this->enable();
        this->write_byte(0);
        this->write_byte(bytes[i]);
        this->disable();
      }
    }
  }

  void reset_params_() {
    if (!this->is_ready())
      return;
    this->write_command_(this->invert_colors_ ? INVERT_ON : INVERT_OFF);
    if (this->brightness_.has_value())
      this->write_command_(BRIGHTNESS, this->brightness_.value());

    uint8_t madctl = (uint8_t) MADCTL;
    constexpr bool use_flips = (MADCTL & MADCTL_FLIP_FLAG) != 0;
    constexpr uint8_t x_mask = use_flips ? MADCTL_XFLIP : MADCTL_MX;
    constexpr uint8_t y_mask = use_flips ? MADCTL_YFLIP : MADCTL_MY;
    if constexpr (HAS_HARDWARE_ROTATION) {
      switch (this->rotation_) {
        default:
          break;
        case display::DISPLAY_ROTATION_90_DEGREES:
          madctl ^= x_mask;
          madctl ^= MADCTL_MV;
          break;
        case display::DISPLAY_ROTATION_180_DEGREES:
          madctl ^= x_mask;
          madctl ^= y_mask;
          break;
        case display::DISPLAY_ROTATION_270_DEGREES:
          madctl ^= y_mask;
          madctl ^= MADCTL_MV;
          break;
      }
    }
    esph_log_d(TAG, "Setting MADCTL for rotation %d, value %X", this->rotation_, madctl);
    this->write_command_(MADCTL_CMD, madctl);
  }

  uint16_t get_offset_width_() const {
    if constexpr (HAS_HARDWARE_ROTATION) {
      switch (this->rotation_) {
        case display::DISPLAY_ROTATION_90_DEGREES:
          return OFFSET_HEIGHT;
        case display::DISPLAY_ROTATION_180_DEGREES:
          return PAD_WIDTH;
        case display::DISPLAY_ROTATION_270_DEGREES:
          return PAD_HEIGHT;
        default:
          break;
      }
    }
    return OFFSET_WIDTH;
  }

  uint16_t get_offset_height_() const {
    if constexpr (HAS_HARDWARE_ROTATION) {
      switch (this->rotation_) {
        case display::DISPLAY_ROTATION_90_DEGREES:
          return PAD_WIDTH;
        case display::DISPLAY_ROTATION_180_DEGREES:
          return PAD_HEIGHT;
        case display::DISPLAY_ROTATION_270_DEGREES:
          return OFFSET_WIDTH;
        default:
          break;
      }
    }
    return OFFSET_HEIGHT;
  }

  void set_addr_window_(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    esph_log_v(TAG, "Set addr %d/%d, %d/%d", x1, y1, x2, y2);
    uint8_t buf[4];
    x1 += get_offset_width_();
    x2 += get_offset_width_();
    y1 += get_offset_height_();
    y2 += get_offset_height_();
    put16_be(buf, y1);
    put16_be(buf + 2, y2);
    this->write_command_(RASET, buf, sizeof buf);
    put16_be(buf, x1);
    put16_be(buf + 2, x2);
    this->write_command_(CASET, buf, sizeof buf);
    if constexpr (BUS_TYPE != BUS_TYPE_QUAD) {
      this->write_command_(WDATA);
    }
  }

  static PixelMode get_pixel_mode(display::ColorBitness bitness) {
    switch (bitness) {
      case display::COLOR_BITNESS_888:
        return PIXEL_MODE_18;
      case display::COLOR_BITNESS_565:
        return PIXEL_MODE_16;
      default:
        return PIXEL_MODE_8;
    }
  }

  void write_display_data_(const uint8_t *ptr, size_t w, size_t h, size_t stride) {
    if (stride == w) {
      if constexpr (BUS_TYPE == BUS_TYPE_SINGLE || BUS_TYPE == BUS_TYPE_SINGLE_16) {
        this->write_array(ptr, w * h);
      } else if constexpr (BUS_TYPE == BUS_TYPE_QUAD) {
        this->write_cmd_addr_data(8, 0x32, 24, WDATA << 8, ptr, w * h, 4);
      } else if constexpr (BUS_TYPE == BUS_TYPE_OCTAL) {
        this->write_cmd_addr_data(0, 0, 0, 0, ptr, w * h, 8);
      }
    } else {
      for (size_t y = 0; y != h; y++) {
        if constexpr (BUS_TYPE == BUS_TYPE_SINGLE || BUS_TYPE == BUS_TYPE_SINGLE_16) {
          this->write_array(ptr, w);
        } else if constexpr (BUS_TYPE == BUS_TYPE_QUAD) {
          this->write_cmd_addr_data(8, 0x32, 24, WDATA << 8, ptr, w, 4);
        } else if constexpr (BUS_TYPE == BUS_TYPE_OCTAL) {
          this->write_cmd_addr_data(0, 0, 0, 0, ptr, w, 8);
        }
        ptr += stride;
      }
    }
  }

  void write_to_display_(int x_start, int y_start, int w, int h, const BUFFERTYPE *ptr, int x_offset, int y_offset,
                         int x_pad) {
    this->set_addr_window_(x_start, y_start, x_start + w - 1, y_start + h - 1);
    this->enable();
    ptr += y_offset * (x_offset + w + x_pad) + x_offset;
    if constexpr (BUFFERPIXEL == DISPLAYPIXEL) {
      this->write_display_data_(reinterpret_cast<const uint8_t *>(ptr), w * sizeof(BUFFERTYPE), h,
                                (x_offset + w + x_pad) * sizeof(BUFFERTYPE));
    } else {
      uint8_t dbuffer[DISPLAYPIXEL * 48];
      uint8_t *dptr = dbuffer;
      auto stride = x_offset + w + x_pad;
      for (size_t y = 0; y != static_cast<size_t>(h); y++) {
        for (size_t x = 0; x != static_cast<size_t>(w); x++) {
          auto color_val = ptr[y * stride + x];
          if constexpr (DISPLAYPIXEL == PIXEL_MODE_18 && BUFFERPIXEL == PIXEL_MODE_16) {
            if constexpr (IS_BIG_ENDIAN) {
              *dptr++ = color_val & 0xF8;
              *dptr++ = ((color_val & 0x7) << 5) | (color_val & 0xE000) >> 11;
              *dptr++ = (color_val >> 5) & 0xF8;
            } else {
              *dptr++ = (color_val >> 8) & 0xF8;
              *dptr++ = (color_val & 0x7E0) >> 3;
              *dptr++ = color_val << 3;
            }
          } else if constexpr (DISPLAYPIXEL == PIXEL_MODE_18 && BUFFERPIXEL == PIXEL_MODE_8) {
            *dptr++ = color_val << 6;
            *dptr++ = (color_val & 0x1C) << 3;
            *dptr++ = (color_val & 0xE0);
          } else if constexpr (DISPLAYPIXEL == PIXEL_MODE_16 && BUFFERPIXEL == PIXEL_MODE_8) {
            if constexpr (IS_BIG_ENDIAN) {
              *dptr++ = (color_val & 0xE0) | ((color_val & 0x1C) >> 2);
              *dptr++ = (color_val & 3) << 3;
            } else {
              *dptr++ = (color_val & 3) << 3;
              *dptr++ = (color_val & 0xE0) | ((color_val & 0x1C) >> 2);
            }
          }
          if (dptr == dbuffer + sizeof(dbuffer)) {
            this->write_display_data_(dbuffer, sizeof(dbuffer), 1, sizeof(dbuffer));
            dptr = dbuffer;
          }
        }
      }
      if (dptr != dbuffer) {
        this->write_display_data_(dbuffer, dptr - dbuffer, 1, dptr - dbuffer);
      }
    }
    this->disable();
  }

  GPIOPin *reset_pin_{nullptr};
  std::vector<GPIOPin *> enable_pins_{};
  GPIOPin *dc_pin_{nullptr};
  bool invert_colors_{};
  optional<uint8_t> brightness_{};
  const char *model_{"Unknown"};
  std::vector<uint8_t> init_sequence_{};
};

template<typename BUFFERTYPE, PixelMode BUFFERPIXEL, bool IS_BIG_ENDIAN, PixelMode DISPLAYPIXEL, BusType BUS_TYPE,
         uint16_t WIDTH, uint16_t HEIGHT, int OFFSET_WIDTH, int OFFSET_HEIGHT, int PAD_WIDTH, int PAD_HEIGHT,
         uint16_t MADCTL, bool HAS_HARDWARE_ROTATION, int FRACTION, unsigned ROUNDING>
class MipiSpiBuffer
    : public MipiSpi<BUFFERTYPE, BUFFERPIXEL, IS_BIG_ENDIAN, DISPLAYPIXEL, BUS_TYPE, WIDTH, HEIGHT, OFFSET_WIDTH,
                     OFFSET_HEIGHT, PAD_WIDTH, PAD_HEIGHT, MADCTL, HAS_HARDWARE_ROTATION> {
 public:
  static constexpr size_t round_buffer(size_t size) { return (size + ROUNDING - 1) / ROUNDING * ROUNDING; }

  MipiSpiBuffer() = default;

  void dump_config() override {
    MipiSpi<BUFFERTYPE, BUFFERPIXEL, IS_BIG_ENDIAN, DISPLAYPIXEL, BUS_TYPE, WIDTH, HEIGHT, OFFSET_WIDTH, OFFSET_HEIGHT,
            PAD_WIDTH, PAD_HEIGHT, MADCTL, HAS_HARDWARE_ROTATION>::dump_config();
    esph_log_config(TAG,
                    "  Rotation: %d°\n"
                    "  Buffer pixels: %d bits\n"
                    "  Buffer fraction: 1/%d\n"
                    "  Buffer bytes: %zu\n"
                    "  Draw rounding: %u",
                    this->rotation_, BUFFERPIXEL * 8, FRACTION,
                    sizeof(BUFFERTYPE) * round_buffer(WIDTH) * round_buffer(HEIGHT) / FRACTION, ROUNDING);
  }

  void setup() override {
    MipiSpi<BUFFERTYPE, BUFFERPIXEL, IS_BIG_ENDIAN, DISPLAYPIXEL, BUS_TYPE, WIDTH, HEIGHT, OFFSET_WIDTH, OFFSET_HEIGHT,
            PAD_WIDTH, PAD_HEIGHT, MADCTL, HAS_HARDWARE_ROTATION>::setup();
    RAMAllocator<BUFFERTYPE> allocator{};
    this->buffer_ = allocator.allocate(round_buffer(WIDTH) * round_buffer(HEIGHT) / FRACTION);
    if (this->buffer_ == nullptr) {
      this->mark_failed(LOG_STR("Buffer allocation failed"));
    }
  }

  // Direct framebuffer access for performance-sensitive renderers. These helpers are intentionally restricted to a
  // full framebuffer so callers can use logical screen coordinates without having to account for start_line_.
  BUFFERTYPE *get_framebuffer() {
    if constexpr (FRACTION != 1)
      return nullptr;
    return this->buffer_;
  }

  size_t get_framebuffer_stride() {
    if constexpr (FRACTION != 1)
      return 0;
    return round_buffer(this->get_width_internal());
  }

  BUFFERTYPE native_color(const Color &color) { return convert_color(color); }

  void mark_dirty(int x0, int y0, int x1, int y1) {
    if constexpr (FRACTION != 1)
      return;
    if (x0 > x1)
      std::swap(x0, x1);
    if (y0 > y1)
      std::swap(y0, y1);
    x0 = clamp(x0, 0, this->get_width_internal() - 1);
    x1 = clamp(x1, 0, this->get_width_internal() - 1);
    y0 = clamp(y0, 0, this->get_height_internal() - 1);
    y1 = clamp(y1, 0, this->get_height_internal() - 1);
    if (x0 < this->x_low_)
      this->x_low_ = x0;
    if (x1 > this->x_high_)
      this->x_high_ = x1;
    if (y0 < this->y_low_)
      this->y_low_ = y0;
    if (y1 > this->y_high_)
      this->y_high_ = y1;
  }

  bool render_only() {
    if (this->is_failed())
      return false;
    if constexpr (FRACTION != 1) {
      esph_log_e(TAG, "render_only requires buffer_size: 100%%");
      return false;
    }
    this->start_line_ = 0;
    this->end_line_ = this->get_height_internal();
    if (this->auto_clear_enabled_)
      this->clear();
    if (this->page_ != nullptr) {
      this->page_->get_writer()(*this);
    } else if (this->writer_.has_value()) {
      (*this->writer_)(*this);
    } else {
      this->test_card();
    }
    return true;
  }

  bool service_async_flush() {
    if (!this->async_transaction_open_)
      return false;
    if (this->async_busy())
      return true;
    this->disable();
    this->async_transaction_open_ = false;
    return false;
  }

  bool async_flush_busy() { return this->service_async_flush(); }

  bool start_async_flush() {
    if (this->is_failed())
      return false;
    if constexpr (FRACTION != 1 || BUS_TYPE != BUS_TYPE_SINGLE || BUFFERPIXEL != DISPLAYPIXEL) {
      esph_log_e(TAG, "Async MIPI flush currently requires a full buffer, single-bit SPI and matching pixel formats");
      return false;
    }

    if (this->service_async_flush())
      return false;

    if (this->x_low_ > this->x_high_ || this->y_low_ > this->y_high_)
      return true;

    this->x_low_ = this->x_low_ / ROUNDING * ROUNDING;
    this->y_low_ = this->y_low_ / ROUNDING * ROUNDING;
    this->x_high_ = round_buffer(this->x_high_ + 1) - 1;
    this->y_high_ = clamp_at_most(round_buffer(this->y_high_ + 1) - 1, this->get_height_internal() - 1);

    const int w = this->x_high_ - this->x_low_ + 1;
    const int h = this->y_high_ - this->y_low_ + 1;
    const size_t row_bytes = w * sizeof(BUFFERTYPE);
    const size_t stride_bytes = round_buffer(this->get_width_internal()) * sizeof(BUFFERTYPE);
    const auto *source = reinterpret_cast<const uint8_t *>(
        this->buffer_ + this->y_low_ * round_buffer(this->get_width_internal()) + this->x_low_);

    this->set_addr_window_(this->x_low_, this->y_low_, this->x_high_, this->y_high_);
    this->enable();
    const bool queued = this->write_array_async_strided(source, row_bytes, h, stride_bytes);
    if (!queued) {
      this->disable();
      return false;
    }

    this->x_low_ = this->get_width_internal();
    this->y_low_ = this->get_height_internal();
    this->x_high_ = 0;
    this->y_high_ = 0;

    this->async_transaction_open_ = this->async_busy();
    if (!this->async_transaction_open_)
      this->disable();
    return true;
  }

  void update() override {
#if ESPHOME_LOG_LEVEL == ESPHOME_LOG_LEVEL_VERBOSE
    auto now = millis();
#endif
    if (this->is_failed()) {
      return;
    }
    auto increment = (this->get_height_internal() / FRACTION / ROUNDING) * ROUNDING;
    for (this->start_line_ = 0; this->start_line_ < this->get_height_internal(); this->start_line_ = this->end_line_) {
#if ESPHOME_LOG_LEVEL == ESPHOME_LOG_LEVEL_VERBOSE
      auto lap = millis();
#endif
      this->end_line_ = clamp_at_most(this->start_line_ + increment, this->get_height_internal());
      if (this->auto_clear_enabled_) {
        this->clear();
      }
      if (this->page_ != nullptr) {
        this->page_->get_writer()(*this);
      } else if (this->writer_.has_value()) {
        (*this->writer_)(*this);
      } else {
        this->test_card();
      }
#if ESPHOME_LOG_LEVEL == ESPHOME_LOG_LEVEL_VERBOSE
      esph_log_v(TAG, "Drawing from line %d took %dms", this->start_line_, millis() - lap);
      lap = millis();
#endif
      if (this->x_low_ > this->x_high_ || this->y_low_ > this->y_high_)
        return;
      esph_log_v(TAG, "x_low %d, y_low %d, x_high %d, y_high %d", this->x_low_, this->y_low_, this->x_high_,
                 this->y_high_);
      this->x_low_ = this->x_low_ / ROUNDING * ROUNDING;
      this->y_low_ = this->y_low_ / ROUNDING * ROUNDING;
      this->x_high_ = round_buffer(this->x_high_ + 1) - 1;
      this->y_high_ = clamp_at_most(round_buffer(this->y_high_ + 1) - 1, this->end_line_ - 1);
      int w = this->x_high_ - this->x_low_ + 1;
      int h = this->y_high_ - this->y_low_ + 1;
      this->write_to_display_(this->x_low_, this->y_low_, w, h, this->buffer_, this->x_low_,
                              this->y_low_ - this->start_line_,
                              round_buffer(this->get_width_internal()) - w - this->x_low_);
      this->x_low_ = this->get_width_internal();
      this->y_low_ = this->get_height_internal();
      this->x_high_ = 0;
      this->y_high_ = 0;
#if ESPHOME_LOG_LEVEL == ESPHOME_LOG_LEVEL_VERBOSE
      esph_log_v(TAG, "Write to display took %dms", millis() - lap);
      lap = millis();
#endif
    }
#if ESPHOME_LOG_LEVEL == ESPHOME_LOG_LEVEL_VERBOSE
    esph_log_v(TAG, "Total update took %dms", millis() - now);
#endif
  }

  void draw_pixel_at(int x, int y, Color color) override {
    if (!this->get_clipping().inside(x, y))
      return;
    if constexpr (not HAS_HARDWARE_ROTATION) {
      if (this->rotation_ == display::DISPLAY_ROTATION_180_DEGREES) {
        x = WIDTH - x - 1;
        y = HEIGHT - y - 1;
      } else if (this->rotation_ == display::DISPLAY_ROTATION_90_DEGREES) {
        auto tmp = x;
        x = WIDTH - y - 1;
        y = tmp;
      } else if (this->rotation_ == display::DISPLAY_ROTATION_270_DEGREES) {
        auto tmp = y;
        y = HEIGHT - x - 1;
        x = tmp;
      }
    }
    if (x < 0 || x >= this->get_width_internal() || y < this->start_line_ || y >= this->end_line_)
      return;
    this->buffer_[(y - this->start_line_) * round_buffer(this->get_width_internal()) + x] = convert_color(color);
    if (x < this->x_low_) {
      this->x_low_ = x;
    }
    if (x > this->x_high_) {
      this->x_high_ = x;
    }
    if (y < this->y_low_) {
      this->y_low_ = y;
    }
    if (y > this->y_high_) {
      this->y_high_ = y;
    }
  }

  void fill(Color color) override {
    if (this->get_clipping().is_set()) {
      display::Display::fill(color);
      return;
    }

    this->x_low_ = 0;
    this->y_low_ = this->start_line_;
    this->x_high_ = this->get_width_internal() - 1;
    this->y_high_ = this->end_line_ - 1;
    std::fill_n(this->buffer_, (this->end_line_ - this->start_line_) * round_buffer(this->get_width_internal()),
                convert_color(color));
  }

 protected:
  static BUFFERTYPE convert_color(const Color &color) {
    if constexpr (BUFFERPIXEL == PIXEL_MODE_8) {
      return (color.red & 0xE0) | (color.g & 0xE0) >> 3 | color.b >> 6;
    } else if constexpr (BUFFERPIXEL == PIXEL_MODE_16) {
      if constexpr (IS_BIG_ENDIAN) {
        return (color.r & 0xF8) | color.g >> 5 | (color.g & 0x1C) << 11 | (color.b & 0xF8) << 5;
      } else {
        return (color.r & 0xF8) << 8 | (color.g & 0xFC) << 3 | color.b >> 3;
      }
    }
    return static_cast<BUFFERTYPE>(0);
  }

  BUFFERTYPE *buffer_{};
  uint16_t x_low_{WIDTH};
  uint16_t y_low_{HEIGHT};
  uint16_t x_high_{0};
  uint16_t y_high_{0};
  uint16_t start_line_{0};
  uint16_t end_line_{1};
  bool async_transaction_open_{false};
};

}  // namespace esphome::mipi_spi
