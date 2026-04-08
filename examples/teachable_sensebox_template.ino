#include "esp_camera.h"
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h> // http://librarymanager/All#Adafruit_GFX_Library
#include <Adafruit_SSD1306.h> // http://librarymanager/All#Adafruit_SSD1306
#include <TensorFlowLite.h>
#include "tensorflow/lite/micro/kernels/micro_ops.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"



// Globals, used for compatibility with Arduino-style sketches.
namespace {
  const tflite::Model* model = nullptr;
  tflite::MicroInterpreter* interpreter = nullptr;
  TfLiteTensor* model_input = nullptr;
  int input_length;

  // Create an area of memory to use for input, output, and intermediate arrays.
  // The size of this will depend on the model you're using, and may need to be
  // determined by experimentation.
  constexpr int kTensorArenaSize = 136 * 1024 ;
  uint8_t tensor_arena[kTensorArenaSize];
}


#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SENSOR_WIDTH 8
#define SENSOR_HEIGHT 8
#define DISPLAY_WIDTH 64  // Expanded display width
#define DISPLAY_HEIGHT 64
#define SCALE_FACTOR 8    // Scale 8x8 to 64x64
#define OLED_RESET -1

#define PIN_QWIIC_SDA 2
#define PIN_QWIIC_SCL 1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const uint8_t FRAME_HEADER[] = {0x46, 0x52, 0x41, 0x4D};

void setup() {

  Serial.begin(115200);
  Wire.begin(PIN_QWIIC_SDA,PIN_QWIIC_SCL);

  // Camera config
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_GRAYSCALE;
  config.frame_size = FRAMESIZE_96X96; // Small grayscale image
  config.jpeg_quality = 12;
  config.fb_count = 1;
  config.fb_location = CAMERA_FB_IN_DRAM;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  // Init OLED
  display.begin(SSD1306_SWITCHCAPVCC, 0x3D);
  display.setRotation(2);
  display.display();
  delay(100);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("Initializing...");
  display.display();

  // Initialize camera
  Serial.println("Init camera...");
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x", err);
    display.println("Camera init failed!");
    display.display();
  }

  // Tensorflow
  // Map the model into a usable data structure. This doesn't involve any
  // copying or parsing, it's a very lightweight operation.
  model = tflite::GetModel(g_person_detect_model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    Serial.printf("Model provided is schema version %d not equal "
                         "to supported version %d.",
                         model->version(), TFLITE_SCHEMA_VERSION);
    return;
  }

  // This imports all operations, which is more intensive, than just importing the ones we need.
  // If we ever run out of storage with a model, we can check here to free some space
  static tflite::AllOpsResolver resolver;

  // Build an interpreter to run the model with.
  static tflite::MicroInterpreter static_interpreter(
      model, resolver, tensor_arena, kTensorArenaSize);
  interpreter = &static_interpreter;

  // Allocate memory from the tensor_arena for the model's tensors.
  interpreter->AllocateTensors();
  // Obtain pointer to the model's input tensor.
  model_input = interpreter->input(0);

  Serial.printf("model_input->dims->size: %i \n", model_input->dims->size);
  Serial.printf("model_input->dims->data[0]: %i \n", model_input->dims->data[0]);
  Serial.printf("model_input->dims->data[1]: %i \n", model_input->dims->data[1]);
  Serial.printf("model_input->dims->data[2]: %i \n", model_input->dims->data[2]);
  Serial.printf("model_input->dims->data[3]: %i \n", model_input->dims->data[3]);
  Serial.printf("model_input->type: %i \n", model_input->type);
  if ((model_input->dims->size != 4) || (model_input->dims->data[1] != 96) ||
      (model_input->dims->data[2] != 96) || 
      (model_input->type != kTfLiteInt8)) {
    Serial.println(model_input->dims->size);
    Serial.println(model_input->dims->data[1]);
    Serial.println(model_input->dims->data[2]);
    Serial.println(model_input->type);
    Serial.println("Bad input tensor parameters in model");
    return;
  }

  input_length = model_input->bytes;
  Serial.printf("input_length: %i \n", input_length);
}

void feedImageToModel(camera_fb_t* fb, int8_t* model_input_data) {
  const int image_width = fb->width;
  const int image_height = fb->height;
  const int channels = 1;  // Grayscale
  uint8_t* src = fb->buf;
  int image_size = image_width * image_height * channels;
  for (int i = 0; i < image_size; i++) {
    int16_t val = static_cast<int16_t>(src[i]) - 128;  // signed offset
    if (val > 127) val = 127;
    if (val < -128) val = -128;
    model_input_data[i] = static_cast<int8_t>(val);
  }
}

void drawImage(camera_fb_t* fb) {

  const int srcWidth = 96;
  const int srcHeight = 96;
  const int targetHeight = 64;
  const int targetWidth = 64;
  const int xOffset = 0; // (SCREEN_WIDTH - targetWidth) / 2;  // 32

  // Simple nearest-neighbor downscaling from 96x96 -> 64x64
  for (int y = 0; y < targetHeight; y++) {
    for (int x = 0; x < targetWidth; x++) {
      // Map target (x, y) to source (sx, sy)
      int sx = x * srcWidth / targetWidth;
      int sy = y * srcHeight / targetHeight;
      uint8_t pixel = fb->buf[sy * srcWidth + sx];

      if (pixel > 128) {
        display.drawPixel(x + xOffset, y, WHITE);
      } else {
        display.drawPixel(x + xOffset, y, BLACK);
      }
    }
  }
}

void drawClassification(float class1Percentage, float class2Percentage, float class3Percentage) {
  display.setTextSize(1);
  display.setTextColor(WHITE, BLACK);
  
  // Class 1
  display.setCursor(80, 0);
  display.print((String)(kCategoryLabels[0]) + ":");
  int barWidth1 = (int)(class1Percentage * 48); // Scale to max ~48 pixels
  display.fillRect(80, 8, barWidth1, 6, WHITE);
  display.drawRect(80, 8, 48, 6, WHITE); // Border for full bar area
  
  if (kCategoryCount > 1) {
    // Class 2
    display.setCursor(80, 19);
    display.println((String)(kCategoryLabels[1]) + ":");
    int barWidth2 = (int)(class2Percentage * 48);
    display.fillRect(80, 27, barWidth2, 6, WHITE);
    display.drawRect(80, 27, 48, 6, WHITE);
  }
  
  if (kCategoryCount > 2) {
    // Class 3
    display.setCursor(80, 38);
    display.println((String)(kCategoryLabels[2]) + ":");
    int barWidth3 = (int)(class3Percentage * 48);
    display.fillRect(80, 46, barWidth3, 6, WHITE);
    display.drawRect(80, 46, 48, 6, WHITE);
  }
}

float confidenceToFloat(int8_t score) {
  // Map from range [-127, 128] to [0, 1]
  return (score + 127.0) / 255.0;
}

void loop() {
  float class1Percentage = -1;
  float class2Percentage = -1;
  float class3Percentage = -1;
  camera_fb_t *fb = esp_camera_fb_get();
  feedImageToModel(fb, model_input->data.int8);
  // Serial.println(7);
  // Run inference, and report any error.
  TfLiteStatus invoke_status = interpreter->Invoke();
  // Serial.println(8);
  if (invoke_status == kTfLiteOk)
  {

      TfLiteTensor* output = interpreter->output(0);

      // Raw logits
      int8_t class1_logit = output->data.int8[0];
      int8_t class2_logit = output->data.int8[1];
      int8_t class3_logit = output->data.int8[2];

      // const float *prediction_scores = interpreter->output(0)->data.f;
      class1Percentage = confidenceToFloat(class1_logit);
      class2Percentage = confidenceToFloat(class2_logit);
      class3Percentage = confidenceToFloat(class3_logit);
  }
  // Serial.println(9);

  display.clearDisplay();
  drawClassification(class1Percentage, class2Percentage, class3Percentage);
  drawImage(fb);
  display.display();

  Serial.write(FRAME_HEADER, 4);
  Serial.write(fb->buf, 96*96);

  esp_camera_fb_return(fb);
  // delay(100);
  // Serial.println(10);
}