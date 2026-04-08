#include "esp_camera.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "driver/rtc_io.h"
//#include "camera_pins.h"

#define PIN_QWIIC_SDA 2
#define PIN_QWIIC_SCL 1

// TASK 1: Find the screen height and width in px from the display by searching for the senseBox OLED Display on the Internet!
// Replace <HEIGHT_IN_PX> and <WIDTH_IN_PX> with the appropriate values.
#define SCREEN_WIDTH <HEIGHT_IN_PX>
#define SCREEN_HEIGHT <WIDTH_IN_PX>
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

void setup() {
  Wire.begin(PIN_QWIIC_SDA,PIN_QWIIC_SCL);
  Serial.begin(115200);
  delay(100);

  // init OLED Display 
  // TASK 2: Find the I2C Address of the Display. Hint: It is printed on the display breakout board.
  // Replace <I2C ADDRESS> with the appropriate value.
  if(!display.begin(SSD1306_SWITCHCAPVCC, <I2C ADDRESS>)) { 
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  display.setRotation(2);
  display.display();
  delay(100);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("Initializing...");
  display.display();

  // Camera config
  // Check the schematics and/or pin_configuration file and enter pin variable names or pin numbers
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = <FILL_ME_IN>;
  config.pin_d1 = <FILL_ME_IN>;
  config.pin_d2 = <FILL_ME_IN>;
  config.pin_d3 = <FILL_ME_IN>;
  config.pin_d4 = <FILL_ME_IN>;
  config.pin_d5 = <FILL_ME_IN>;
  config.pin_d6 = <FILL_ME_IN>;
  config.pin_d7 = <FILL_ME_IN>;
  config.pin_xclk = <FILL_ME_IN>;
  config.pin_pclk = <FILL_ME_IN>;
  config.pin_vsync = <FILL_ME_IN>;
  config.pin_href = <FILL_ME_IN>;
  config.pin_sccb_sda = <FILL_ME_IN>;
  config.pin_sccb_scl = <FILL_ME_IN>;
  config.pin_pwdn = <FILL_ME_IN>;
  config.pin_reset = <FILL_ME_IN>;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_GRAYSCALE;
  config.frame_size = FRAMESIZE_96X96; // Small grayscale image
  config.jpeg_quality = 12;
  config.fb_count = 1;
  config.fb_location = CAMERA_FB_IN_DRAM;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  // Initialize camera
  Serial.println("Init camera...");
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x", err);
    display.println("Camera init failed!");
    display.display();
    return;
  }
}

void loop() {
  // Capture image
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Capture failed");
    display.println("Capture failed!");
    display.display();
    return;
  }

  // Prepare to draw
  display.clearDisplay();

  const int srcWidth = 96;
  const int srcHeight = 96;
  const int targetHeight = 64;
  const int targetWidth = 64;
  const int xOffset = (SCREEN_WIDTH - targetWidth) / 2;  // 32

  // Simple nearest-neighbor downscaling from 96x96 -> 64x64
  for (int y = 0; y < targetHeight; y++) {
    for (int x = 0; x < targetWidth; x++) {
      // Map target (x, y) to source (sx, sy)
      int sx = x * srcWidth / targetWidth;
      int sy = y * srcHeight / targetHeight;
      uint8_t pixel = fb->buf[sy * srcWidth + sx];

      // TASK 2: Check the pixel value and print black or white pixels based on its color (greyscale) value. Use the value 128 as threshold.
      // Hint: display.drawPixel(x, y, <color>) allows you to draw single pixels; <color> can either be BLACK or WHITE.
      // Place your code here.
    }
  }

  display.display();
  esp_camera_fb_return(fb);
  delay(10);
}