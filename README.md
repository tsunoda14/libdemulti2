# libdemulti2: BCAS復号ライブラリ

ソフトウェアでBCASのデスクランブル(MULTI2の復号)を行うためのライブラリ。  
TSパケットをデスクランブルするには、ECMを復号してスクランブルキーK\_sを取得・更新しておく必要があるが、  
そのための手段やAPIとして、以下の三つのバックエンドから選択できる。

(環境変数DEMULTI2\_MODEで指定できるが、指定のない場合は依存ライブラリを検出して自動的に選択される。)


## ECM復号の内部モード

1. DEMULTI2\_MODE = [yakisoba](../libyakisoba/): ソフトウェアのみ、独自API
2. DEMULTI2\_MODE = [sobacas](../libsobacas/): ソフトウェアのみ、PC/SCのAPIを提供するラッパーライブラリを利用
3. DEMULTI2\_MODE = pcsc: BCASカード・ICカードリーダを使用。PC/SCのAPI

環境変数による指定がない場合は、上記の順で検出・選択される。

### `DEMULTI2_MODE == 'yakisoba'`

```
              +----------------------+
              |   Client App.        |
              +---+------------+-----+
                  |  descramble|  ^
     feed_ecm(ECM)|    (TS_pkt)|  |平文TS_pkt
                  v            v  |
libdemulti2   +---+------------+--+--+
              |   |   K_s ---> 復号  |
              +---+--+---------------+
                  |  ^
   decode_ECM(ECM)|  |
                  v  |
libyakisoba   +---+--+---------------+
              |   復号 <-- K_w       |
              +----------------------+
```
実行時の依存: libyakisoba.so.0 (+設定ファイル)

### `DEMULTI2_MODE == 'sobacas'`

```
              +----------------------+
              |   Client App.        |
              +---+------------+-----+
                  |  descramble|  ^
     feed_ecm(ECM)|    (TS_pkt)|  |平文TS_pkt
                  v            v  |
libdemulti2   +---+------------+--+--+
              |   |   K_s ---> 復号  |
              +---+--+---------------+
                  |  ^
     SCardTransmit|  |
            (ECM) v  |
PC/SC         +---+--+---------------+
              |   | libsobacas       |
              +---+--^---------------+
                  |  ^
   decode_ECM(ECM)|  |
                  v  |
libyakisoba   +---+--+---------------+
              |   復号 <-- K_w       |
              +----------------------+
```
実行時の依存: libsobacas.so.0, libyakisoba.so.x (+設定ファイル)

### `DEMULTI2_MODE == 'pcsc'`

```
              +----------------------+
              |   Client App.        |
              +---+------------+-----+
                  |  descramble|  ^
     feed_ecm(ECM)|    (TS_pkt)|  |平文TS_pkt
                  v            v  |
libdemulti2   +---+------------+--+--+
              |   |   K_s ---> 復号  |
              +---+--+---------------+
                  |  ^
     SCardTransmit|  |
            (ECM) v  |
PC/SC         +---+--+---------------+
              |   | libpcsclite      |
              +---+--^---------------+
                  |  |
              +---v--+---------------+
              |   | pcscd            |
              |   v  |(ccid)         |
              +---+--^---------------+
                  |  |
              +---v--+---------------+
              |   | CardReader       |
              +---+--^---------------+
                  |  |
              +---v--+---------------+
              |     BCas Card (K_w)  |
              +----------------------+
```
実行時の依存: libpcsclite.so.1, ccidドライバ, pcscdサービスの起動、 カードリーダ・BCASカード

## 使い方

```c
#include <demulti2.h>

h = demulti2_open();

loop {

   // TS パケットの読み込み、PSIセクションの組み立て etc...

   // buf[in]: ECMボディ
   demulti2_feed_ecm(h, buf, len, ecm_pid);

   // src[in]: TSパケットのペイロード, dst[out]: 平文ペイロード
   demulti2_descramble(h, src, len, ts_pkt[3], ecm_pid, dst);
   /* または
    * demulti2_descramble(h, src, len, ts_pkt[3], ecm_pid, NULL); // in-place復号
    */

}

demulti2_close(h);
```

## ビルド

```
$ git clone [--depth 1] https://github.com/.../libdemulti2
$ cd libdemulti2; autoreconf -i
$ mkdir build; cd build
$ ../configure [--preifx=...]
$ make
$ sudo make install
```
