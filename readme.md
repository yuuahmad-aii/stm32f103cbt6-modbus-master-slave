# STM32F103CBT6 Modbus Master-Slave Project

Proyek ini mengimplementasikan komunikasi **Modbus RTU** antara dua microcontroller STM32F103CBT6 yang saling terhubung melalui interface **RS485**. Salah satu board berperan sebagai Master dan yang lain sebagai Slave.

## 📋 Daftar Isi

- [Gambaran Umum](#gambaran-umum)
- [Fitur Utama](#fitur-utama)
- [Konfigurasi Hardware](#konfigurasi-hardware)
- [Arsitektur Master](#arsitektur-master)
- [Arsitektur Slave](#arsitektur-slave)
- [Protokol Komunikasi](#protokol-komunikasi)
- [Finite State Machine Master](#finite-state-machine-master)
- [Konfigurasi Pin](#konfigurasi-pin)
- [Cara Membangun](#cara-membangun)
- [Cara Menggunakan](#cara-menggunakan)

---

## 🎯 Gambaran Umum

Proyek ini mendemonstrasikan implementasi **Modbus RTU** yang non-blocking dan efisien menggunakan DMA (Direct Memory Access). Komunikasi dilakukan melalui UART3 dengan RS485 transceiver (MAX485), memungkinkan two-way communication antara Master dan Slave.

**Karakteristik Utama:**

- ✅ Komunikasi Modbus RTU 115200 baud
- ✅ Non-blocking menggunakan state machine (Master)
- ✅ Event-driven processing (Slave)
- ✅ DMA untuk transmisi/penerimaan data
- ✅ CRC-16 validation
- ✅ Kontrol LED dan pembacaan button via Modbus

---

## ✨ Fitur Utama

### Master

- **Fungsi Control (FC 05):** Mengirim perintah ke Slave untuk mengontrol LED-nya
- **Fungsi Read (FC 02):** Membaca status tombol pada Slave
- **State Machine:** Mengelola sequence komunikasi dengan delay yang optimal
- **Non-blocking:** Program utama 100% non-blocking, semua proses komunikasi driven by DMA

### Slave

- **Fungsi Write Coil (FC 05):** Menerima perintah dari Master untuk mengontrol LED lokal
- **Fungsi Read Input (FC 02):** Mengirimkan status tombol lokal kepada Master
- **Event-driven:** Hanya memproses data Modbus ketika frame diterima
- **Validasi CRC:** Memastikan integritas data sebelum berproses

---

## 🔌 Konfigurasi Hardware

### Komponen yang Digunakan

- **2x STM32F103CBT6** Development Board
- **2x MAX485 RS485 Transceiver Module**
- **Kabel RS485** (A, B, GND)
- **LED** (dengan resistor)
- **Push Button** (dengan pull-down resistor, aktivasi LOW)

### Koneksi RS485

```
Master MAX485     Slave MAX485
   A     --------- A
   B     --------- B
   GND   --------- GND
   DE,RE --------- DE,RE (control pin terpisah)
```

---

## 🎮 Arsitektur Master

**File:** `f103cbt6-modbus-master/Core/Src/main.c`

### Ringkasan Kerja Master

```mermaid
graph TD;
    A[STATE_IDLE (100ms)] --> B{STATE_SEND_FC05 (Write Coil - Kontrol LED Slave)};
    B --> C{STATE_WAIT_FC05 (Tunggu balasan: 50ms timeout)};
    C --> D{STATE_DELAY_FC02 (Jeda 5ms untuk stabilitas)};
    D --> E{STATE_SEND_FC02 (Read Input - Baca tombol Slave)};
    E --> F{STATE_WAIT_FC02 (Tunggu balasan: 50ms timeout)};
    F --> A;
````

### Fungsi Utama Master

#### 1. **CRC-16 Modbus**
```c
uint16_t Modbus_CRC16(uint8_t *buf, uint8_t len)
````

- Menghitung checksum CRC-16 sesuai standar Modbus RTU
- Digunakan untuk validasi integritas frame

#### 2. **RS485 Send via DMA**

```c
void RS485_Send_DMA(uint8_t *data, uint16_t len)
```

- Mengatur MAX485 ke mode transmit (DE/RE HIGH)
- Mengirim data menggunakan DMA (non-blocking)

#### 3. **State Machine**

| State              | Deskripsi                                       |
| ------------------ | ----------------------------------------------- |
| `STATE_IDLE`       | Menunggu 100ms sebelum mengirim FC05            |
| `STATE_SEND_FC05`  | Membuat frame FC05 (Write Coil) dan mengirimnya |
| `STATE_WAIT_FC05`  | Menunggu respons Slave (timeout 50ms)           |
| `STATE_DELAY_FC02` | Jeda aman 5ms antara dua frame                  |
| `STATE_SEND_FC02`  | Membuat frame FC02 (Read Input) dan mengirimnya |
| `STATE_WAIT_FC02`  | Menunggu respons Slave (timeout 50ms)           |

#### 4. **Callback DMA**

- **`HAL_UART_TxCpltCallback()`:** Dipanggil saat TX DMA selesai

  - Mengatur MAX485 ke mode receive (DE/RE LOW)
  - Memulai listening untuk menerima respons

- **`HAL_UARTEx_RxEventCallback()`:** Dipanggil saat RX mendeteksi IDLE line (frame lengkap)
  - Menyimpan ukuran frame ke `rx_length`
  - Set flag `rx_complete_flag`

### FC05: Write Single Coil (Kontrol LED Slave)

**Frame Request (8 bytes):**

```
Byte 0: Slave ID (0x01)
Byte 1: Function Code (0x05)
Byte 2-3: Coil Address (0x0000)
Byte 4-5: Coil Value (0xFF00 = ON, 0x0000 = OFF)
Byte 6-7: CRC-16
```

### FC02: Read Discrete Inputs (Baca Tombol Slave)

**Frame Request (8 bytes):**

```
Byte 0: Slave ID (0x01)
Byte 1: Function Code (0x02)
Byte 2-3: Start Address (0x0000)
Byte 4-5: Quantity of Inputs (0x0001)
Byte 6-7: CRC-16
```

**Frame Response (6 bytes):**

```
Byte 0: Slave ID (0x01)
Byte 1: Function Code (0x02)
Byte 2: Byte Count (0x01)
Byte 3: Input Status (0x01 = pressed, 0x00 = not pressed)
Byte 4-5: CRC-16
```

---

## 🖥️ Arsitektur Slave

**File:** `f103cbt6-modbus-slave/Core/Src/main.c`

### Ringkasan Kerja Slave

```mermaid
graph TD;
    A[STATE_IDLE (100ms)] --> B{Listening untuk frame Modbus};
    B --> C{Frame diterima (IDLE line detected)};
    C --> D{Validasi ID Slave dan CRC};
    D --> E{FC05: Write Coil (Ubah LED Slave)};
    D --> F{FC02: Read Input (Kirim status tombol)};
    F --> A{Enable RX DMA ulang};
````

### Fungsi Utama Slave

#### 1. **CRC-16 Modbus**

Sama seperti Master - menghitung checksum frame

#### 2. **RS485 Send via DMA**

Sama seperti Master - mengirim data via DMA

#### 3. **Event Processing**

Slave menggunakan flag `process_modbus_flag` yang diset oleh callback `HAL_UARTEx_RxEventCallback()` saat frame lengkap diterima.

**Pseudo-code:**

```
IF process_modbus_flag == 1:
    Validasi: rx_length >= 8 dan Slave ID match
    Validasi: CRC valid

    IF FC == 0x05 (Write Coil):
        Ubah output LED sesuai nilai
        Echo frame kembali ke Master

    ELSE IF FC == 0x02 (Read Input):
        Baca status tombol lokal
        Buat response frame dengan status tombol
        Kirim response ke Master

    Reset flag dan dengarkan frame berikutnya
```

#### 4. **Callback DMA**

- **`HAL_UARTEx_RxEventCallback()`:** Dipanggil saat frame lengkap diterima

  - Set `process_modbus_flag = 1`
  - Update `rx_length` dengan ukuran frame

- **`HAL_UART_TxCpltCallback()`:** Dipanggil saat TX DMA selesai
  - Kembali ke mode receive (DE/RE LOW)
  - Mulai listening untuk frame berikutnya

---

## 📡 Protokol Komunikasi

### Standar: Modbus RTU

**Karakteristik:**

- **Baud Rate:** 115200 bps
- **Data Bits:** 8
- **Stop Bits:** 1
- **Parity:** None
- **CRC:** CRC-16 (polynomial 0xA001)
- **Inter-message delay:** Min 3.5 character times

### Format Frame Modbus RTU

```
┌──────────────────────────────────────────────┐
│      MODBUS RTU FRAME FORMAT                │
├──────────────────────────────────────────────┤
│ Slave ID   │ Function │ Data... │ CRC-16    │
│ (1 byte)   │ (1 byte) │ (varies)│ (2 bytes) │
└──────────────────────────────────────────────┘
```

**Deteksi End-of-Frame:**

- Menggunakan **UART Idle Line Detection** via DMA
- `HAL_UARTEx_ReceiveToIdle_DMA()` mendeteksi jeda 3.5+ character times
- Callback `HAL_UARTEx_RxEventCallback()` dipicu ketika frame lengkap

---

## 🔄 Finite State Machine Master

### Diagram Transisi State

```
                        ┌─ (timeout atau RX ──┐
                        │  tidak complete)     │
      ┌─────────────────┴──────────────┐       │
      │                               │        │
      ↓                               ↑        │
   STATE_IDLE ──(timer 100ms)──> STATE_SEND_FC05
      ▲                               │
      │                               ↓
      │                          STATE_WAIT_FC05
      │                               │
      └──────(RX complete atau        ↓
             timeout 50ms)──> STATE_DELAY_FC02
      ▲                               │
      │                               ↓
      └──(timeout 50ms)──> STATE_SEND_FC02 ──────┐
      │                               │           │
      │                               ↓           │
      └────────────── STATE_WAIT_FC02 ◄──────────┘
                          │
                          ↓ (RX complete atau
                          │  timeout 50ms)
                          │
                          ├──> Back to STATE_IDLE
```

### Penjelasan Setiap State

1. **STATE_IDLE** (100ms)

   - Menunggu interval timer selama 100ms
   - Ketika timer habis, transisi ke FC05

2. **STATE_SEND_FC05**

   - Format frame: `[ID][FC05][ADDR][VALUE][CRC]`
   - Kirim via DMA
   - Transisi ke STATE_WAIT_FC05

3. **STATE_WAIT_FC05**

   - Menunggu respons Slave (timeout 50ms)
   - Jika RX complete atau timeout → STATE_DELAY_FC02

4. **STATE_DELAY_FC02** (5ms jeda)

   - Memberikan jeda aman antara dua frame
   - Memastikan Slave siap menerima frame berikutnya

5. **STATE_SEND_FC02**

   - Format frame: `[ID][FC02][ADDR][COUNT][CRC]`
   - Kirim via DMA
   - Transisi ke STATE_WAIT_FC02

6. **STATE_WAIT_FC02**
   - Menunggu respons Slave (timeout 50ms)
   - Jika RX complete atau timeout → kembali ke STATE_IDLE

### Interval Komunikasi

| Operasi                     | Durasi |
| --------------------------- | ------ |
| Cycle lengkap (FC05 + FC02) | ~260ms |
| Timeout tunggu respons      | 50ms   |
| Jeda inter-frame            | 5ms    |
| Main loop update            | ~100ms |

---

## 📍 Konfigurasi Pin

### Master dan Slave (sama)

| Pin  | Port  | Fungsi                    | Mode               |
| ---- | ----- | ------------------------- | ------------------ |
| PA8  | GPIOA | USART3_SEL (MAX485 DE/RE) | Output PP          |
| PB0  | GPIOB | USER_LED                  | Output PP          |
| PB1  | GPIOB | USER_BTN                  | Input (Pull-Down)  |
| PB10 | GPIOB | USART3_TX                 | Alternate Function |
| PB11 | GPIOB | USART3_RX                 | Alternate Function |

### UART Configuration

- **UART Peripheral:** USART3
- **Baud Rate:** 115200 bps
- **Word Length:** 8 bits
- **Stop Bits:** 1
- **Parity:** None
- **Mode:** TX/RX
- **Hardware Flow Control:** None
- **DMA:** Enabled untuk TX dan RX

### DMA Configuration

- **DMA Controller:** DMA1
- **RX Channel:** DMA1_Channel3 (USART3_RX)
- **TX Channel:** DMA1_Channel2 (USART3_TX)
- **Transfer Mode:** Circular untuk RX (ReceiveToIdle)

---

## 🔨 Cara Membangun

### Prerequisites

- STM32CubeIDE atau arm-gcc toolchain
- STM32CubeMX (untuk generate kode jika diperlukan)
- OpenOCD (untuk flashing)

### Langkah-langkah Kompilasi

#### Menggunakan STM32CubeIDE

1. **Buka Master Project:**

   ```bash
   cd f103cbt6-modbus-master
   ```

   - Import project ke STM32CubeIDE
   - Build: `Project → Build Project`

2. **Buka Slave Project:**
   ```bash
   cd ../f103cbt6-modbus-slave
   ```
   - Import project ke STM32CubeIDE
   - Build: `Project → Build Project`

#### Menggunakan Command Line

```bash
# Master
cd f103cbt6-modbus-master
make -j4

# Slave
cd ../f103cbt6-modbus-slave
make -j4
```

### Flashing ke Board

#### Menggunakan STM32CubeIDE

1. Sambungkan board via ST-Link debugger
2. Klik Run atau Debug button
3. Pilih ST-Link GDB Server

#### Menggunakan OpenOCD

```bash
# Terminal 1: Start OpenOCD
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg

# Terminal 2: Flash Master
arm-none-eabi-gdb Debug/f103cbt6-modbus-master.elf
(gdb) target remote localhost:3333
(gdb) load
(gdb) continue

# Terminal 2: Flash Slave (ganti dengan Slave firmware)
arm-none-eabi-gdb Debug/f103cbt6-modbus-slave.elf
(gdb) target remote localhost:3333
(gdb) load
(gdb) continue
```

---

## 🚀 Cara Menggunakan

### Wiring Fisik

```
┌─────────────────┐          RS485 BUS          ┌─────────────────┐
│   MASTER        │                             │     SLAVE       │
│ (STM32F103)     │                             │ (STM32F103)     │
│                 │                             │                 │
│ PA8 (DE/RE) ────┼────────────                 │ PA8 (DE/RE) ────┤
│                 │           │                 │                 │
│ PB10 (USART_TX) ┼─→ MAX485  │                 │ PB10 (USART_TX) │
│                 │           ├─── A ─────────→ ├─→ MAX485        │
│                 │           │                 │     ├─ A ───┐   │
│ PB11 (USART_RX) ┼← MAX485   │                 │     │       │   │
│                 │           ├─── B ─────────→ ├─← MAX485     │   │
│                 │           │                 │     └─ B ───┤   │
│ PB11 (USART_RX) ┼← MAX485   │    B     │               │   │
│                 │           ├────── ─────────→ ├─← MAX485   │   │
│                 │           │                 │     │───────┘   │
│ GND             ┼─ MAX485   │                 │ GND            │
│                 │           ├─── GND ────────→ ┼─ MAX485        │
│                 │           │                 │                 │
│ PB0 (LED)      ┼─ 470Ω ─ ─┤                 │ PB0 (LED)       │
│                 │           ├───┬──────────┐  │     ├───┬──────┐│
│                 │           │   │ GND  │   │  │     │   │ GND ││
│                 │           │   └──────┘   │  │     │   └─────┘│
│                 │           │              │  │     │          │
│ +3.3V ─────────┬┴─ Button ──┤              │  └─────┴─ Button─ ┤
│                 │            │              │        ├─ +3.3V  │
│ GND            ┼────────────┬┘              │ GND ────┘         │
│                 │            │              │                   │
└─────────────────┘            └──────────────┴───────────────────┘
```

### Operasi

1. **Power On**

   - Sambungkan power supply ke kedua board
   - LED User akan OFF (initial state)

2. **Tekan Button Master**

   - LED Master akan menyala
   - Setelah ~100ms, perintah FC05 dikirim ke Slave
   - LED Slave akan menyala (respons terhadap perintah Master)

3. **Tekan Button Slave**

   - LED Slave akan menyala
   - Master membaca status button Slave via FC02
   - LED Master akan menyala (respons terhadap status button Slave)

4. **Release Button**
   - Proses berulang dengan update yang terus menerus

### LED Behavior

| Kondisi                | Master LED     | Slave LED      |
| ---------------------- | -------------- | -------------- |
| Button Master Pressed  | ON             | ON (via FC05)  |
| Button Master Released | OFF            | OFF (via FC05) |
| Button Slave Pressed   | ON (via FC02)  | ON             |
| Button Slave Released  | OFF (via FC02) | OFF            |

---

## 🔍 Debug & Troubleshooting

### Komunikasi Tidak Terjalin

**Masalah:** LED tidak menyala walaupun button ditekan

**Solusi:**

1. Cek koneksi RS485 (A, B, GND)
2. Cek baud rate (harus 115200)
3. Verifikasi pin PA8 (DE/RE) tersaksi dengan baik
4. Gunakan logic analyzer untuk confirm timing

### CRC Error

**Masalah:** Frame diterima tapi CRC fail

**Solusi:**

1. Verifikasi algoritma CRC-16
2. Cek integrity frame saat transmisi
3. Cek ground loop pada RS485 bus

### Timeout Communication

**Masalah:** State machine stuck di STATE_WAIT_FC05 atau STATE_WAIT_FC02

**Solusi:**

1. Cek apakah Slave aktif dan dapat berkomunikasi
2. Verifikasi parameter timeout (50ms)
3. Reset board Slave dan coba ulang

---

## 📊 Frame Examples

### Master → Slave: FC05 (Kontrol LED)

```
REQUEST:
[01] [05] [00] [00] [FF] [00] [CD] [CA]
 ID   FC   ADDR(H) ADDR(L) VALUE(H) VALUE(L) CRC(L) CRC(H)

Arti:
- ID: 0x01 (Slave 1)
- FC: 0x05 (Write Single Coil)
- Address: 0x0000 (Coil 0)
- Value: 0xFF00 (ON - 65280)
- CRC: 0xCACD

RESPONSE (Echo):
[01] [05] [00] [00] [FF] [00] [CD] [CA]
```

### Master → Slave: FC02 (Baca Button)

```
REQUEST:
[01] [02] [00] [00] [00] [01] [39] [44]
 ID   FC   ADDR(H) ADDR(L) COUNT(H) COUNT(L) CRC(L) CRC(H)

Arti:
- ID: 0x01 (Slave 1)
- FC: 0x02 (Read Discrete Inputs)
- Start Address: 0x0000 (Input 0)
- Quantity: 0x0001 (1 input)
- CRC: 0x4439

RESPONSE:
[01] [02] [01] [01] [80] [39]
 ID   FC  BYCNT DATA  CRC(L) CRC(H)

Arti:
- ID: 0x01
- FC: 0x02 (Response FC02)
- Byte Count: 0x01 (1 byte data)
- Data: 0x01 (Button pressed)
- CRC: 0x3980
```

---

## 📝 Lisensi

Project ini menggunakan library dan tools berikut:

- **STM32 HAL Library** (STMicroelectronics) - Included in project
- **CMSIS** (ARM) - Included in project
- **STM32CubeIDE** (free)

---

## 🤝 Kontribusi

Untuk bug reports atau improvements, silakan buat issue atau pull request.

---

## 📞 Kontak & Support

Jika ada pertanyaan atau membutuhkan bantuan:

- Baca komentar dalam code
- Periksa datasheet STM32F103CBT6
- Konsultasi dokumentasi Modbus RTU standard

---

## 🎓 Referensi

- **Modbus RTU Standard:** https://www.modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf
- **STM32F103 Reference Manual:** https://www.st.com/resource/en/reference_manual/cd00171190-stm32f103xx_reference_manual.pdf
- **MAX485 Datasheet:** https://datasheets.maximintegrated.com/en/ds/MAX485-MAX487.pdf

---

## 📌 Catatan Penting

- Semua komunikasi **non-blocking** menggunakan DMA
- State machine Master memastikan **consistent communication**
- Slave **event-driven** dan **low-latency**
- CRC validation pada **semua frame**
- Frame detection menggunakan **UART Idle Line** (robust)
- Inter-frame delay **5ms** untuk stabilitas

---

**Last Updated:** April 15, 2026  
**Version:** 1.0
