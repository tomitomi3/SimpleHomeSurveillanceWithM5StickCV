import image, lcd, sensor,gc
from fpioa_manager import fm,board_info
import KPU as kpu
from Maix import GPIO
from pmu import axp192
from machine import UART,I2C
import KPU as kpu

# Refference code anoken 2019
# https://gist.github.com/anoken/8b0ce255e9aef9d1a7f4d46272cedcaa#file-maixpy_unitv-py-L9

#------------------------------------------------------------------------------
# Functions
#------------------------------------------------------------------------------
#def Init():

#------------------------------------------------------------------------------
# Init
#------------------------------------------------------------------------------
#UART 初期化
print("\n--- UART Initialize ---")
fm.register(35, fm.fpioa.UART1_TX, force=True)
fm.register(34, fm.fpioa.UART1_RX, force=True)
uart1 = UART(UART.UART1, 115200,8,0,0, timeout=1000, read_buf_len=4096)

#lcd
print("\n--- Lcd Initialize ---")
lcd.init()
lcd.rotation(2)
time.sleep(0.1)

#電源管理
pmu = axp192()
pmu.setScreenBrightness(10) # 8だとちらつく
time.sleep(0.1)

#カメラの初期化
print("\n--- Camera Initialize ---")
while 1:
    try:
        time.sleep(0.01)
        sensor.reset()
        break
    except:
        time.sleep(0.01)
        continue

#sensor.set_hmirror(1)
#sensor.set_vflip(1)
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA) #QVGA=320x240
#sensor.set_windowing((224, 224))
sensor.run(1)
time.sleep(0.1)

#init kpu デフォルトの顔認識モデル
print("\n--- KPU Initialize ---")
task = kpu.load(0x300000) # Load Model File from Flash
anchor = (1.889, 2.5245, 2.9465, 3.94056, 3.99987, 5.3658, 5.155437, 6.92275, 6.718375, 9.01025)
# Anchor data is for bbox, extracted from the training sets.
kpu.init_yolo2(task, 0.5, 0.3, 5, anchor)


#------------------------------------------------------------------------------
# Loop
#------------------------------------------------------------------------------
try:
    print("--- loop ---")
    while(True):
        #カメラから画像取得
        img_org = sensor.snapshot()

        #顔認識
        bbox = kpu.run_yolo2(task, img_org) # Run the detection routine
        if bbox:
            #加工前にコピー 送信用に加工
            img_buf = img_org.copy()
            img_buf = img_buf.resize(240,160)
            img_buf.compress(quality=60)

            #書き込み
            for i in bbox:
                print(i)
                img_org.draw_rectangle(i.rect())

            #確認用に表示 カメラの比率
            lcd.display(img_org)

            #画像サイズを8bitに分割
            img_size1 = (img_buf.size()& 0xFF0000)>>16
            img_size2 = (img_buf.size()& 0x00FF00)>>8
            img_size3 = (img_buf.size()& 0x0000FF)>>0

            #10バイト分パケット
            data_packet = bytearray([0xFF,0xF1,0xF2,0xA1,img_size1,img_size2,img_size3,0x00,0x00,0x00])

            #送信
            uart1.write(data_packet)
            uart1.write(img_buf)
            print(img_buf.size())
            #print("",img_buf.size(),",",data_packet)

        #確認用に表示
        lcd.display(img_org)

        #wait
        time.sleep(0.1)

except KeyboardInterrupt:
    a = kpu.deinit(task)
    sys.exit()


