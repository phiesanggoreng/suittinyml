// Suit TinyML: tiga tombol pilihan -> model TinyML -> game -> OLED SH1107.
// Setiap tombol langsung memainkan satu ronde; tidak ada lagi pemetaan jari.
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <esp_system.h>
#include <Chirale_TensorFlowLite.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/micro/micro_mutable_op_resolver.h>
#include <tensorflow/lite/schema/schema_generated.h>
#include "model_data.h"
#include "game_logic.h"

constexpr int BUTTON_PINS[] = {25, 26, 27};  // BATU, GUNTING, KERTAS
constexpr int BUZZER_PIN = 18;
constexpr uint32_t RESULT_MS = 2500;
constexpr uint8_t SCREEN_WIDTH = 128;
constexpr uint8_t SCREEN_HEIGHT = 128;
constexpr uint8_t OLED_ADDRESS = 0x3C;

Adafruit_SH1107 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1, 1000000, 100000);
bool oledReady = false;
bool modelReady = false;
alignas(16) uint8_t tensorArena[16 * 1024];
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* inputTensor = nullptr;
TfLiteTensor* outputTensor = nullptr;
DebouncedButton choiceButtons[3];

enum Screen { LIVE, SHOW_RESULT, SHOW_INVALID, FATAL_ERROR };
Screen screen = LIVE;
uint32_t screenSince = 0;
int livePose = TIDAK_VALID;
float liveScore = 0;
int playerChoice = TIDAK_VALID;
int cpuChoice = TIDAK_VALID;
Result lastResult = SERI;
unsigned int wins = 0, losses = 0, draws = 0;

void clearDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
}

void drawHeader(const char* title) {
  display.fillRoundRect(0, 0, 128, 16, 3, SH110X_WHITE);
  display.setTextColor(SH110X_BLACK);
  display.setCursor(5, 4);
  display.print(title);
  display.setTextColor(SH110X_WHITE);
  display.drawLine(0, 19, 127, 19, SH110X_WHITE);
}

void drawScore() {
  display.drawLine(0, 111, 127, 111, SH110X_WHITE);
  display.setCursor(5, 116);
  display.printf("W:%u   L:%u   D:%u", wins, losses, draws);
}

void drawLiveScreen() {
  clearDisplay();
  drawHeader("SUIT TINYML");
  display.setTextSize(2);
  display.setCursor(5, 28);
  display.print("PILIH");
  display.setTextSize(1);
  display.setCursor(8, 57);
  display.print("1  BATU");
  display.setCursor(8, 70);
  display.print("2  GUNTING");
  display.setCursor(8, 83);
  display.print("3  KERTAS");
  display.setCursor(8, 98);
  display.print("TEKAN SATU TOMBOL");
  drawScore();
  display.display();
}

void drawResultScreen() {
  clearDisplay();
  drawHeader("HASIL RONDE");
  display.setCursor(5, 27);
  display.print("KAMU : "); display.println(POSE_NAMES[playerChoice]);
  display.setCursor(5, 40);
  display.print("CPU  : "); display.println(POSE_NAMES[cpuChoice]);
  display.drawRoundRect(2, 55, 124, 43, 4, SH110X_WHITE);
  display.setTextSize(2);
  display.setCursor(8, 68);
  display.print(lastResult == MENANG ? "MENANG!" : lastResult == KALAH ? "KALAH" : "SERI");
  display.setTextSize(1);
  drawScore();
  display.display();
}

void drawInvalidScreen() {
  clearDisplay();
  drawHeader("PILIHAN SALAH");
  display.setCursor(5, 30);
  display.println("TEKAN SATU TOMBOL");
  display.setCursor(5, 45);
  display.println("BATU / GUNTING /");
  display.setCursor(5, 58);
  display.println("KERTAS SAJA");
  display.setCursor(5, 83);
  display.println("RONDE TIDAK DIHITUNG");
  drawScore();
  display.display();
}

void render() {
  if (!oledReady || screen == FATAL_ERROR) return;
  if (screen == SHOW_RESULT) drawResultScreen();
  else if (screen == SHOW_INVALID) drawInvalidScreen();
  else drawLiveScreen();
}

void fail(const char* message) {
  modelReady = false;
  screen = FATAL_ERROR;
  Serial.print("ERROR: "); Serial.println(message);
  if (oledReady) {
    clearDisplay();
    drawHeader("GAGAL MEMULAI");
    display.setCursor(5, 30); display.println(message);
    display.setCursor(5, 48); display.println("Periksa model/kabel");
    display.display();
  }
}

bool initializeModel() {
  const tflite::Model* model = tflite::GetModel(kSuitModel);
  if (model->version() != TFLITE_SCHEMA_VERSION) return false;
  static tflite::MicroMutableOpResolver<2> resolver;
  if (resolver.AddFullyConnected() != kTfLiteOk ||
      resolver.AddSoftmax() != kTfLiteOk) return false;
  static tflite::MicroInterpreter instance(model, resolver, tensorArena, sizeof(tensorArena));
  interpreter = &instance;
  if (interpreter->AllocateTensors() != kTfLiteOk) return false;
  inputTensor = interpreter->input(0);
  outputTensor = interpreter->output(0);
  return inputTensor && outputTensor &&
    inputTensor->type == kTfLiteFloat32 && outputTensor->type == kTfLiteFloat32 &&
    inputTensor->bytes == 3 * sizeof(float) && outputTensor->bytes == 4 * sizeof(float);
}

// Fitur ini sama dengan training/train.py dan model_data.h.
void encodeChoice(int choice) {
  const float features[3][3] = {
    {0.0f, 0.0f, 0.0f},  // BATU
    {1.0f, 1.0f, 0.0f},  // GUNTING
    {1.0f, 1.0f, 1.0f}   // KERTAS
  };
  for (int i = 0; i < 3; ++i) inputTensor->data.f[i] = features[choice][i];
}

bool predictChoice(int choice) {
  if (choice < 0 || choice > 2) return false;
  encodeChoice(choice);
  if (interpreter->Invoke() != kTfLiteOk) {
    fail("Inference gagal");
    return false;
  }
  livePose = selectPose(outputTensor->data.f, &liveScore);
  return true;
}

void playRound(int requestedChoice, uint32_t now) {
  screenSince = now;
  if (!predictChoice(requestedChoice) || livePose != requestedChoice) {
    screen = SHOW_INVALID;
    tone(BUZZER_PIN, 220, 120);
    Serial.println("PILIHAN DITOLAK: model tidak mengenali tombol.");
    render();
    return;
  }
  playerChoice = requestedChoice;
  cpuChoice = random(0, 3);
  lastResult = decideWinner(playerChoice, cpuChoice);
  if (lastResult == MENANG) ++wins;
  else if (lastResult == KALAH) ++losses;
  else ++draws;
  screen = SHOW_RESULT;
  tone(BUZZER_PIN, lastResult == MENANG ? 1200 : lastResult == KALAH ? 330 : 660, 140);
  Serial.printf("Kamu=%s CPU=%s Hasil=%s | W=%u L=%u D=%u\n",
      POSE_NAMES[playerChoice], POSE_NAMES[cpuChoice],
      lastResult == MENANG ? "MENANG" : lastResult == KALAH ? "KALAH" : "SERI",
      wins, losses, draws);
  render();
}

void handleSerial() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == 'r' || ch == 'R') {
      wins = losses = draws = 0;
      screen = LIVE;
      Serial.println("Skor direset.");
      render();
    }
  }
}

void setup() {
  Serial.begin(115200);
  for (int pin : BUTTON_PINS) pinMode(pin, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  randomSeed(esp_random());

  Wire.begin(21, 22);
  oledReady = display.begin(OLED_ADDRESS, true);
  if (!oledReady) Serial.println("OLED SH1107 gagal.");

  if (!initializeModel()) { fail("Model/arena gagal"); return; }
  modelReady = true;
  Serial.println("SUIT TinyML siap. Tekan tombol BATU, GUNTING, atau KERTAS.");
  Serial.println("Serial: r=reset skor.");
  Serial.printf("Model %u bytes; arena tersedia %u bytes\n", kSuitModelLen,
                static_cast<unsigned int>(sizeof(tensorArena)));
  render();
}

void loop() {
  if (!modelReady) { delay(20); return; }
  uint32_t now = millis();
  handleSerial();
  int pressedChoice = -1;
  for (int i = 0; i < 3; ++i) {
    if (choiceButtons[i].update(digitalRead(BUTTON_PINS[i]) == LOW, now)) {
      if (pressedChoice != -1) pressedChoice = TIDAK_VALID;
      else pressedChoice = i;
    }
  }
  if (screen == SHOW_RESULT || screen == SHOW_INVALID) {
    if (uint32_t(now - screenSince) >= RESULT_MS) {
      screen = LIVE;
      render();
    }
  } else if (pressedChoice >= 0) {
    playRound(pressedChoice, now);
  }
  delay(2);
}
