#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <driver/i2s.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <HTTPClient.h>
#include <SD.h>

// INMP441 Microphone Setup
#define I2S_WS 22
#define I2S_SD 21
#define I2S_SCK 26

#define I2S_PORT I2S_NUM_0
#define SAMPLE_RATE (16000) // 44100
#define I2S_SAMPLE_BITS (16)
#define I2S_READ_LEN (16 * 1024)
#define RECORD_TIME (5) // Seconds
#define I2S_CHANNEL_NUM (1)
#define FLASH_RECORD_SIZE (I2S_CHANNEL_NUM * SAMPLE_RATE * I2S_SAMPLE_BITS / 8 * RECORD_TIME)
int playRadio = 0;
File file;
const char audioRecordfile[] = "/recording.wav";
const char audioResponsefile[] = "/voicedby.wav";
const int headerSize = 44;

// MAX98356 Amplifier Setup
#define I2S_DOUT 15
#define I2S_BCLK 12
#define I2S_LRC 13

#define MAX_I2S_NUM I2S_NUM_1
#define MAX_I2S_SAMPLE_RATE (12000)
#define MAX_I2S_SAMPLE_BITS (16)
#define MAX_I2S_READ_LEN (256)

// Wifi
// std::string ssid = "UCInet Mobile Access";
std::string ssid = "user";
std::string pass = "password";

// TTGO Button Pins
#define RIGHT_BUTTON 35
int rightState;            // the current reading from the input pin
int lastRightState = LOW;  // the previous reading from the input pin
unsigned long lastRightDebounceTime = 0;
unsigned long debounceDelay = 50;

// TFT Display
TFT_eSPI tft = TFT_eSPI();

void SPIFFS_init();
void listSPIFFS();
void MAX98357A_install();
void INMP441_install();
void record();
void record_data(uint8_t *d_buf, uint8_t *s_buf, uint32_t len);
void wavHeader(byte *header, int wavSize);

void SPIFFSInit()
{
  if (!SPIFFS.begin(true))
  {
    Serial.println("SPIFFS initialisation failed!");
    while (1)
      yield();
  }

  // format_Spiffs();
  if (SPIFFS.exists(audioRecordfile))
  {
    SPIFFS.remove(audioRecordfile);
  }
  if (SPIFFS.exists(audioResponsefile))
  {
    SPIFFS.remove(audioResponsefile);
  }

  file = SPIFFS.open(audioRecordfile, FILE_WRITE);
  if (!file)
  {
    Serial.println("File is not available!");
  }

  byte header[headerSize];
  wavHeader(header, FLASH_RECORD_SIZE);

  file.write(header, headerSize);
  listSPIFFS();
}

void listSPIFFS(void)
{
  // DEBUG
  printSpaceInfo();
  Serial.println(F("\r\nListing SPIFFS files:"));
  static const char line[] PROGMEM = "=================================================";

  Serial.println(FPSTR(line));
  Serial.println(F("  File name                              Size"));
  Serial.println(FPSTR(line));

  fs::File root = SPIFFS.open("/");
  if (!root)
  {
    Serial.println(F("Failed to open directory"));
    return;
  }
  if (!root.isDirectory())
  {
    Serial.println(F("Not a directory"));
    return;
  }

  fs::File file = root.openNextFile();
  while (file)
  {

    if (file.isDirectory())
    {
      Serial.print("DIR : ");
      String fileName = file.name();
      Serial.print(fileName);
    }
    else
    {
      String fileName = file.name();
      Serial.print("  " + fileName);
      // File path can be 31 characters maximum in SPIFFS
      int spaces = 33 - fileName.length(); // Tabulate nicely
      if (spaces < 1)
        spaces = 1;
      while (spaces--)
        Serial.print(" ");
      String fileSize = (String)file.size();
      spaces = 10 - fileSize.length(); // Tabulate nicely
      if (spaces < 1)
        spaces = 1;
      while (spaces--)
        Serial.print(" ");
      Serial.println(fileSize + " bytes");
    }

    file = root.openNextFile();
  }

  Serial.println(FPSTR(line));
  Serial.println();
  TickDelay(1000);
}

void MAX98357A_install()
{
  const i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = MAX_I2S_SAMPLE_RATE,
      .bits_per_sample = i2s_bits_per_sample_t(MAX_I2S_SAMPLE_BITS),
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = MAX_I2S_READ_LEN,
      .use_apll = false,
      .tx_desc_auto_clear = true,
      .fixed_mclk = 0
  };

  const i2s_pin_config_t pin_config = {
      .bck_io_num = I2S_BCLK,
      .ws_io_num = I2S_LRC,
      .data_out_num = I2S_DOUT,
      .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(MAX_I2S_NUM, &i2s_config, 0, NULL);
  i2s_set_pin(MAX_I2S_NUM, &pin_config);
  i2s_zero_dma_buffer(MAX_I2S_NUM);
}

void INMP441_install() {
  const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = i2s_bits_per_sample_t(I2S_SAMPLE_BITS),
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, // I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S), // i2s_comm_format_t(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 64,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
 
  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);

  // Set I2S pin configuration
  const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD
  };
 
  i2s_set_pin(I2S_PORT, &pin_config);
  i2s_set_clk(I2S_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_STEREO);
}

void record_data(uint8_t *d_buf, uint8_t *s_buf, uint32_t len)
{
  uint32_t j = 0;
  uint32_t dac_value = 0;
  for (int i = 0; i < len; i += 2)
  {
    dac_value = ((((uint16_t)(s_buf[i + 1] & 0xf) << 8) | ((s_buf[i + 0]))));
    d_buf[j++] = 0;
    d_buf[j++] = dac_value * 256 / 2048;
  }
}

void record() {
  int read_len = I2S_READ_LEN;
  int write_len = 0;
  size_t bytes_read;

  char *read_buf = (char *) calloc(read_len, sizeof(char));
  uint8_t *write_buf = (uint8_t *) calloc(read_len, sizeof(char));

  i2s_read(I2S_PORT, (void *)read_buf, read_len, &bytes_read, portMAX_DELAY);
  // i2s_read(I2S_PORT, (void *)read_buf, read_len, &bytes_read, portMAX_DELAY);

  // digitalWrite(isAudioRecording, HIGH);
  Serial.println("RECORDING!");
  while (write_len < FLASH_RECORD_SIZE) {
    i2s_read(I2S_PORT, (void *)read_buf, read_len, &bytes_read, portMAX_DELAY);

    record_data(write_buf, (uint8_t *)read_buf, read_len);
    file.write((const byte *)write_buf, read_len);
    write_len += read_len;
  }

  file.close();
  free(read_buf);
  read_buf = NULL;
  free(write_buf);
  write_buf = NULL;
}

void wavHeader(byte *header, int wavSize)
{
  header[0] = 'R';
  header[1] = 'I';
  header[2] = 'F';
  header[3] = 'F';
  unsigned int fileSize = wavSize + headerSize - 8;
  header[4] = (byte)(fileSize & 0xFF);
  header[5] = (byte)((fileSize >> 8) & 0xFF);
  header[6] = (byte)((fileSize >> 16) & 0xFF);
  header[7] = (byte)((fileSize >> 24) & 0xFF);
  header[8] = 'W';
  header[9] = 'A';
  header[10] = 'V';
  header[11] = 'E';
  header[12] = 'f';
  header[13] = 'm';
  header[14] = 't';
  header[15] = ' ';
  header[16] = 0x10;
  header[17] = 0x00;
  header[18] = 0x00;
  header[19] = 0x00;
  header[20] = 0x01;
  header[21] = 0x00;
  header[22] = 0x01;
  header[23] = 0x00;
  header[24] = 0x80;
  header[25] = 0x3E;
  header[26] = 0x00;
  header[27] = 0x00;
  header[28] = 0x00;
  header[29] = 0x7D;
  header[30] = 0x01;
  header[31] = 0x00;
  header[32] = 0x02;
  header[33] = 0x00;
  header[34] = 0x10;
  header[35] = 0x00;
  header[36] = 'd';
  header[37] = 'a';
  header[38] = 't';
  header[39] = 'a';
  header[40] = (byte)(wavSize & 0xFF);
  header[41] = (byte)((wavSize >> 8) & 0xFF);
  header[42] = (byte)((wavSize >> 16) & 0xFF);
  header[43] = (byte)((wavSize >> 24) & 0xFF);
}

void setup() {
  Serial.begin(9600);
  Serial.println(" ");
  delay(1000);

  // Set TTGO buttons
  pinMode(RIGHT_BUTTON, INPUT_PULLUP);

  // Setup MAX98367 Amplifier
  MAX98357A_install();

  // Setup INMP441 Microphone
  INMP441_install();

  // Setup Wifi
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  // WiFi Connected
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("");
  // audio.connecttospeech("The WiFi device is connected.", "zh-CN");

  // Setup Display
  tft.init();
  tft.setRotation(1);
}

void loop() {
  // audio.loop();
  tft.fillScreen(TFT_WHITE);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setCursor(90, 25, 2);
  tft.println("Gago Translate!");

  if (millis() >= 10000 && playRadio) {
    playRadio = 0;
    Serial.println("Playing radio...");
    // audio.connecttohost("https://stationplaylist.com:7104/listen.aac");
    // audio.connecttohost("https://ice64.securenetsystems.net/LFTM");
    // audio.connecttohost("https://lhttp.qtfm.cn/live/1926/64k.mp3");
  }

  int right = digitalRead(RIGHT_BUTTON);

  if (right != lastRightState) {
    lastRightDebounceTime = millis();
  }

  if ((millis() - lastRightDebounceTime) > debounceDelay) {
    if (right != rightState) {
      rightState = right;

      if (rightState == HIGH) {
        Serial.println("Right pressed");
        // Serial.printf("Volume: %d\n", VOLUME);
        // tft.printf("Volume: %d", VOLUME);
      }
    }
  }
  lastRightState = right;
}