#include "spi.h"
#include <array>
#include <cstring>
#include <vector>
#include "esp_heap_caps.h"

namespace esphome::spi {

#ifdef USE_ESP32
static const char *const TAG = "spi-esp-idf";
static const size_t MAX_TRANSFER_SIZE = 4092;  // dictated by ESP-IDF API.
static const size_t ASYNC_QUEUE_SIZE = 32;

class SPIDelegateHw : public SPIDelegate {
 public:
  SPIDelegateHw(SPIInterface channel, uint32_t data_rate, SPIBitOrder bit_order, SPIMode mode, GPIOPin *cs_pin,
                bool release_device, bool write_only)
      : SPIDelegate(data_rate, bit_order, mode, cs_pin),
        channel_(channel),
        release_device_(release_device),
        write_only_(write_only) {
    if (!this->release_device_)
      add_device_();

    if (this->write_only_) {
      ESP_LOGV(TAG, "SPI device with CS pin %d using half-duplex mode (write-only)",
               Utility::get_pin_no(this->cs_pin_));
    }
  }

  bool is_ready() override { return this->handle_ != nullptr; }

  void begin_transaction() override {
    if (this->release_device_)
      this->add_device_();
    if (this->is_ready()) {
      if (spi_device_acquire_bus(this->handle_, portMAX_DELAY) != ESP_OK)
        ESP_LOGE(TAG, "Failed to acquire SPI bus");
      SPIDelegate::begin_transaction();
    } else {
      ESP_LOGW(TAG, "SPI device not ready, cannot begin transaction");
    }
  }

  void end_transaction() override {
    if (this->is_ready()) {
      this->wait_async();
      SPIDelegate::end_transaction();
      spi_device_release_bus(this->handle_);
      if (this->release_device_) {
        spi_bus_remove_device(this->handle_);
        this->handle_ = nullptr;  // reset handle to indicate no device is registered
      }
    }
  }

  ~SPIDelegateHw() override {
    this->wait_async();
    if (this->handle_ != nullptr) {
      esp_err_t const err = spi_bus_remove_device(this->handle_);
      if (err != ESP_OK)
        ESP_LOGE(TAG, "Remove device failed - err %X", err);
    }
    if (this->async_buffer_ != nullptr)
      heap_caps_free(this->async_buffer_);
  }

  // do a transfer. either txbuf or rxbuf (but not both) may be null.
  // transfers above the maximum size will be split.
  void transfer(const uint8_t *txbuf, uint8_t *rxbuf, size_t length) override {
    if (rxbuf != nullptr && this->write_only_) {
      ESP_LOGE(TAG, "Attempted read from write-only channel");
      return;
    }
    this->wait_async();
    spi_transaction_t desc = {};
    desc.flags = 0;
    while (length != 0) {
      size_t const partial = std::min(length, MAX_TRANSFER_SIZE);
      desc.length = partial * 8;
      desc.rxlength = this->write_only_ ? 0 : partial * 8;
      desc.tx_buffer = txbuf;
      desc.rx_buffer = rxbuf;
      // polling is used for ordinary synchronous transfers because it has lower overhead than interrupt transfers.
      esp_err_t err = spi_device_polling_start(this->handle_, &desc, portMAX_DELAY);
      if (err == ESP_OK) {
        err = spi_device_polling_end(this->handle_, portMAX_DELAY);
      }
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Transmit failed - err %X", err);
        break;
      }
      length -= partial;
      if (txbuf != nullptr)
        txbuf += partial;
      if (rxbuf != nullptr)
        rxbuf += partial;
    }
  }

  void write(uint16_t data, size_t num_bits) override {
    this->wait_async();
    spi_transaction_ext_t desc = {};
    desc.command_bits = num_bits;
    desc.base.flags = SPI_TRANS_VARIABLE_CMD;
    desc.base.cmd = data;
    esp_err_t err = spi_device_polling_start(this->handle_, (spi_transaction_t *) &desc, portMAX_DELAY);
    if (err == ESP_OK) {
      err = spi_device_polling_end(this->handle_, portMAX_DELAY);
    }

    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Transmit failed - err %X", err);
    }
  }

  /**
   * Write command, address and data
   * @param cmd_bits Number of bits to write in the command phase
   * @param cmd The command value to write
   * @param addr_bits Number of bits to write in addr phase
   * @param address Address data
   * @param data Remaining data bytes
   * @param length Number of data bytes
   * @param bus_width The number of data lines to use
   */
  void write_cmd_addr_data(size_t cmd_bits, uint32_t cmd, size_t addr_bits, uint32_t address, const uint8_t *data,
                           size_t length, uint8_t bus_width) override {
    this->wait_async();
    spi_transaction_ext_t desc = {};
    if (length == 0 && cmd_bits == 0 && addr_bits == 0) {
      esph_log_w(TAG, "Nothing to transfer");
      return;
    }
    desc.base.flags = SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_DUMMY;
    if (bus_width == 4) {
      desc.base.flags |= SPI_TRANS_MODE_QIO;
    } else if (bus_width == 8) {
      desc.base.flags |= SPI_TRANS_MODE_OCT;
    }
    desc.command_bits = cmd_bits;
    desc.address_bits = addr_bits;
    desc.dummy_bits = 0;
    desc.base.rxlength = 0;
    desc.base.cmd = cmd;
    desc.base.addr = address;
    do {
      size_t chunk_size = std::min(length, MAX_TRANSFER_SIZE);
      if (data != nullptr && chunk_size != 0) {
        desc.base.length = chunk_size * 8;
        desc.base.tx_buffer = data;
        length -= chunk_size;
        data += chunk_size;
      } else {
        length = 0;
        desc.base.length = 0;
      }
      esp_err_t err = spi_device_polling_start(this->handle_, (spi_transaction_t *) &desc, portMAX_DELAY);
      if (err == ESP_OK) {
        err = spi_device_polling_end(this->handle_, portMAX_DELAY);
      }
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Transmit failed - err %X", err);
        return;
      }
      // if more data is to be sent, skip the command and address phases.
      desc.command_bits = 0;
      desc.address_bits = 0;
    } while (length != 0);
  }

  void transfer(uint8_t *ptr, size_t length) override { this->transfer(ptr, ptr, length); }

  uint8_t transfer(uint8_t data) override {
    uint8_t rxbuf;
    this->transfer(&data, &rxbuf, 1);
    return rxbuf;
  }

  void write16(uint16_t data) override { this->write(data, 16); }

  void write_array(const uint8_t *ptr, size_t length) override { this->transfer(ptr, nullptr, length); }

  bool write_array_async(const uint8_t *ptr, size_t length) override {
    if (!this->is_ready())
      return false;
    if (this->async_active_) {
      ESP_LOGW(TAG, "Async SPI write requested while previous write is still active");
      return false;
    }
    if (length == 0)
      return true;

    const size_t transaction_count = (length + MAX_TRANSFER_SIZE - 1) / MAX_TRANSFER_SIZE;
    if (transaction_count > ASYNC_QUEUE_SIZE) {
      ESP_LOGW(TAG, "Async SPI write too large for queue (%zu transactions), using synchronous transfer",
               transaction_count);
      this->write_array(ptr, length);
      return true;
    }

    if (!this->ensure_async_buffer_(length)) {
      ESP_LOGW(TAG, "Failed to allocate %zu byte DMA buffer, using synchronous transfer", length);
      this->write_array(ptr, length);
      return true;
    }

    std::memcpy(this->async_buffer_, ptr, length);
    return this->queue_async_buffer_(length);
  }

  bool write_array_async_strided(const uint8_t *ptr, size_t row_bytes, size_t rows, size_t stride) override {
    if (row_bytes == 0 || rows == 0)
      return true;
    if (stride < row_bytes) {
      ESP_LOGE(TAG, "Invalid strided SPI write: stride %zu is smaller than row size %zu", stride, row_bytes);
      return false;
    }
    if (stride == row_bytes || rows == 1)
      return this->write_array_async(ptr, row_bytes * rows);
    if (!this->is_ready())
      return false;
    if (this->async_active_) {
      ESP_LOGW(TAG, "Async SPI write requested while previous write is still active");
      return false;
    }

    const size_t length = row_bytes * rows;
    const size_t transaction_count = (length + MAX_TRANSFER_SIZE - 1) / MAX_TRANSFER_SIZE;
    if (transaction_count > ASYNC_QUEUE_SIZE) {
      ESP_LOGW(TAG, "Async strided SPI write too large for queue (%zu transactions), using synchronous transfer",
               transaction_count);
      for (size_t row = 0; row < rows; row++)
        this->write_array(ptr + row * stride, row_bytes);
      return true;
    }

    if (!this->ensure_async_buffer_(length)) {
      ESP_LOGW(TAG, "Failed to allocate %zu byte DMA buffer for strided write, using synchronous transfer", length);
      for (size_t row = 0; row < rows; row++)
        this->write_array(ptr + row * stride, row_bytes);
      return true;
    }

    uint8_t *dest = this->async_buffer_;
    for (size_t row = 0; row < rows; row++) {
      std::memcpy(dest, ptr + row * stride, row_bytes);
      dest += row_bytes;
    }
    return this->queue_async_buffer_(length);
  }

  bool async_busy() override {
    if (!this->async_active_)
      return false;

    while (this->async_completed_ < this->async_count_) {
      spi_transaction_t *completed = nullptr;
      const esp_err_t err = spi_device_get_trans_result(this->handle_, &completed, 0);
      if (err == ESP_ERR_TIMEOUT)
        return true;
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed polling async SPI transaction - err %X", err);
        this->async_active_ = false;
        return false;
      }
      this->async_completed_++;
    }

    this->async_active_ = false;
    return false;
  }

  bool wait_async() override {
    if (!this->async_active_)
      return true;

    while (this->async_completed_ < this->async_count_) {
      spi_transaction_t *completed = nullptr;
      const esp_err_t err = spi_device_get_trans_result(this->handle_, &completed, portMAX_DELAY);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed waiting for async SPI transaction - err %X", err);
        this->async_active_ = false;
        return false;
      }
      this->async_completed_++;
    }

    this->async_active_ = false;
    return true;
  }

  void write_array16(const uint16_t *data, size_t length) override {
    if (this->bit_order_ == BIT_ORDER_LSB_FIRST) {
      this->write_array((uint8_t *) data, length * 2);
    } else {
      uint16_t buffer[MAX_TRANSFER_SIZE / 2];
      while (length != 0) {
        size_t const partial = std::min(length, MAX_TRANSFER_SIZE / 2);
        for (size_t i = 0; i != partial; i++) {
          buffer[i] = SPI_SWAP_DATA_TX(*data++, 16);
        }
        this->write_array((const uint8_t *) buffer, partial * 2);
        length -= partial;
      }
    }
  }

  void read_array(uint8_t *ptr, size_t length) override { this->transfer(nullptr, ptr, length); }

 protected:
  bool ensure_async_buffer_(size_t length) {
    if (length <= this->async_buffer_size_)
      return true;

    constexpr uint32_t caps = MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL;
    uint8_t *new_buffer = nullptr;
    if (this->async_buffer_ == nullptr) {
      new_buffer = static_cast<uint8_t *>(heap_caps_malloc(length, caps));
    } else {
      new_buffer = static_cast<uint8_t *>(heap_caps_realloc(this->async_buffer_, length, caps));
      if (new_buffer == nullptr) {
        // On a very tight heap, realloc may be unable to reserve the larger block while the old DMA buffer still
        // exists. Release the old staging buffer and make one clean attempt at the requested size.
        heap_caps_free(this->async_buffer_);
        this->async_buffer_ = nullptr;
        this->async_buffer_size_ = 0;
        new_buffer = static_cast<uint8_t *>(heap_caps_malloc(length, caps));
      }
    }

    if (new_buffer == nullptr)
      return false;
    this->async_buffer_ = new_buffer;
    this->async_buffer_size_ = length;
    return true;
  }

  bool queue_async_buffer_(size_t length) {
    this->async_count_ = 0;
    this->async_completed_ = 0;

    const uint8_t *data = this->async_buffer_;
    size_t remaining = length;
    while (remaining != 0) {
      const size_t partial = std::min(remaining, MAX_TRANSFER_SIZE);
      auto &desc = this->async_descs_[this->async_count_];
      desc = {};
      desc.length = partial * 8;
      desc.rxlength = 0;
      desc.tx_buffer = data;

      const esp_err_t err = spi_device_queue_trans(this->handle_, &desc, portMAX_DELAY);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to queue async SPI transaction - err %X", err);
        this->async_active_ = this->async_count_ != 0;
        this->wait_async();
        return false;
      }

      this->async_count_++;
      data += partial;
      remaining -= partial;
    }

    this->async_active_ = true;
    return true;
  }

  bool add_device_() {
    spi_device_interface_config_t config = {};
    config.mode = static_cast<uint8_t>(this->mode_);
    config.clock_speed_hz = static_cast<int>(this->data_rate_);
    config.spics_io_num = -1;
    config.flags = 0;
    config.queue_size = ASYNC_QUEUE_SIZE;
    config.pre_cb = nullptr;
    config.post_cb = nullptr;
    if (this->bit_order_ == BIT_ORDER_LSB_FIRST)
      config.flags |= SPI_DEVICE_BIT_LSBFIRST;
    if (this->write_only_)
      config.flags |= SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY;
    esp_err_t const err = spi_bus_add_device(this->channel_, &config, &this->handle_);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Add device failed - err %X", err);
      return false;
    }
    return true;
  }

  SPIInterface channel_{};
  spi_device_handle_t handle_{};
  bool release_device_{false};
  bool write_only_{false};

  std::array<spi_transaction_t, ASYNC_QUEUE_SIZE> async_descs_{};
  uint8_t *async_buffer_{nullptr};
  size_t async_buffer_size_{0};
  size_t async_count_{0};
  size_t async_completed_{0};
  bool async_active_{false};
};

class SPIBusHw : public SPIBus {
 public:
  SPIBusHw(GPIOPin *clk, GPIOPin *sdo, GPIOPin *sdi, SPIInterface channel, std::vector<uint8_t> data_pins)
      : SPIBus(clk, sdo, sdi), channel_(channel) {
    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num = Utility::get_pin_no(clk);
    buscfg.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_SCLK;
    if (data_pins.empty()) {
      buscfg.mosi_io_num = Utility::get_pin_no(sdo);
      buscfg.miso_io_num = Utility::get_pin_no(sdi);
      buscfg.quadwp_io_num = -1;
      buscfg.quadhd_io_num = -1;
    } else {
      buscfg.data0_io_num = data_pins[0];
      buscfg.data1_io_num = data_pins[1];
      buscfg.data2_io_num = data_pins[2];
      buscfg.data3_io_num = data_pins[3];
      if (data_pins.size() == 8) {
        buscfg.data4_io_num = data_pins[4];
        buscfg.data5_io_num = data_pins[5];
        buscfg.data6_io_num = data_pins[6];
        buscfg.data7_io_num = data_pins[7];
        buscfg.flags |= SPICOMMON_BUSFLAG_OCTAL;
      } else {
        buscfg.data4_io_num = -1;
        buscfg.data5_io_num = -1;
        buscfg.data6_io_num = -1;
        buscfg.data7_io_num = -1;
        buscfg.flags |= SPICOMMON_BUSFLAG_QUAD;
      }
    }
    buscfg.max_transfer_sz = MAX_TRANSFER_SIZE;
    auto err = spi_bus_initialize(channel, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK)
      ESP_LOGE(TAG, "Bus init failed - err %X", err);
  }

  SPIDelegate *get_delegate(uint32_t data_rate, SPIBitOrder bit_order, SPIMode mode, GPIOPin *cs_pin,
                            bool release_device, bool write_only) override {
    return new SPIDelegateHw(this->channel_, data_rate, bit_order, mode, cs_pin, release_device,
                             write_only || Utility::get_pin_no(this->sdi_pin_) == -1);
  }

 protected:
  SPIInterface channel_{};

  bool is_hw() override { return true; }
};

SPIBus *SPIComponent::get_bus(SPIInterface interface, GPIOPin *clk, GPIOPin *sdo, GPIOPin *sdi,
                              const std::vector<uint8_t> &data_pins) {
  return new SPIBusHw(clk, sdo, sdi, interface, data_pins);
}

#endif  // USE_ESP32
}  // namespace esphome::spi
