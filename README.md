NiosII を用いた MicroBridge
===========================

概要
----
Arduino 用 USB Host Shield と Altera FPGA 上に形成した CPU コア NiosII を用いて
MicroBridge を実現します。

動作確認環境
------------
* DE0-Nano
* USB Host Shield (DEV-09628 を改造。後述)
* QuartusII 11.0sp1 Web Edition
  * VerilogHDL
  * NiosII/e (SOPC Builder)
* HTC Desire (Android 2.2)
  * 本家 MicroBridge のソースに含まれるデモ用 Android アプリ

対応 FPGA 基板
--------------
基本的に 3.3V I/O で USB Host Shield と接続でき、
NiosII 用に 16KByte 以上の RAM を確保できれば動作可能。

USB Host Shield との接続
------------------------
USB Host Shield と DE0-Nano 等の 40pin ピンヘッダとの接続は以下のようにしています。
GPX は接続しなくても動きます。

               +------+
       (NC) ---| 1   2|--- SCLK
       (NC) ---| 3   4|--- MISO
         SS ---| 5   6|--- MOSI
       (NC) ---| 7   8|--- INT
        RST ---| 9  10|--- GPX
        +5V ---|11  12|--- GND
       (NC) ---|13  14|--- (NC)
                ・  ・ 
                ・  ・ 
                ・  ・ 

USB Host Shield の電源は未改造では Vin に 7V 程度を供給する必要があります。
DE0-Nano との動作確認では
USB Host Shield の Vin→5V のレギュレータ LM1117 を除去し、
DE0-Nano からの 5V を 5V ラインに供給しています。


ディレクトリ構造
----------------
* mb_de0nano/ .... DE0-Nano での構成 (Verilog 等)
  * software/ .... DE0-Nano での構成 (ソフトウェア部分)
    * mb_de0nano_cpp_bsp/ .... C++ 版 BSP
    * mb_de0nano_cpp_demo/ .... C++ 版 デモアプリ
    * mb_de0nano_c_bsp/ .... C 版 BSP
    * mb_de0nano_c_demo/ .... C 版 デモアプリ

