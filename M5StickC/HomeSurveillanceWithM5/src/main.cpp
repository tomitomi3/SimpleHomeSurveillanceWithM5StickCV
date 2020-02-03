#include <M5StickC.h>
#include <WiFi.h>
#include <ssl_client.h>
#include <WiFiClientSecure.h>

//Refference code anoken 2019
//https://gist.github.com/anoken/8b0ce255e9aef9d1a7f4d46272cedcaa
//https://github.com/anoken/purin_wo_mimamoru_gijutsu/blob/master/2_6_M5Camera_Send_LineNotify/2_6_M5Camera_Send_LineNotify.ino

HardwareSerial serial_ext(2); //UARTは3つ 0は予約済

//LINE Notify
const char* ssid	= ""; //wifi ssid
const char* passwd	= ""; //wifi password
const char* host	= "notify-api.line.me";
const char* token	= ""; //line notify token

//Jpeg
typedef struct {
  uint32_t length;
  uint8_t *buf;
} jpeg_data_t;

jpeg_data_t jpeg_data;
static const int RX_BUF_SIZE = 10000; //bufferの一時領域
static const uint8_t packet_img[4] = { 0xFF, 0xF1, 0xF2, 0xA1}; //識別用パケット

//------------------------------------------------------------------------------------------
// プロトタイプ宣言
//------------------------------------------------------------------------------------------
void setup_wifi();
void sendLineNotify(uint8_t* image_data, size_t image_sz);

//------------------------------------------------------------------------------------------
// Initilize
//------------------------------------------------------------------------------------------
#define WAIT_INIT 100
void setup() {
  //M5 init
  M5.begin();

  //使わない
  M5.Axp.ScreenBreath(8); //明るさ
  //EXTEN 5V OFF
  {
      Wire1.beginTransmission(0x34);
      Wire1.write(0x10);      
      Wire1.endTransmission();
      Wire1.requestFrom(0x34, 1);
      uint8_t state = Wire1.read() & ~(1 << 2);
      Wire1.beginTransmission(0x34);
      Wire1.write(0x10);
      Wire1.write(state);
      Wire1.endTransmission();
  }
  delay(WAIT_INIT);

  //シリアル通信の開始
  serial_ext.begin(115200, SERIAL_8N1, 32, 33); //RX:32, TX:33
  Serial.println("Setup() start");

  //LCD condfig
  M5.Lcd.setRotation(0);
  M5.Lcd.println("Initialize...");
  M5.Lcd.fillScreen(BLACK);

  //wifi
  setup_wifi();

  //メモリ確保
  jpeg_data.buf = (uint8_t *) malloc(sizeof(uint8_t) * RX_BUF_SIZE);
  jpeg_data.length = 0;
  if (jpeg_data.buf == NULL) {
    Serial.println("malloc jpeg buffer 1 error");
  }

  //OK
  M5.Lcd.println("OK");
  M5.Lcd.fillScreen(BLACK);
  delay(WAIT_INIT);
}

//------------------------------------------------------------------------------------------
// Loop
//------------------------------------------------------------------------------------------
bool isOneshot = false;
portTickType recentNotifyTime, nowTime;
#define NotifyInterval 15 //直近の通知から何秒たったら送信するか
void loop() {
  //ボタン呼び出し
  M5.update();

  //one shot
  if(isOneshot==false)
  {
    Serial.println("--- Loop() start ---");
    nowTime = xTaskGetTickCount();
    recentNotifyTime = xTaskGetTickCount();
    isOneshot = true;
  }

  //時刻
  nowTime = xTaskGetTickCount();

  //LCD
  M5.Lcd.drawString("wait...", 5, 5);

  //シリアル
  if (serial_ext.available()) {
    //10バイト分読み込みと確認
    uint8_t rx_buffer[10] = {0};
    int rx_size = serial_ext.readBytes(rx_buffer, 10);    
    if (rx_size == 10) {
      //受信データのチェック 指定4バイト有無確認
      if ((rx_buffer[0] == packet_img[0]) && (rx_buffer[1] == packet_img[1]) && 
            (rx_buffer[2] == packet_img[2]) && (rx_buffer[3] == packet_img[3])) {
        //画像サイズ
        jpeg_data.length = (uint32_t)(rx_buffer[4] << 16) | (rx_buffer[5] << 8) | rx_buffer[6];
        Serial.print("Image Size = ");
        Serial.println(jpeg_data.length);

        //画像データ読み込み
        serial_ext.readBytes(jpeg_data.buf, jpeg_data.length);

        //LINE通知
        uint32_t intervalSecond = ((nowTime - recentNotifyTime) * portTICK_RATE_MS)/1000;
        Serial.println(intervalSecond); //debug
        if(intervalSecond>NotifyInterval)
        {
          M5.Lcd.fillScreen(BLACK);
          M5.Lcd.drawString("Line notify!", 5, 5);
          Serial.println("Line notify!");

          recentNotifyTime = xTaskGetTickCount();
          sendLineNotify(jpeg_data.buf, jpeg_data.length);
          
          M5.Lcd.fillScreen(BLACK);
        }

        //M5StickC is not this function (arduino m5stickc lib ver0.1.1)
        //you need add this
        //https://github.com/anoken/M5StickC/commit/34965325891bdcab97f2bb056d4838cdf75e1b8d
        //M5.lcd.drawJpg(jpeg_data.buf, jpeg_data.length, 0, 20);
      }
      else {
        //バッファを空にする
        serial_ext.flush();
        Serial.println("seriali flush!");
        //int rx_size = serial_ext.readBytes(rx_buffer, 1);
      }
    }
  }

  //10ms待機 : FreeRTOSのAPI タスクを待たせるAPI. https://qiita.com/eggman/items/58a772c0669781863ca9
  vTaskDelay(10 / portTICK_RATE_MS);
}

//init wifi
void setup_wifi() {
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, passwd);
  delay(100);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  delay(100);
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  delay(100);
}

//Line通知
void sendLineNotify(uint8_t* image_data, size_t image_sz) {
  WiFiClientSecure client;
  if (!client.connect(host, 443))
  {
    return;
  }

  int httpCode = 404;
  size_t image_size = image_sz;
  String boundary = "----detect--";
  String body = "--" + boundary + "\r\n";
  String message = "顔検出";
  body += "Content-Disposition: form-data; name=\"message\"\r\n\r\n" + message + " \r\n";
  if (image_data != NULL && image_sz > 0 ) {
    image_size = image_sz;
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"imageFile\"; filename=\"image.jpg\"\r\n";
    body += "Content-Type: image/jpeg\r\n\r\n";
  }
  String body_end = "--" + boundary + "--\r\n";
  size_t body_length = body.length() + image_size + body_end.length();
  
  String header = "POST /api/notify HTTP/1.1\r\n";
  header += "Host: notify-api.line.me\r\n";
  header += "Authorization: Bearer " + String(token) + "\r\n";
  header += "User-Agent: " + String("M5Stack") + "\r\n";
  header += "Connection: close\r\n";
  header += "Cache-Control: no-cache\r\n";
  header += "Content-Length: " + String(body_length) + "\r\n";
  header += "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n\r\n";
  client.print(header + body);
  //Serial.print(header + body);

  bool Success_h = false;
  uint8_t line_try = 3;
  while (!Success_h && line_try-- > 0) {
    if (image_size > 0) {
      size_t BUF_SIZE = 1024;
      if ( image_data != NULL) {
        uint8_t *p = image_data;
        size_t sz = image_size;
        while ( p != NULL && sz) {
          if ( sz >= BUF_SIZE) {
            client.write( p, BUF_SIZE); //送信
            p += BUF_SIZE; sz -= BUF_SIZE;
          } else {
            client.write( p, sz);
            p += sz; sz = 0;
          }
        }
      }
      client.print("\r\n" + body_end);
      Serial.print("\r\n" + body_end);

      //受信
      while ( client.connected() && !client.available()) delay(10);

      //HTTPコード受信
      if ( client.connected() && client.available() ) {
        String resp = client.readStringUntil('\n');
        httpCode    = resp.substring(resp.indexOf(" ") + 1, resp.indexOf(" ", resp.indexOf(" ") + 1)).toInt();
        Success_h   = (httpCode == 200); //200受信でOK
        //Serial.println(resp);
      }
      delay(10);
    }
  }
  client.stop();
}
