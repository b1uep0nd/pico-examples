# Adafruit IO BMP280 Example

Pico WでBMP280センサーのデータをAdafruit IOに送信するサンプルです。

## 必要なもの

- Raspberry Pi Pico W
- BMP280ブレークアウトボード
- ジャンパーワイヤー（M/M）× 4本
- Wi-Fi接続環境

## 配線

- **GPIO 4 (Pin 6)** → **SDA** (BMP280)
- **GPIO 5 (Pin 7)** → **SCL** (BMP280)
- **3.3V (Pin 36)** → **VCC** (BMP280)
- **GND (Pin 38)** → **GND** (BMP280)

## Adafruit IOの設定

1. [Adafruit IO](https://io.adafruit.com) にアカウントを作成
2. 「My Key」から以下を取得：
   - Username
   - Active Key
3. 「Feeds」で2つのFeedを作成：
   - `bmp280-temp` (温度用)
   - `bmp280-pressure` (気圧用)
4. 「Dashboards」でダッシュボードを作成し、Feedを追加

## ビルド方法

```bash
cd /pico-examples
mkdir -p build
cd build
cmake .. -DPICO_BOARD=pico_w \
  -DWIFI_SSID="your_wifi_ssid" \
  -DWIFI_PASSWORD="your_wifi_password" \
  -DADAFRUIT_IO_USERNAME="your_adafruit_username" \
  -DADAFRUIT_IO_KEY="your_adafruit_key" \
  -DADAFRUIT_IO_FEED_TEMP="bmp280-temp" \
  -DADAFRUIT_IO_FEED_PRESSURE="bmp280-pressure"
make adafruit_io_bmp280
```

または、環境変数を使用：

```bash
export WIFI_SSID="your_wifi_ssid"
export WIFI_PASSWORD="your_wifi_password"
export ADAFRUIT_IO_USERNAME="your_adafruit_username"
export ADAFRUIT_IO_KEY="your_adafruit_key"
export ADAFRUIT_IO_FEED_TEMP="bmp280-temp"
export ADAFRUIT_IO_FEED_PRESSURE="bmp280-pressure"

cmake .. -DPICO_BOARD=pico_w
make adafruit_io_bmp280
```

## 実行方法

1. UF2ファイルをPico Wに書き込む：
   ```bash
   cp build/pico_w/adafruit_io_bmp280/adafruit_io_bmp280.uf2 /media/ユーザー名/RPI-RP2/
   ```

2. シリアルモニターで動作確認（オプション）：
   ```bash
   putty -serial /dev/ttyACM0 -sercfg 115200,8,n,1,N
   ```

3. Adafruit IOのダッシュボードでデータを確認

## 動作

- 500msごとにセンサーからデータを読み取り
- 5秒ごとにAdafruit IOに送信
- LEDが点滅して動作中であることを示します

## トラブルシューティング

### Wi-Fi接続に失敗する場合
- SSIDとパスワードが正しいか確認
- Wi-Fiが2.4GHz帯であることを確認（Pico Wは5GHz非対応）

### Adafruit IOへの送信に失敗する場合
- UsernameとKeyが正しいか確認
- Feed名が正しいか確認
- インターネット接続を確認

### センサーが認識されない場合
- 配線を確認
- I2Cアドレスが0x76であることを確認（一部のモジュールは0x77）

