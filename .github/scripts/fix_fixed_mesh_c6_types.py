#!/usr/bin/env python3
from pathlib import Path

p = Path('esphome/components/mipi_spi/fixed_mesh_renderer.h')
s = p.read_text()
old = '''    int dirty_x0 = 0;
    int dirty_y0 = 0;
    int dirty_x1 = -1;
    int dirty_y1 = -1;
    if (!this->frame_initialized_) {
      dirty_x1 = this->screen_w_ - 1;
      dirty_y1 = this->screen_h_ - 1;
    } else if (this->have_old_box_) {
      int raw_x0 = this->old_x0_;
      int raw_y0 = this->old_y0_;
      int raw_x1 = this->old_x1_;
      int raw_y1 = this->old_y1_;
      if (this->have_new_box_) {
        raw_x0 = std::min(raw_x0, this->new_x0_);
        raw_y0 = std::min(raw_y0, this->new_y0_);
        raw_x1 = std::max(raw_x1, this->new_x1_);
        raw_y1 = std::max(raw_y1, this->new_y1_);
      }
      dirty_x0 = std::max(0, raw_x0);
      dirty_y0 = std::max(0, raw_y0);
      dirty_x1 = std::min(this->screen_w_ - 1, raw_x1);
      dirty_y1 = std::min(this->screen_h_ - 1, raw_y1);
    } else if (this->have_new_box_) {
      dirty_x0 = std::max(0, this->new_x0_);
      dirty_y0 = std::max(0, this->new_y0_);
      dirty_x1 = std::min(this->screen_w_ - 1, this->new_x1_);
      dirty_y1 = std::min(this->screen_h_ - 1, this->new_y1_);
    }
'''
new = '''    // Keep dirty/bounds math explicitly int32_t. On ESP32-C6, int32_t is
    // `long int` while the display dimensions are ordinary `int`, so leaving
    // these as mixed types breaks std::min/max template deduction.
    int32_t dirty_x0 = 0;
    int32_t dirty_y0 = 0;
    int32_t dirty_x1 = -1;
    int32_t dirty_y1 = -1;
    const int32_t screen_x1 = static_cast<int32_t>(this->screen_w_ - 1);
    const int32_t screen_y1 = static_cast<int32_t>(this->screen_h_ - 1);
    if (!this->frame_initialized_) {
      dirty_x1 = screen_x1;
      dirty_y1 = screen_y1;
    } else if (this->have_old_box_) {
      int32_t raw_x0 = this->old_x0_;
      int32_t raw_y0 = this->old_y0_;
      int32_t raw_x1 = this->old_x1_;
      int32_t raw_y1 = this->old_y1_;
      if (this->have_new_box_) {
        raw_x0 = std::min<int32_t>(raw_x0, this->new_x0_);
        raw_y0 = std::min<int32_t>(raw_y0, this->new_y0_);
        raw_x1 = std::max<int32_t>(raw_x1, this->new_x1_);
        raw_y1 = std::max<int32_t>(raw_y1, this->new_y1_);
      }
      dirty_x0 = std::max<int32_t>(0, raw_x0);
      dirty_y0 = std::max<int32_t>(0, raw_y0);
      dirty_x1 = std::min<int32_t>(screen_x1, raw_x1);
      dirty_y1 = std::min<int32_t>(screen_y1, raw_y1);
    } else if (this->have_new_box_) {
      dirty_x0 = std::max<int32_t>(0, this->new_x0_);
      dirty_y0 = std::max<int32_t>(0, this->new_y0_);
      dirty_x1 = std::min<int32_t>(screen_x1, this->new_x1_);
      dirty_y1 = std::min<int32_t>(screen_y1, this->new_y1_);
    }
'''
if old not in s:
    raise SystemExit('target end_frame bounds block not found')
s = s.replace(old, new, 1)
p.write_text(s)
