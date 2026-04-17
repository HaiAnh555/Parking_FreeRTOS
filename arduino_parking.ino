#include <Arduino.h>
#include <Arduino_FreeRTOS.h>
#include <queue.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Servo.h>

// ===== CẤU HÌNH THIẾT BỊ =====
LiquidCrystal_I2C lcd(0x27, 16, 2);
#define SS_PIN 10
#define RST_PIN 9
MFRC522 rfid(SS_PIN, RST_PIN);

#define IR1 2
#define IR2 3
#define IR3 4
#define IR4 5
#define SERVO_IN 6
#define SERVO_OUT 7
#define BUZZER 8

Servo servoIn, servoOut;

// ===== CẤU TRÚC DỮ LIỆU RTOS =====
typedef struct {
  int cmd; // 1: In, 2: Out, 3: Full, 4: Error
  int minutes;
  int fee;
  byte uid[4];
} GateData;

QueueHandle_t gateQueue;
bool slot[4];

// ===== QUẢN LÝ UID  =====
byte validUIDs[][4] = { {0x5A,0x73,0x3D,0x02}, {0x6F,0x0A,0x20,0x1F}, {0xFF,0x9B,0x57,0x1E}, {0xD1,0xE1,0xFD,0x53}, {0xE1,0x64,0xA2,0x53} };
byte invalidUIDs[][4] = { {0x2C,0xD1,0x45,0x03} };
#define VALID_COUNT (sizeof(validUIDs)/4)
#define INVALID_COUNT (sizeof(invalidUIDs)/4)

#define MAX_USERS 5
byte uidList[MAX_USERS][4];
unsigned long timeIn[MAX_USERS];
int uidCount = 0;

// ===== CÁC HÀM HỖ TRỢ (Tối ưu RAM với F() Macro) =====
void beep(int t){
  for(int i=0;i<t;i++){
    digitalWrite(BUZZER,1); vTaskDelay(pdMS_TO_TICKS(80));
    digitalWrite(BUZZER,0); vTaskDelay(pdMS_TO_TICKS(80));
  }
}

void openServo(Servo &s, int pin){
  s.attach(pin); s.write(170);
  vTaskDelay(pdMS_TO_TICKS(1500)); 
  s.write(90); vTaskDelay(pdMS_TO_TICKS(200)); s.detach();
}

bool compareUID(byte *a, byte *b){
  for(int i=0;i<4;i++) if(a[i]!=b[i]) return false;
  return true;
}

int findUID(byte *u){
  for(int i=0;i<uidCount;i++) if(compareUID(uidList[i],u)) return i;
  return -1;
}

int freeSlotCount(){
  int c=0;
  for(int i=0;i<4;i++) if(!slot[i]) c++;
  return c;
}

void printUID_LCD(byte *uid){
  char buf[12];
  sprintf(buf,"%02X %02X %02X %02X", uid[0], uid[1], uid[2], uid[3]);
  lcd.setCursor(0,1); lcd.print(buf);
}

// ===== TASK 1: QUÉT CẢM BIẾN (Gửi S: sang ESP32) =====
void TaskSensor(void *pv){
  for(;;){
    slot[0] = (digitalRead(IR1) == LOW);
    slot[1] = (digitalRead(IR2) == LOW);
    slot[2] = (digitalRead(IR3) == LOW);
    slot[3] = (digitalRead(IR4) == LOW);

    // Truyền sang ESP32 định dạng S:1010
    Serial.print(F("S:"));
    for(int i=0; i<4; i++) Serial.print(slot[i] ? "1" : "0");
    Serial.println();

    vTaskDelay(pdMS_TO_TICKS(1500)); 
  }
}

// ===== TASK 2: QUÉT THẺ RFID (Logic Xe Vào/Ra) =====
void TaskRFID(void *pv){
  static uint32_t lastRead = 0;
  for(;;){
    if(millis() - lastRead > 1000 && rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()){
      lastRead = millis();
      GateData data;
      memcpy(data.uid, rfid.uid.uidByte, 4);
      
      bool valid = false;
      for(int i=0; i<VALID_COUNT; i++) if(compareUID(data.uid, validUIDs[i])) valid = true;
      
      if(!valid){
        data.cmd = 4; // Thẻ không hợp lệ
      } else {
        int idx = findUID(data.uid);
        if(idx == -1){
          if(freeSlotCount() > 0 && uidCount < MAX_USERS){
            memcpy(uidList[uidCount], data.uid, 4);
            timeIn[uidCount] = millis();
            uidCount++;
            data.cmd = 1; // Vào bãi
          } else data.cmd = 3; // Hết chỗ
        } else {
          unsigned long t = (millis() - timeIn[idx]) / 60000;
          data.minutes = (t == 0) ? 1 : (int)t;
          data.fee = data.minutes * 3000;
          data.cmd = 2; // Ra bãi
          for(int i=idx; i<uidCount-1; i++){
            memcpy(uidList[i], uidList[i+1], 4);
            timeIn[i] = timeIn[i+1];
          }
          uidCount--;
        }
      }
      xQueueSend(gateQueue, &data, 0);
      rfid.PICC_HaltA(); rfid.PCD_StopCrypto1();
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ===== TASK 3: ĐIỀU KHIỂN LCD & SERVO (Hiển thị Slot F/E) =====
void TaskControl(void *pv){
  GateData d;
  uint32_t tLCD = 0;
  for(;;){
    // Cập nhật LCD trạng thái Slot mỗi 1 giây
    if(millis() - tLCD > 1000){
      tLCD = millis();
      lcd.setCursor(0,0);
      lcd.print(F("Con:")); lcd.print(freeSlotCount()); lcd.print(F(" Slot      "));
      
      lcd.setCursor(0,1);
      lcd.print(slot[0] ? F("1:F ") : F("1:E "));
      lcd.print(slot[1] ? F("2:F ") : F("2:E "));
      lcd.print(slot[2] ? F("3:F ") : F("3:E "));
      lcd.print(slot[3] ? F("4:F")  : F("4:E"));
    }

    if(xQueueReceive(gateQueue, &d, 0)){
      if(d.cmd == 1){ // VÀO
        Serial.print(F("IN:"));
        for(int i=0; i<4; i++){ if(d.uid[i]<0x10) Serial.print("0"); Serial.print(d.uid[i], HEX); }
        Serial.println();
        lcd.clear(); lcd.print(F("WELCOME")); printUID_LCD(d.uid);
        beep(1); openServo(servoIn, SERVO_IN);
        tLCD = millis(); 
      } 
      else if(d.cmd == 2){ // RA
        Serial.print(F("OUT:"));
        for(int i=0; i<4; i++){ if(d.uid[i]<0x10) Serial.print("0"); Serial.print(d.uid[i], HEX); }
        Serial.print(","); Serial.print(d.minutes);
        Serial.print(","); Serial.println(d.fee);
        lcd.clear(); lcd.print(F("M:")); lcd.print(d.minutes); lcd.print(F(" P:")); lcd.print(d.fee);
        printUID_LCD(d.uid);
        beep(2); openServo(servoOut, SERVO_OUT);
        tLCD = millis();
      }
      else if(d.cmd == 3){ // FULL
        lcd.clear(); lcd.print(F("BAI FULL!")); beep(3);
        vTaskDelay(pdMS_TO_TICKS(1500));
      }
      else if(d.cmd == 4){ // SAI THẺ
        lcd.clear(); lcd.print(F("THE SAI!")); printUID_LCD(d.uid); beep(4);
        vTaskDelay(pdMS_TO_TICKS(1500));
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void setup(){
  Serial.begin(9600); // Giao tiếp với Serial Monitor & ESP32
  pinMode(IR1,INPUT_PULLUP); pinMode(IR2,INPUT_PULLUP);
  pinMode(IR3,INPUT_PULLUP); pinMode(IR4,INPUT_PULLUP);
  pinMode(BUZZER,OUTPUT);

  Wire.begin(); lcd.init(); lcd.backlight();
  SPI.begin(); rfid.PCD_Init();

  gateQueue = xQueueCreate(3, sizeof(GateData));

  
  xTaskCreate(TaskSensor, "SEN", 80, NULL, 1, NULL);
  xTaskCreate(TaskRFID, "RF", 130, NULL, 2, NULL);
  xTaskCreate(TaskControl, "CTL", 130, NULL, 3, NULL);
}

void loop() {}
