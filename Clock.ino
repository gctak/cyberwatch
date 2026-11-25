// CyberWatch - Copyright (C) 2026 Giovanna Cardoso Takingami
// Licensed under the GNU GPL v3.0. See the LICENSE file for details.

#include <Arduino.h>
#include "Arduino_GFX_Library.h"
#include "pin_config.h"
#include <Wire.h>
#include "HWCDC.h"
#include "SensorPCF85063.hpp"
#include "secrets.h"
#include <WiFi.h>
#include "Arduino_DriveBus_Library.h"
#include "XPowersLib.h"
#include "ESP_I2S.h"
#include "esp_check.h"
#include "es8311.h"
#include "config.h"
#if SERVER_MONITOR_ENABLED
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#endif

HWCDC USBSerial;
SensorPCF85063 rtc;
XPowersPMU power;

// Cria a comunicação com a tela (QSPI, pinos do pin_config.h)
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  LCD_CS /* CS */, LCD_SCLK /* SCK */, LCD_SDIO0 /* SDIO0 */, LCD_SDIO1 /* SDIO1 */,
  LCD_SDIO2 /* SDIO2 */, LCD_SDIO3 /* SDIO3 */);

// Traduz os comandos de desenho pro chip da tela (CO5300)
Arduino_GFX *gfx = new Arduino_CO5300(bus, LCD_RESET /* RST */,
                                      0 /* rotation */, LCD_WIDTH, LCD_HEIGHT,
                                      22 /* col_offset1 */,
                                      0 /* row_offset1 */,
                                      0 /* col_offset2 */,
                                      0 /* row_offset2 */);

// Cria o "fio" I2C para conversar com o chip de toque (o mesmo fio do RTC)
std::shared_ptr<Arduino_IIC_DriveBus> IIC_Bus = std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);
// Avisa o compilador que essa função existe (ela é escrita mais abaixo)
void Arduino_IIC_Touch_Interrupt(void);
// Cria o chip de toque (FT3168): por qual fio ele conversa, por qual pino avisa do toque e qual função chamar quando isso acontecer
std::unique_ptr<Arduino_IIC> FT3168(new Arduino_FT3x68(IIC_Bus, FT3168_DEVICE_ADDRESS, DRIVEBUS_DEFAULT_VALUE, TP_INT, Arduino_IIC_Touch_Interrupt));

// Chamada automaticamente quando a tela é tocada -> levanta a bandeirinha
void Arduino_IIC_Touch_Interrupt(void) {
  if (FT3168) {
    FT3168->IIC_Interrupt_Flag = true;
  }
}

// Áudio (ES8311 + amplificador)
#define EXAMPLE_SAMPLE_RATE 16000
#define EXAMPLE_VOICE_VOLUME 60                  // 0 - 100
#define EXAMPLE_MIC_GAIN (es8311_mic_gain_t)(3)  // 0 - 7
#define I2C_NUM 0

I2SClass i2s;
const char *TAG = "cyberwatch_audio";
int16_t powerUp[9600];  // som de power-up (0,3 s em estéreo)

// Configurações do relógio
const char *WEEKDAYS[] = { "Dom", "Seg", "Ter", "Qua", "Qui", "Sex", "Sab" };
const int BOLT_START[] = { 4, 3, 2, 1, 0, 2, 1, 0 };
const int BOLT_LEN[] = { 2, 2, 2, 5, 5, 2, 2, 2 };
#if SERVER_MONITOR_ENABLED
const int PAGE_COUNT = 2;
#else
const int PAGE_COUNT = 1;
#endif

// Variáveis do relógio
uint16_t colorGreen = gfx->color565(0x30, 0xD1, 0x58);
uint16_t colorYellow = gfx->color565(0xFF, 0xD6, 0x0A);
uint16_t colorRed = gfx->color565(0xCD, 0x2C, 0x25);
unsigned long lastMillis = 0;               // momento da última atualização da tela
unsigned long lastWarningMillis = 0;        // momento do último aviso
unsigned long lastTouchMillis = 0;          // momento do último toque -> detecta o duplo
unsigned long lastScreenRefresh = 0;        // momento que a tela foi acesa
bool screenOn = true;                       // tela está ligada?
int lastDisplayedDay = 0;                   // último dia exibido na tela
int lastPercent = 101;                      // última porcentagem de bateria exibida
bool lastChargingState = false;             // último estado de carregamento registrado para usar na bateria
bool isTouching = false;                    // há um toque ativo na tela?
int xStart = 0;                             // posição X  inicial do deslize
int xEnd = 0;                               // posição X final do deslize
int lastSyncDay = 0;                        // último dia que a data e o horário foi sincronizado pelo NTP server
unsigned long lastSyncCheck = 0;            // momento da última checagem do horário pra sincronização agendada
bool wasCharging = false;                   // estava carregando?
unsigned long lastChargeCheck = 0;          // momento da última checagem do cabo
int currentPage = 0;                        // página atualmente exibida na tela
unsigned long lastBatteryUpdateMillis = 0;  // momento da última atualização da bateria


void show_date() {
  gfx->setTextColor(gfx->color565(0x60, 0x60, 0x66), BLACK);
  gfx->setTextSize(3);
  gfx->setCursor(79, 187);

  RTC_DateTime datetime = rtc.getDateTime();  // pega a data
  gfx->printf("%s %02d/%02d/%d", WEEKDAYS[datetime.getWeek()], datetime.getDay(), datetime.getMonth(), datetime.getYear());
}

void show_time() {
  gfx->setTextColor(gfx->color565(0xD8, 0xD8, 0xDD), BLACK);
  gfx->setTextSize(8);
  gfx->setCursor(61, 251);

  RTC_DateTime datetime = rtc.getDateTime();  //pega as horas
  gfx->printf("%02d:%02d", datetime.getHour(), datetime.getMinute());

  gfx->setTextSize(4);
  gfx->setCursor(301, 283);
  gfx->printf("%02d", datetime.getSecond());
}

void fill_even_circle(int cx, int cy, int r, uint16_t color) {
  for (int d = -r; d <= r; d += 2) {
    int w = sqrt((r * r) - (d * d));
    gfx->fillRect(cx - w, cy + d, 2 * w, 2, color);
  }
}

void show_status_bar() {
  gfx->setTextColor(colorRed, BLACK);
  gfx->setTextSize(2);
  gfx->setCursor(60, 40);
  gfx->print("CYBERWATCH");
  fill_even_circle(46, 48, 6, colorRed);
}

void show_battery(int percent, bool chargingState) {
  int fillWidth = (32 * percent / 100) & ~1;

  gfx->fillRect(314, 40, 32, 16, gfx->color565(0x2C, 0x2C, 0x2E));
  gfx->fillRect(346, 44, 4, 8, gfx->color565(0x2C, 0x2C, 0x2E));

  if (chargingState) {
    gfx->fillRect(314, 40, fillWidth, 16, colorGreen);
    for (int line = 0; line < 8; line++) {
      gfx->fillRect(324 + BOLT_START[line] * 2, 40 + line * 2, BOLT_LEN[line] * 2, 2, gfx->color565(0xD8, 0xD8, 0xDD));
    }
  } else if (percent <= 20) {
    gfx->fillRect(314, 40, fillWidth, 16, colorYellow);
  } else {
    gfx->fillRect(314, 40, fillWidth, 16, gfx->color565(0x8E, 0x8E, 0x93));
  }
}

int read_touch_x() {
  return FT3168->IIC_Read_Device_Value(FT3168->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_X);
}

bool connect_wifi(const char *ssid, const char *password) {
  int attempts = 0;
  WiFi.disconnect(true);  // 1º: desiste da tentativa anterior
  delay(100);
  WiFi.begin(ssid, password);  // 2º: começa a nova, do zero
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {

    if (currentPage == 0) {
      show_time();
    }

    delay(500);
    attempts += 1;
  }
  return WiFi.status() == WL_CONNECTED;
}

bool connect_any() {
  WiFi.setAutoReconnect(false);  // "não insista sozinho"
  bool connected = connect_wifi(HOME_SSID, HOME_PASSWORD) || connect_wifi(HOTSPOT_SSID, HOTSPOT_PASSWORD);
  if (!connected) {
    USBSerial.println("Wi-Fi connection failed");
    USBSerial.printf("Status:%d\n", WiFi.status());
  } else {
    USBSerial.printf("(%lu) Connected to: %s\n", millis(), WiFi.SSID().c_str());
  }
  return connected;
}

void sync_time() {
  struct timeval zero = { 0, 0 };         // "1º de janeiro de 1970, 0h"
  settimeofday(&zero, NULL);              // zera o relógio interno
  configTime(-10800, 0, "pool.ntp.org");  // configura de onde pegar a hora e o fuso
  struct tm timeinfo;                     // cria uma "caixa" pra guardar a hora
  int ntp_attempts = 0;
  while (getLocalTime(&timeinfo, 1000) != true && ntp_attempts < 10) {
    show_time();
    delay(500);
    ntp_attempts += 1;
  }
  if (ntp_attempts < 10) {
    rtc.setDateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    USBSerial.printf("(%lu) Time syncronized successfuly", millis());
  } else {
    USBSerial.println("Failed to synchronize time with NTP server");
  }
}

esp_err_t es8311_codec_init(void) {
  es8311_handle_t es_handle = es8311_create(I2C_NUM, ES8311_ADDRRES_0);
  ESP_RETURN_ON_FALSE(es_handle, ESP_FAIL, TAG, "es8311 create failed");
  const es8311_clock_config_t es_clk = {
    .mclk_inverted = false,
    .sclk_inverted = false,
    .mclk_from_mclk_pin = true,
    .mclk_frequency = EXAMPLE_SAMPLE_RATE * 256,
    .sample_frequency = EXAMPLE_SAMPLE_RATE
  };

  ESP_ERROR_CHECK(es8311_init(es_handle, &es_clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16));
  ESP_RETURN_ON_ERROR(es8311_sample_frequency_config(es_handle, es_clk.mclk_frequency, es_clk.sample_frequency), TAG, "set es8311 sample frequency failed");
  ESP_RETURN_ON_ERROR(es8311_microphone_config(es_handle, false), TAG, "set es8311 microphone failed");

  ESP_RETURN_ON_ERROR(es8311_voice_volume_set(es_handle, EXAMPLE_VOICE_VOLUME, NULL), TAG, "set es8311 volume failed");
  ESP_RETURN_ON_ERROR(es8311_microphone_gain_set(es_handle, EXAMPLE_MIC_GAIN), TAG, "set es8311 microphone gain failed");
  return ESP_OK;
}

void setup_audio() {
  i2s.setPins(I2S_SCLK, I2S_LRCK, I2S_DSDIN, -1, I2S_MCLK);
  if (!i2s.begin(I2S_MODE_STD, EXAMPLE_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
    USBSerial.println("Failed to initialize I2S bus!");
    return;
  }

  pinMode(PA_CTRL, OUTPUT);
  digitalWrite(PA_CTRL, LOW);

  if (es8311_codec_init() != ESP_OK) {
    USBSerial.println("ES8311 init failed!");
  }

  USBSerial.println("Audio ready");

  float angle = 0;
  for (int i = 0; i < 4800; i++) {
    angle += (2 * PI) * (400 + 1200 * (i / 4800.0)) / EXAMPLE_SAMPLE_RATE;
    int16_t sample = 8000 * sin(angle);
    powerUp[i * 2] = sample;      // lado esquerdo
    powerUp[i * 2 + 1] = sample;  // lado direito
  }
}

void play_power_up() {
  digitalWrite(PA_CTRL, HIGH);
  i2s.write((uint8_t *)powerUp, sizeof(powerUp));
  delay(50);
  digitalWrite(PA_CTRL, LOW);
}

void show_page_indicator() {
  if (PAGE_COUNT > 1) {
    int width = (PAGE_COUNT * 20) + ((PAGE_COUNT - 1) * 10);
    int start = ((LCD_WIDTH / 2) - (width / 2)) & ~1;
    for (int i = 0; i < PAGE_COUNT; i++) {
      int x = start + (i * 30);
      if (i == currentPage) {
        gfx->fillRect(x, 480, 20, 4, colorRed);
      } else {
        gfx->fillRect(x, 480, 20, 4, gfx->color565(0x8E, 0x8E, 0x93));
      }
    }
  }
}

void show_clock_page() {
  currentPage = 0;
  gfx->fillRect(0, 60, LCD_WIDTH, LCD_HEIGHT - 60, BLACK);
  show_date();
  show_time();
  show_page_indicator();
}

#if SERVER_MONITOR_ENABLED
const char *check_server() {
  bool connection = connect_any();
  const char *status = "";
  if (connection) {
    WiFiClientSecure client;
    client.setInsecure();  // não confere o certificado do site
    HTTPClient http;
    http.begin(client, SERVER_URL);  // prepara a consulta
    http.setTimeout(5000);           // espera no máximo 5 s pela resposta
    int code = http.GET();           // faz a consulta e devolve o código (ou negativo se falhar)
    http.end();
    if (code == 200) {
      status = "ONLINE";
    } else if ((code == 530) || (code == 502) || (code == 503) || (code == 504)) {
      status = "OFFLINE";
    } else {
      status = "UNKNOWN";
    }
  } else {
    status = "NO NETWORK";
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  return status;
}

void draw_server_unit(int y, uint16_t ledColor) {
  int x = ((LCD_WIDTH - 160) / 2) & ~1;
  gfx->fillRect(x, y, 160, 52, gfx->color565(0xD8, 0xD8, 0xDD));
  gfx->fillRect(x + 4, y + 4, 152, 44, BLACK);
  fill_even_circle(x + 24, y + 26, 6, ledColor);
  gfx->fillRect(x + 44, y + 24, 96, 4, gfx->color565(0xD8, 0xD8, 0xDD));
}

void show_server_page() {
  currentPage = 1;
  int top = 158;
  gfx->fillRect(0, 60, LCD_WIDTH, LCD_HEIGHT - 60, BLACK);  //limpa
  uint16_t ledColor = gfx->color565(0x60, 0x60, 0x66);
  draw_server_unit(top, ledColor);
  draw_server_unit(top + 68, ledColor);
  gfx->setTextColor(gfx->color565(0xD8, 0xD8, 0xDD), BLACK);
  gfx->setTextSize(5);
  int nameWidth = strlen(SERVER_NAME) * 30;
  int nameX = ((LCD_WIDTH - nameWidth) / 2) & ~1;
  gfx->setCursor(nameX, top + 146);
  gfx->print(SERVER_NAME);
  gfx->setTextColor(gfx->color565(0x60, 0x60, 0x66), BLACK);
  gfx->setTextSize(2);
  gfx->setCursor(90, top + 206);
  gfx->print("STATUS: CHECKING...");
  show_page_indicator();
  const char *status = check_server();
  if (strcmp(status, "ONLINE") == 0) {  // as letras são iguais?
    ledColor = colorGreen;
  } else if (strcmp(status, "OFFLINE") == 0) {
    ledColor = colorRed;
  } else if (strcmp(status, "UNKNOWN") == 0) {
    ledColor = colorYellow;
  }
  draw_server_unit(top, ledColor);
  draw_server_unit(top + 68, ledColor);
  int width = (8 + strlen(status)) * 12;         // 8 do "STATUS: " + o resultado
  int x = ((LCD_WIDTH - width) / 2) & ~1;        // centraliza e deixa par
  gfx->fillRect(90, top + 206, 228, 16, BLACK);  // apaga a linha inteira
  gfx->setCursor(x, top + 206);
  gfx->printf("STATUS: %s", status);
  lastScreenRefresh = millis();
}
#endif

void setup(void) {
  USBSerial.begin(115200);

#ifdef GFX_EXTRA_PRE_INIT
  GFX_EXTRA_PRE_INIT();
#endif

  // Liga a tela
  if (!gfx->begin()) {
    USBSerial.println("gfx->begin() failed!");
  }

  gfx->fillScreen(BLACK);
  show_status_bar();

  // Liga o RTC
  Wire.begin(IIC_SDA, IIC_SCL);
  if (!rtc.begin(Wire, IIC_SDA, IIC_SCL)) {
    USBSerial.println("RTC unavailable; continuing without clock updates.");
  }

  // Liga o toque
  FT3168->begin();

  // Liga o chip de energia
  if (!power.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
    USBSerial.println("AXP2101 unavailable; continuing without battery info.");
  }

  // Liga o áudio
  setup_audio();

  // Mostra a data a hora e a bateria
  show_clock_page();
  int percent = power.getBatteryPercent();
  bool chargingState = power.isCharging();
  show_battery(percent, chargingState);
  lastPercent = percent;
  lastChargingState = chargingState;

  // Sincroniza a hora pela internet
  if (connect_any()) {
    sync_time();
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  FT3168->IIC_Interrupt_Flag = false;  // abaixa a bandeirinha

  lastScreenRefresh = millis();  // inicia o timer da tela

  wasCharging = power.isCharging();
}

void loop() {
  // Dorme enquanto a tela está apagada (acorda com toque ou a cada 2s)
  if (!screenOn) {
    esp_sleep_enable_timer_wakeup(2000000ULL);                    // acorda depois de 2s
    gpio_wakeup_enable((gpio_num_t)TP_INT, GPIO_INTR_LOW_LEVEL);  // acorda quando o pino do toque for pra LOW
    esp_sleep_enable_gpio_wakeup();                               // liga o "despertador por pino"
    esp_light_sleep_start();                                      // dorme aqui... e continua desta linha ao acordar
    gpio_wakeup_disable((gpio_num_t)TP_INT);                      // desliga o "despertador por pino"
    gpio_set_intr_type((gpio_num_t)TP_INT, GPIO_INTR_NEGEDGE);    // volta a avisar só no momento em que vai pra LOW
  }

  // Cuida dos toques
  if (FT3168->IIC_Interrupt_Flag) {           // a bandeirinha do chip está levantada?
    FT3168->IIC_Interrupt_Flag = false;       // abaixa a bandeirinha do chip
    if (millis() - lastWarningMillis > 50) {  // é um toque novo
      xStart = read_touch_x();
      isTouching = true;
      if (millis() - lastTouchMillis <= 500) {  // é toque duplo
        lastTouchMillis = 0;
        if (!screenOn) {
          gfx->displayOn();  // liga a tela
          show_clock_page();
          screenOn = true;
        }
        lastScreenRefresh = millis();
      } else {
        lastTouchMillis = millis();
      }
    }
    xEnd = read_touch_x();
    lastWarningMillis = millis();
  }

  // Apaga a tela depois de 15s
  if (screenOn && ((millis() - lastScreenRefresh) > 15000)) {
    gfx->displayOff();
    screenOn = false;
  }

  // Atualiza a hora e a data
  if ((millis() - lastMillis > 1000) && screenOn && currentPage == 0) {
    lastMillis = millis();
    show_time();
    int day = rtc.getDateTime().getDay();
    if (lastDisplayedDay != day) {
      show_date();
      lastDisplayedDay = day;
    }
  }

  // Atualiza a bateria
  if ((millis() - lastBatteryUpdateMillis > 1000) && screenOn) {
    lastBatteryUpdateMillis = millis();
    int percent = power.getBatteryPercent();
    bool chargingState = power.isCharging();
    if ((percent != lastPercent) || (chargingState != lastChargingState)) {
      show_battery(percent, chargingState);
      lastPercent = percent;
      lastChargingState = chargingState;
    }
  }

  // Cuida do deslize
  if ((isTouching) && ((millis() - lastWarningMillis) > 50)) {
    lastScreenRefresh = millis();  // reinicia o temporizador após qualquer toque
    int xCourse = xEnd - xStart;

    if ((xCourse > 100) && (currentPage == 1) && screenOn) {  // (xCourse > 100) = deslizou pra direita
      show_clock_page();
    }
#if SERVER_MONITOR_ENABLED
    else if ((xCourse < -100) && (currentPage + 1 < PAGE_COUNT) && screenOn) {  //(xCourse < -100) = deslizou pra esquerda
      show_server_page();
    }
#endif
    isTouching = false;
  }

  // Atualiza a data e hora todo dia às 4h
  if ((millis() - lastSyncCheck) > 60000) {
    lastSyncCheck = millis();
    RTC_DateTime datetime = rtc.getDateTime();
    if ((datetime.getDay() != lastSyncDay) && (datetime.getHour() == 4)) {
      if (connect_any()) {
        sync_time();
      }

      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);

      lastSyncDay = datetime.getDay();
    }
  }

  // Detecta se o cabo foi conectado
  if ((millis() - lastChargeCheck) > 1000) {
    lastChargeCheck = millis();
    bool chargingState = power.isCharging();  //agora
    if (!wasCharging && chargingState) {
      if (!screenOn) {
        gfx->displayOn();
        screenOn = true;
        if (currentPage == 0) {
          show_clock_page();
        }
#if SERVER_MONITOR_ENABLED
        else if (currentPage == 1) {
          show_server_page();
        }
#endif
      }
      play_power_up();
      lastScreenRefresh = millis();
    }
    wasCharging = chargingState;
  }
}
