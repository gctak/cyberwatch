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

// Configurações do relógio
const char *WEEKDAYS[] = { "Dom", "Seg", "Ter", "Qua", "Qui", "Sex", "Sab" };
const int BOLT_START[] = { 4, 3, 2, 1, 0, 2, 1, 0 };
const int BOLT_LEN[] = { 2, 2, 2, 5, 5, 2, 2, 2 };

// Variáveis do relógio
unsigned long lastMillis = 0;         // momento da última atualização da tela
unsigned long lastWarningMillis = 0;  // momento do último aviso
unsigned long lastTouchMillis = 0;    // momento do último toque -> detecta o duplo
unsigned long lastScreenRefresh = 0;  // momento que a tela foi acesa
bool screenOn = true;                 // tela está ligada?
int lastDisplayedDay = 0;             // último dia exibido na tela
uint16_t statusColor = gfx->color565(0xCD, 0x2C, 0x25);
int lastPercent = 101;           // última porcentagem de bateria exibida
bool lastChargingState = false;  // último estado de carregamento registrado
bool isTouching = false;         // há um toque ativo na tela?
int xStart = 0;                  // posição X  inicial do deslize
int xEnd = 0;                    // posição X final do deslize

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
  gfx->setTextColor(statusColor, BLACK);
  gfx->setTextSize(2);
  gfx->setCursor(60, 40);
  gfx->print("CYBERWATCH");
  fill_even_circle(46, 48, 6, statusColor);
}

void show_battery(int percent, bool chargingState) {
  int fillWidth = (32 * percent / 100) & ~1;

  gfx->fillRect(314, 40, 32, 16, gfx->color565(0x2C, 0x2C, 0x2E));
  gfx->fillRect(346, 44, 4, 8, gfx->color565(0x2C, 0x2C, 0x2E));

  if (chargingState) {
    gfx->fillRect(314, 40, fillWidth, 16, gfx->color565(0x30, 0xD1, 0x58));
    for (int line = 0; line < 8; line++) {
      gfx->fillRect(324 + BOLT_START[line] * 2, 40 + line * 2, BOLT_LEN[line] * 2, 2, gfx->color565(0xD8, 0xD8, 0xDD));
    }
  } else if (percent <= 20) {
    gfx->fillRect(314, 40, fillWidth, 16, gfx->color565(0xFF, 0xD6, 0x0A));  //amarelo
  } else {
    gfx->fillRect(314, 40, fillWidth, 16, gfx->color565(0x8E, 0x8E, 0x93));
  }
}

int read_touch_x() {
  return FT3168->IIC_Read_Device_Value(FT3168->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_X);
}

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

  show_time();

  // Tenta o Wi-Fi
  int attempts = 0;
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    show_time();
    delay(500);
    attempts += 1;
  }
  if (WiFi.status() == WL_CONNECTED) {
    USBSerial.println("Wi-Fi connection successful");
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
    } else {
      USBSerial.println("Failed to synchronize time with NTP server");
    }
  } else {
    USBSerial.println("Wi-Fi connection failed");
    USBSerial.printf("Status:%d\n", WiFi.status());
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  FT3168->IIC_Interrupt_Flag = false;  // abaixa a bandeirinha

  lastScreenRefresh = millis();  // inicia o timer da tela
}

void loop() {
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
          show_time();
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
  if (screenOn && (millis() - lastScreenRefresh > 15000)) {
    gfx->displayOff();
    screenOn = false;
  }

  // Atualiza a hora e a data
  if ((millis() - lastMillis > 1000) && screenOn) {
    lastMillis = millis();
    show_time();
    int day = rtc.getDateTime().getDay();
    if (lastDisplayedDay != day) {
      show_date();
      lastDisplayedDay = day;
    }
    // Atualiza a bateria
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
    int xCourse = xEnd - xStart;

    if (xCourse > 100) {
      USBSerial.println("Swiped right");
    } else if (xCourse < -100) {
      USBSerial.println("Swiped left");
    }
    
    isTouching = false;
  }
}
