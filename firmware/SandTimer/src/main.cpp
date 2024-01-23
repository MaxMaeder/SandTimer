#include <Arduino.h>
#include <Encoder.h>
#include <Adafruit_MPU6050.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Fonts/TomThumb.h>
#include <Adafruit_IS31FL3731.h>

enum OpMode {
  SLEEP,
  ADJUST_TIME,
  COUNTDOWN,
};

enum Orientation {
  UP,
  DOWN,
  SIDE_UP,
  SIDE_DOWN
};

enum EncoderChange {
  INC,
  DECM,
  NO
};

const uint8_t ENC_PIN_A = 14;
const uint8_t ENC_PIN_B = 13;
const uint8_t MIN_ENC_CHANGE = 4;

const float_t ACCEL_THRESH = 8.0;

const uint32_t SLEEP_AFTER = 10 * 1000; // millis

const float_t MIN_DUR = .25;
const float_t MAX_DUR = 5;
const float_t DUR_STEP = .25;

const uint8_t SAND_COLORS[] = {10, 20, 30, 40, 50};
const uint8_t INDENT_COLOR = 1;
const uint32_t SAND_UPDATE_AFTER = 20; // millis

Encoder dial_enc(ENC_PIN_A, ENC_PIN_B);
int32_t pre_enc_ticks = 0;

Adafruit_MPU6050 imu;

Adafruit_IS31FL3731_Wing matrix = Adafruit_IS31FL3731_Wing();
GFXcanvas8 canvas(15, 7);

OpMode op_mode = OpMode::ADJUST_TIME;

Orientation pre_ori;
Orientation curr_ori;
uint32_t sleep_last_update = 0;

float_t curr_dur = .5;
uint32_t start_time = 0;

uint32_t last_sand_update = 0;

void update_ori() {
  sensors_event_t a, g, temp;
  imu.getEvent(&a, &g, &temp);

  if (a.acceleration.y > ACCEL_THRESH) {
    curr_ori = Orientation::UP;
  } else if (a.acceleration.y < -ACCEL_THRESH) {
    curr_ori = Orientation::DOWN;
  } else if (a.acceleration.x > ACCEL_THRESH) {
    curr_ori = Orientation::SIDE_DOWN;
  } else if (a.acceleration.x < -ACCEL_THRESH) {
    curr_ori = Orientation::SIDE_UP;
  }
}

bool has_flipped() {
  if (curr_ori != pre_ori) {
    sleep_last_update = millis();
    pre_ori = curr_ori;
    return true;
  }

  return false;
}

bool should_sleep() {
  return millis() - sleep_last_update > SLEEP_AFTER;
}

bool is_facing_up() {
  return curr_ori == Orientation::UP || curr_ori == Orientation::SIDE_UP;
}

bool is_vertical() {
  return curr_ori == Orientation::UP || curr_ori == Orientation::DOWN;
}

bool is_enc_change() {
   return abs(dial_enc.read() - pre_enc_ticks) >= MIN_ENC_CHANGE;
}

EncoderChange get_enc_change() {
  if (!is_enc_change())
    return EncoderChange::NO;

  int32_t curr_ticks = dial_enc.read();
  EncoderChange change = curr_ticks > pre_enc_ticks ? EncoderChange::INC : EncoderChange::DECM;
  pre_enc_ticks = curr_ticks;

  return change;
}

void update_matrix() {
  matrix.drawGrayscaleBitmap(0, 0, canvas.getBuffer(),
    canvas.width(), canvas.height());
}

void print_dur() {
  int minutes = (int)curr_dur;
  int seconds = (int)((curr_dur - minutes) * 60);

  char time[10];
  sprintf(time, "%01d:%02d", minutes, seconds);

  canvas.setCursor(0, 6);
  canvas.print(time);
}

void draw_indents() {
  canvas.fillTriangle(6, 0, 8, 0, 7, 2, INDENT_COLOR);
  canvas.fillTriangle(6, 6, 8, 6, 7, 4, INDENT_COLOR);
  canvas.drawPixel(6, 5, INDENT_COLOR);
  canvas.drawPixel(8, 5, INDENT_COLOR);
}

void draw_sand(uint8_t x_start, uint8_t x_len, bool reverse) {
  for (int i = 0; i < x_len; i++) {
    for (int j = 0; j < 7; j++) {
      uint8_t color_sd = (millis() / max(35, (i + 1) * 25) + ((i % 2 == 0) ? j : -j)) % 5;

      canvas.drawPixel(x_start + (reverse ? -i : i), j, SAND_COLORS[color_sd]);
    }
  }
}

void setup() {
  Serial.begin(115200);

  if (!imu.begin()) {
    Serial.println("Failed to init IMU");
    while (1)
      delay(10);
  }
  imu.setAccelerometerRange(MPU6050_RANGE_8_G);
  imu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  if (!matrix.begin()) {
    Serial.println("Failed to init LED Matrix");
    while (1)
      delay(10);
  }
  canvas.setFont(&TomThumb);
  canvas.setTextColor(40);

  // Reset orientation state
  update_ori();
  has_flipped();
}

void loop() {
  update_ori();
  canvas.setRotation(is_facing_up() ? 0 : 2);

  switch (op_mode) {
    case OpMode::SLEEP: {
      matrix.clear();

      has_flipped();
      if (!should_sleep() || is_enc_change()) {
        sleep_last_update = millis();
        op_mode = OpMode::ADJUST_TIME;
      }

      break;
    }

    case OpMode::ADJUST_TIME: {
      if (is_enc_change()) {
        sleep_last_update = millis();

        float_t delta = DUR_STEP;
        if (get_enc_change() == EncoderChange::DECM)
          delta *= -1;

        curr_dur = min(MAX_DUR, max(MIN_DUR, curr_dur + delta));
      }

      canvas.fillScreen(0);
      print_dur();
      update_matrix();

      if (has_flipped()) {
        if (is_vertical()) {
          start_time = millis();
          matrix.clear();
          op_mode = OpMode::COUNTDOWN;
        }
        sleep_last_update = millis();
      } else if (should_sleep()) {
        op_mode = OpMode::SLEEP;
      }

      break;
    }

    case OpMode::COUNTDOWN: {
      if (is_enc_change()) {
        sleep_last_update = millis();
        op_mode = OpMode::ADJUST_TIME;
      }

      if (has_flipped()) {
        if (!is_vertical()) {
          sleep_last_update = millis();
          op_mode = OpMode::ADJUST_TIME;
        }

        matrix.clear();
        start_time = millis();
      }

      if (millis() > last_sand_update + SAND_UPDATE_AFTER) {
        last_sand_update = millis();

        float_t percent_done = (float_t)(millis() - start_time) / (curr_dur * 60 * 1000);
        uint8 num_lines = min(7, (int)(7 * percent_done));

        if (percent_done >= 1) {
          canvas.fillScreen(millis() % 1000 > 500 ? 40 : 0);

          if (should_sleep())
            op_mode = OpMode::SLEEP;
        } else {
          canvas.fillScreen(0);
          draw_sand(7, 8 - num_lines, true);
          draw_sand(15 - num_lines, num_lines, false);
          draw_indents();

          sleep_last_update = millis();
        }

        update_matrix();
      }

      break;
    }

    default:
      break;
  }
}