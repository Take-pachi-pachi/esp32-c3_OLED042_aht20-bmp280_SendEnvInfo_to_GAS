# ESP32-C3 AHT20 + BMP280 環境データロガー

ESP32-C3でAHT20とBMP280から温湿度・気圧を取得し、0.42インチOLEDに表示する環境データロガーです。測定データはWi-Fi経由でGoogle Apps Script（GAS）のWeb APIへHTTP GETで送信し、Googleスプレッドシートへ記録します。

AHT20/BMP280の生データを直接読み取り、ESP32内部時計を使って送信時刻を管理します。

## 構成

- マイコン: ESP32-C3 DevKitM-1
- 温湿度センサ: AHT20（I2C）
- 気圧センサ: BMP280（I2C）
- 表示: 0.42インチOLED SSD1306（I2C）
- 開発環境: VS Code + PlatformIO
- 通信: Wi-Fi、NTP、GAS Web API
- 時刻管理: ESP32内部時計（NTPで同期）

## 主な機能

- AHT20から温度・湿度を取得
- BMP280から温度・気圧を取得
- 2つのセンサーが利用可能な場合、補正後の温度を平均して表示・送信
- 0.42インチOLEDへ温度・湿度・気圧を表示
- ESP32内部時計を使って、指定分にGASへ送信
- 通常時はWi-Fiを切断し、指定時刻にだけ接続して送信
- 不快指数（DI）を計算して送信

## 必要なもの

- ESP32-C3 DevKitM-1
- AHT20モジュール
- BMP280モジュール
- 0.42インチOLED SSD1306 I2Cモジュール
- USBケーブル（データ通信対応）
- 3.3Vまたはモジュール仕様に合う電源
- Windows PC
- VS Code
- PlatformIO IDE
- Googleアカウント（GASとスプレッドシート用）

## 配線

OLED、AHT20、BMP280は同じI2Cバスに接続します。

| 信号 | ESP32-C3 |
| --- | --- |
| SDA | GPIO5 |
| SCL | GPIO6 |
| VCC | モジュール仕様に合わせる |
| GND | GND |

想定I2Cアドレスは次のとおりです。

| デバイス | アドレス |
| --- | --- |
| OLED | `0x3C` |
| AHT20 | `0x38` |
| BMP280 | `0x76` / `0x77` |

起動時にI2Cスキャンを行い、シリアルモニターに各デバイスが認識されることを確認してください。

## config.h パラメータ

[src/config.h](src/config.h) を編集します。

```cpp
const char* WIFI_SSID     = "使用するWi-FiのSSID";
const char* WIFI_PASSWORD = "Wi-Fiパスワード";
const char* TIME_ZONE     = "JST-9";
const char* NTP_SERVER_PRIMARY   = "ntp.nict.jp";
const char* NTP_SERVER_SECONDARY = "pool.ntp.org";

const char* SHEET_URL     = "GAS WebアプリのURL";
const char* SHEET_NAME    = "Test";
const int SEND_MINUTES[]  = {10};

const bool DEBUG_MODE     = false;
const bool OLED_ROTATED   = true;
const float TEMP_OFFSET_aht = -0.8F;
const float TEMP_OFFSET_bmp = -1.3F;
const float HUM_OFFSET_RATE = -0.1F;
const float PRESS_OFFSET = -4.1F;
```

`config.h`にはWi-FiパスワードやGAS URLが含まれるため、公開リポジトリへ実値をそのまま登録しないでください。実機用の設定はローカルに保持し、GitHubに上げるときはプレースホルダーに置き換えてください。

`TIME_ZONE`はPOSIX形式のタイムゾーン文字列です。日本時間は`JST-9`を指定します。

`SEND_MINUTES`には、毎時の送信分を配列で指定します。各値は`0`から`59`までです。例: `{10, 40}`なら、毎時10分と40分に送信します。

## セットアップ

### 1. VS CodeとPlatformIOを準備

1. VS Code をインストールします。
2. 拡張機能から `PlatformIO IDE` をインストールします。
3. VS Code を再起動します。

### 2. 仮想環境を使う場合

このリポジトリでは、ローカル開発用に Python の仮想環境を作成して PlatformIO を使う構成にできます。

```powershell
cd "c:\Users\<ユーザー名>\Programming\PlatformIO\Projects\esp32-c3_OLED042_aht20+bmp280_SendEnvInfo_to_GAS"
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
python -m pip install platformio
```

その後、次を実行します。

```powershell
pio run
pio run --target upload
```

### 3. 構成確認

このプロジェクト直下に次のファイルがあることを確認します。

```text
platformio.ini
src/main.cpp
src/config.h
README.md
LICENSE
THIRD_PARTY_LICENSES.md
```

## GASの設定

詳細は https://github.com/Take-pachi-pachi/GAS_Raspi_Temp_Hum_press_IAQ/ を参照。

1. Googleスプレッドシートを作成します。
2. 書き込み先のシートタブを作成します。
3. [拡張機能] > [Apps Script] を開きます。
4. GASコードを貼り付けて保存します。
5. プロジェクトの設定でスクリプトプロパティ `Spread_ID` を追加します。
6. Webアプリとしてデプロイします。
   - 実行ユーザー: 自分
   - アクセス権: 全員
7. 発行されたURLを `src/config.h` の `SHEET_URL` に設定します。
8. 書き込み先シート名を `SHEET_NAME` に設定します。

## 送信データ

GASへ次のクエリパラメータを送信します。

| パラメータ | 内容 |
| --- | --- |
| `p7` | `SHEET_NAME` に指定したシート名 |
| `p1` | ESP32内部時計で取得した時刻（`YYYY-MM-DD_HH:MM`） |
| `p2` | 補正後の温度（℃） |
| `p3` | 補正後の湿度（%） |
| `p4` | 補正後の気圧（hPa） |
| `p5` | この構成では空欄 |
| `p6` | 温度と湿度から計算した不快指数（DI） |

GASのWebアプリはリダイレクトを返すため、ESP32側ではリダイレクト追従を有効にしています。

## ビルドと書き込み

### PlatformIO画面から実行

1. 左側のPlatformIOアイコンを開く
2. `Project Tasks` を開く
3. `esp32-c3-devkitm-1` を選択
4. `General` > `Build`
5. ESP32-C3をUSB接続
6. `General` > `Upload`

### ターミナルから実行

```powershell
pio run
pio run --target upload
```

または、仮想環境を使っている場合は次のように実行します。

```powershell
.\.venv\Scripts\Activate.ps1
pio run
pio run --target upload
```

## シリアルモニター

通信速度は `115200` です。

```powershell
pio device monitor --baud 115200
```

正常起動時の主なログは次のようになります。

```text
Initial Wi-Fi connection
Connecting to Wi-Fi.....
Wi-Fi connected: 192.168.x.x
Initial NTP synchronization
NTP synchronized: 2026-09-21 12:34:56
Wi-Fi disconnected
...
Wi-Fi connected: 192.168.x.x
Sending URL:
https://script.google.com/macros/s/.../exec?p7=Test&p1=2026-09-21_12:10&p2=25.74&p3=63.00&p4=1015.2&p5=&p6=74.30
Measurement sent: HTTP 200
{"value":"ok"}
```

## 処理の流れ

### 起動時

1. シリアル通信を初期化します。
2. Wi-Fiへ接続します。
3. NTPでESP32内部時計を日本時刻に合わせます。
4. Wi-Fiを切断し、通常時の消費電力を抑えます。
5. OLEDとI2Cバスを初期化します。
6. AHT20とBMP280を初期化します。

### 繰り返し処理

1. AHT20/BMP280から温度・湿度・気圧を取得します。
2. 補正値を加えて表示用の温度を計算します。
3. 温度・湿度・気圧をOLEDへ表示します。
4. `DEBUG_MODE` が `true` の場合、センサ値と時刻をシリアル出力します。
5. ESP32内部時計を確認し、送信予定分かどうかを判定します。
6. 予定分に達したらWi-Fiへ接続します。
7. 直前に取得したセンサ値と、内部時計から生成した時刻でURLを作成します。
8. HTTP GETでGASへ送信し、HTTPステータスが `200` から `299` なら成功と判定します。
9. 送信後にWi-Fiを切断します。

## 補正値・表示設定

センサーの実測値に合わせる場合は、[src/config.h](src/config.h) の次の設定を調整します。

```cpp
const float TEMP_OFFSET_aht = -0.8F;
const float TEMP_OFFSET_bmp = -1.3F;
const float HUM_OFFSET_RATE = -0.1F;
const float PRESS_OFFSET = -4.1F;
```

### 温度補正

```text
AHT20補正後温度 = AHT20生値 + TEMP_OFFSET_aht
BMP280補正後温度 = BMP280生値 + TEMP_OFFSET_bmp
```

両方のセンサーが使える場合は、補正後の平均を表示・送信温度とします。

```text
表示・送信温度 = (AHT20補正後温度 + BMP280補正後温度) / 2
```

### 湿度補正

```text
補正後湿度 = 湿度生値 * (1 + HUM_OFFSET_RATE)
```

### 気圧補正

```text
補正後気圧 = BMP280生値気圧 + PRESS_OFFSET
```

## トラブルシューティング

### センサーが見つからない

シリアルモニターに `0x38`、`0x3C`、`0x77` が表示されない場合は、次を確認します。

- SDAがGPIO5、SCLがGPIO6になっているか
- VCCが適切か
- GNDが共通か
- 配線やはんだ接続に不良がないか
- BMP280のI2Cアドレスが `0x76` / `0x77` のどちらかと一致しているか

### Wi-Fiに接続できない

- SSID とパスワードが正しいか
- スマホやPCが接続できる範囲か
- 2.4GHz帯のWi-Fiかどうか

### 送信されない

- `SEND_MINUTES` に有効な分が設定されているか
- NTP同期が成功しているか
- `WIFI_SSID` / `WIFI_PASSWORD` が正しいか
- GASのWebアプリURLが正しいか

### COMポートが使用中

```text
Could not open COMx, the port is busy or doesn't exist.
```

シリアルモニターや他のアプリがCOMポートを占有していないか確認してから、再度 Upload を実行してください。

## セキュリティ上の注意

- Wi-FiパスワードやGAS URLを公開リポジトリへ登録しないでください。
- `src/config.h` はGit管理対象から除外することを推奨します。
- GASのアクセス権を `全員` にする場合、URLを知っている第三者からアクセスされる可能性があります。
- 実機用の設定と公開用の設定を分けて管理してください。

## 使用ライブラリ

`platformio.ini`で次のライブラリを使用しています。

- `olikraus/U8g2`
- `adafruit/Adafruit AHTX0`
- `adafruit/Adafruit BMP280 Library`
- ESP32 Arduino標準の `WiFi`, `HTTPClient`, `Wire`

BSEC や BME680 専用ライブラリは使わず、ESP32標準 + AHT20/BMP280 ライブラリのみで構成されています。

## ライセンス

このプロジェクトでTake-pachi-pachiが作成したソースコードは、ルートの
[LICENSE](LICENSE)に記載した独自の非商用ライセンスで公開します。私的利用、改変、
無償での共有は許可しますが、販売、広告収益を伴う利用、有償サービスや商用製品への
組み込みなどの商用利用は許可しません。商用利用にはTake-pachi-pachiの事前の書面に
よる許可が必要です。再配布時はライセンス文と著作権表示を保持してください。

PlatformIOで取得するU8g2、Adafruit AHTX0、Adafruit BMP280 Library、Adafruit BusIO、
Adafruit Unified Sensor、Adafruit GFX Library、Adafruit SH110X、およびESP32 Arduino
frameworkは、それぞれの著作権表示とライセンス条件に従います。依存ライブラリの一覧と
注意事項は[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)にまとめています。

ライセンス対象はTake-pachi-pachiが作成した自作コードです。第三者ライブラリや
ESP32 Arduino frameworkには、このプロジェクトの非商用条件は適用されません。
