**Technical Information MB001_ASW GEN-Modbus-en_V2.1. 3**

## Technical Information

## AISWEI Interface

```
(Based On Modbus Standard Protocol)
```

**MB001_ASW GEN-Modbus-en_V2.1. 3 Technical Information**

```
Version Comments Author
1.0 First edition Jincheng Wang
```
2. 1 Upgrade to 2. 1 version Jeff.Ji
2.1.1 Upgrade to 2.1.1 version Jeff.Ji
2.1.2 Upgrade to 2.1.2 version Guohao.Li
2 .1.3 Upgrade to 2.1. 3 version Shaochen.Mao

```
2 .1. 4 Upgrade to 2.1.3 version: add the ct
data
```
```
Zhengjian.zhou
```

**Technical Information MB001_ASW GEN-Modbus-en_V2.1. 3**

## Legal Provisions

The information contained in these documents is property of AISWEI Technology Co., Ltd. Any
publication, whether in whole or in part, requires prior written approval by AISWEI Technology
Co., Ltd. Internal reproduction used solely for the purpose of product evaluation or other
proper use is allowed and does not require prior approval.

‘Modbus’ herein refers to industrial standard serial communication protocol.

**AISWEI Technology Co., Ltd**

Room 905B, 757 Mengzi Road, Huangpu District

200023 Shanghai

P.R. China

Tel. +86 512 6937 0998

Fax +86 512 6937 3159
[http://www.aiswei-tech.com](http://www.aiswei-tech.com)

E-Mail：service.china@aiswei-tech.com

© 202 2 AISWEI Technology Co., Ltd. All rights reserved.


## MB001_ASW GEN-Modbus-en_V2.1. 3 Technical Information

- 1 Information on this Document Table of Contents
- 2 Safety
   - 2.1 Intended Use
   - 2.2 Skills of Qualified Persons
- 3 AISWEI Modbus Profile
   - 3.1 Information on the Assignment Tables
   - 3.2 AISWEI Data Types and NaN Values
   - 3.3 AISWEI Modbus Profile – Register Overview.............................................
   - 3.4 Warning and Error Codes
   - 3.5 Grid Codes
   - 3.6 Frame format
      - 3.6.1 Read Holding Register (Function Code: 0x03)
      - 3.6.2 Read Input Register (Function Code: 0x04)
      - 3.6.3 Write Single Holding Register (Function Code: 0x06)
      - 3.6.4 Write Multiple Holding Registers (Function Code: 0x10)
      - 3.6.5 Write Multiple Holding Registers (Function Code: 0x10) for broadcast
      - 3.6.6 Exception Codes
- 4 Contact


**Technical Information 1 MB001_ASW GEN-Modbus-en_V2.1. 3**

## 1 Information on this Document

**Validity**

This document is valid for AISWEI inverters.

**Target Group**

This document is intended for qualified persons. Only persons with appropriate skills are al-
lowed to perform the tasks described in this document.

**Terminology**

```
Information Explanation
```
```
Pn The rated active power of device
```
```
Pm The instantaneous power when the power
control curve reaches the starting point
```
```
Sn The rated apparent power of device
```

**MB001_ASW GEN-Modbus-en_V2.1. 3 2 Technical Information**

## 2 Safety

### 2.1 Intended Use

The Modbus interface of the supported devices is designed for industrial use, via RS485 or
RS422 protocol to enable remote control of the PV system, remote querying of values, and
remote parameter setting.

### 2.2 Skills of Qualified Persons

The activities described in this document must only be performed by qualified persons.
Qualified persons must have the following skills:

- Detailed knowledge of the grid management services
- Knowledge of IP-based network protocols
- Training in the installation and configuration of IT systems
- Knowledge of the Modbus specifications
- Knowledge of and compliance with this document and all safety information


**Technical Information 3 MB001_ASW GEN-Modbus-en_V2.1. 3**

## 3 AISWEI Modbus Profile

### 3.1 Information on the Assignment Tables

The assignment tables of the AISWEI Modbus profile present the following information:

```
Information Explanation
```
```
ADR (DEC) Decimal Modbus address, you need to re-
move 3x or 4x and subtract 1, then convert
to hexadecimal and use it in the communica-
tion frame. Such as 31001 (decimal) → 1000
(decimal) → 0x03e8 (hexadecimal)
```
```
Description/
number code(s)
```
```
Short description of the Modbus registers
and the number codes used.
```
```
Type Type of the data (see Section 3.2).
```
```
Unit Unit of the data.
```
```
Gain Real value = Gain * output value
```
```
Access RO: Read Only
RW: Read and Write
WO: Write Only
```

**MB001_ASW GEN-Modbus-en_V2.1. 3 4 Technical Information**

### 3.2 AISWEI Data Types and NaN Values

The following table shows the data types used in the AISWEI Modbus profile and the possible
NaN values. The AISWEI data types are listed in the assignment tables in the Type column.
They describe the data widths of the assigned values:

```
Type Description NaN Value
```
```
B16 Bit field ( 16 - bit） 0xFFFF
```
```
B32 Bit field (32-bit） 0xFFFF FFFF
```
```
S16 Signed integer (16-bit） 0x
```
```
U16 Unsigned integer (16-bit） 0xFFFF
```
```
S32 Signed integer ( 32 - bit） 0x8000 0000
```
```
U32 Unsigned integer (32-bit） 0xFFFF FFFF
```
```
E16 Number code（ 16 - bit） 0xFFFF
```
```
String String type (16-bit, combination of two 8-bit ASCII charac-
ters, the high 8 - bit is the first ASCII character, and the low
8 - bit is the second ASCII character)
```
```
0x
```

**Technical Information 5 MB001_ASW GEN-Modbus-en_V2.1. 3**

### 3.3 AISWEI Modbus Profile – Register Overview.............................................

In the following table you will find all the measured values and parameters of the AISWEI
Modbus Profile.

**Input Registers**

```
ADDR(DEC) Description/number code Type Unit Gain Access
```
```
31001 Device Type:^
1=Single phase / 3=Three pahse
```
```
String - - RO
```
```
31002 Modbus address：Default as 3 U16 - - RO
31 003~31018 Serial Number String - - RO
```
##### 31019~

```
Machine type: for example,
"ASW3000", please refer to the
specific machine in practice
```
```
String - - RO
```
##### 31027

```
Current grid code: refer to Section
3.5 E16^ -^ -^ RO^
31028~31029 Rated Power U32 W 1.0 RO
31030~31036 Master Software Version String - - RO
31037~31043 Slave Software Version String - - RO
31044~31050 Safety Version String - - RO
```
##### 31057~

```
Manufacturer's name: for example,
"AISWEI", refer to the specific ma-
chine
```
```
String - - RO
```
##### 31065~

```
Brand name: for example,
"AISWEI", please refer to the spe-
cific machine
```
```
String - - RO
```
```
31301 Grid rated voltage U16 V 0.1 RO
31302 Grid rated frequency U16 Hz 0.01 RO
31303~31304 E-Today of inverter U32 kWh 0.1 RO
31305~31306 E-Total of inverter U32 kWh 0.1 RO
31307~31308 H-Total U32 H 1.0 RO
```
##### 31309

```
Device State:
0 = Wait
1 = Normal
```
##### E16 - - RO


**MB001_ASW GEN-Modbus-en_V2.1. 3 6 Technical Information**

```
2 = Fault
4 = Checking
31310 Connect time U16 s 1.0 RO
31311 Air temperature S16 °C 0.1 RO
31312 Inverter U phase temperature S16 °C 0.1 RO
31313 Inverter V phase temperature S16 °C 0.1 RO
31314 Inverter W phase temperature S16 °C 0.1 RO
31315 Boost temperature S16 °C 0.1 RO
```
```
31316
```
```
Bidirectional DC/DC Converter tem-
perature(*) S16^ °C^ 0.1^ RO^
31317 Bus voltage U16 V 0.1 RO
31319 PV1 voltage U16 V 0.1 RO
31320 PV1 current U16 A 0.01 RO
31321 PV2 voltage U16 V 0.1 RO
31322 PV2 current U16 A 0.01 RO
31323 PV3 voltage(*) U16 V 0.1 RO
31324 PV3 current(*) U16 A 0.01 RO
31325 PV4 voltage(*) U16 V 0.1 RO
31326 PV4 current(*) U16 A 0.01 RO
31327 PV5 voltage(*) U16 V 0.1 RO
31328 PV5 current(*) U16 A 0.01 RO
31329 PV6 voltage(*) U16 V 0.1 RO
31330 PV6 current(*) U16 A 0.01 RO
31331 PV7 voltage(*) U16 V 0.1 RO
31332 PV7 current(*) U16 A 0.01 RO
31333 PV8 voltage(*) U16 V 0.1 RO
31334 PV8 current(*) U16 A 0.01 RO
31335 PV9 voltage(*) U16 V 0.1 RO
31336 PV9 current(*) U16 A 0.01 RO
31337 PV10 voltage(*) U16 V 0.1 RO
31338 PV10 current(*) U16 A 0.01 RO
31339 String 1 current(*) U16 A 0.1 RO
31340 String 2 current(*) U16 A 0.1 RO
```

**Technical Information 7 MB001_ASW GEN-Modbus-en_V2.1. 3**

```
31341 String 3 current(*) U16 A 0.1 RO
31342 String 4 current(*) U16 A 0.1 RO
31343 String 5 current(*) U16 A 0.1 RO
31344 String 6 current(*) U16 A 0.1 RO
31345 String 7 current(*) U16 A 0.1 RO
31346 String 8 current(*) U16 A 0.1 RO
31347 String 9 current(*) U16 A 0.1 RO
31348 String 10 current(*) U16 A 0.1 RO
31349 String 11 current(*) U16 A 0.1 RO
31350 String 12 current(*) U16 A 0.1 RO
31351 String 13 current(*) U16 A 0.1 RO
31352 String 14 current(*) U16 A 0.1 RO
31353 String 15 current(*) U16 A 0.1 RO
31354 String 16 current(*) U16 A 0.1 RO
31355 String 17 current(*) U16 A 0.1 RO
31356 String 18 current(*) U16 A 0.1 RO
31357 String 19 current(*) U16 A 0.1 RO
31358 String 20 current(*) U16 A 0.1 RO
31359 L1 Phase voltage U16 V 0.1 RO
31360 L1 Phase current U16 A 0.1 RO
31361 L2 Phase voltage(*) U16 V 0.1 RO
31362 L2 Phase current(*) U16 A 0.1 RO
31363 L3 Phase voltage(*) U16 V 0.1 RO
31364 L3 Phase current(*) U16 A 0.1 RO
31365 RS Line voltage(*) U16 V 0.1 RO
31366 RT Line voltage(*) U16 V 0.1 RO
31367 ST Line voltage(*) U16 V 0.1 RO
31368 Grid frequency U16 Hz 0.01 RO
31369~31370 Apparent power U32 VA 1.0 RO
31371~31372 Active power S 32 W 1.0 RO
31373~31374 Reactive power S32 Var 1.0 RO
31375 Power factor S16 - 0.01 RO
31377 Fault state of inverter itself： E16 - - RO
```

**MB001_ASW GEN-Modbus-en_V2.1. 3 8 Technical Information**

```
0 = No internal fault
1 = Internal fault
```
```
31378
```
```
Error message:please refer to sec-
tion 3.4 E16^ -^ -^ RO^
```
```
31379 Warning message:please refer to
section 3.
```
##### E16 - - RO

```
31601~31602 PV total power U32 W - RO
31603~31604 PV E-Today U32 kWh 0.1 RO
31605~31606 PV E-Total U32 kWh 0.1 RO
```
##### 31607

```
Battery communication status:
0x000A=Normal
0x0005=Error
```
##### E16 - - RO

##### 31608

```
Battery status:
0 = Not available
1 = Idle
2 = Charging
3 = Discharging
4 = Error
```
##### E16 - - RO

##### 31609

```
Battery Error Status 1:
bit0 communication data error 0-
valid 1-invalid
bit1 cell or module voltage is too
high 0-valid 1-invalid
bit2 Cell or module voltage is too
low 0-valid 1-invalid
bit3 battery temperature is too
high 0-valid 1-invalid
bit4 battery temperature is too low
0 - valid 1-invalid
bit5 discharging current over limit
0 - valid 1-invalid
bit6 charging current over limit 0-
valid 1-invalid
bit7 internal communication error
0 - valid 1-invalid
bit8 Internal cell voltage is unbal-
ance 0-valid 1-invalid
bit9 System insulation resistance is
too low 0-valid 1-invalid
bit10 voltage sensor failure 0-valid
1 - invalid
```
##### B16 - - RO


**Technical Information 9 MB001_ASW GEN-Modbus-en_V2.1. 3**

```
bit11 temperature sensor failure 0 -
valid 1-invalid
bit12 contactor failure 0-valid 1-in-
valid
bit13 Self-test failure during start-
ing 0-valid 1-invalid
bit14 IC self-test failure 0-valid 1-in-
valid
```
```
The default value of undefined bit
is 1
```
##### 31610

```
Battery Error Status 2:
bit0 Self-test failure for battery
voltage 0-valid 1-failure
bit1 Self-test failure for system
voltage 0-valid 1-invalid
bit2 Self-test failure for system in-
sulation resistance 0-valid 1-invalid
bit3 RTC invalid 0-valid 1-invalid
bit4 EEPROM failure 0 - valid 1-inva-
lid
bit5 Flash failure 0-valid 1-invalid
bit6 AFE invalid 0-valid 1-invalid
bit7 Chip failure for insulation re-
sistance dectect 0-valid 1-invalid
bit9 Chip failure for current sam-
pling 0-valid 1-invalid
bit10 HDC failure 0-valid 1-invalid
bit11 Daisy chain failure 0-valid 1-
invalid
bit12 Failure for precharge 0-valid
1 - invalid
```
```
The default value of undefined bit
is 1
```
##### B 16 - - RO

##### 31613

```
Battery warning status 1:
bit0 Communication data error 0-
valid 1-invalid
bit1 Battery or module voltage is
too high 0-valid 1-invalid
bit2 Battery or module voltage is
too low 0-valid 1-invalid
```
##### B16 - - RO


**MB001_ASW GEN-Modbus-en_V2.1. 3 10 Technical Information**

```
bit3 Battery temperature is too
high 0-valid 1-invalid
bit4 Battery temperature is too low
0 - valid 1-invalid
bit5 discharging current over limit
0 - valid 1-invalid
bit6 charging current over limit 0-
valid 1-invalid
bit7 Internal communication failure
0 - valid 1-invalid
bit8 Internal cell voltage is unbal-
ance 0-valid 1-invalid
```
```
The default value of undefined bit
is 1
31617 Battery voltage U16 V 0.01 RO
31618 Battery current S16 A 0.1 RO
31619~31620 Battery power S32 w 1 RO
31621 Battery temperature S16 °C 0.1 RO
31622 Battery SOC U16 - 0.01 RO
31623 Battery SOH U16 - 0.01 RO
31624 Battery charging current limit U16 A 0.1 RO
31625 Battery discharge current limit U16 A 0.1 RO
31626~31627 Battery E-Charge-Today U32 kWh 0.1 RO
31628~31629 Battery E-Discharge-Today U32 kWh 0.1 RO
31630~31631 E-Consumption-Today at AC side U32 kWh 0.1 RO
31632~31633 E-Generation-Today at AC side U32 kWh 0.1 RO
31634 EPS load voltage U16 V 0.1 RO
31635 EPS load current U16 A 0.1 RO
31636 EPS load frequency U16 Hz 0.01 RO
31637~31638 EPS load active power U32 w 1 RO
31639~31640 EPS load reactive power U32 Var 1 RO
```
```
31641~
```
```
E-Consumption-Today at EPS load
side U32^ kWh^ 0.1^ RO^
```
```
31643~
```
```
E-Consumption-Total at EPS load
side U32^ kWh^ 0.1^ RO^
31645 Phase 1 voltage for EPS Load U16 V 0.1 RO
```

**Technical Information 11 MB001_ASW GEN-Modbus-en_V2.1. 3**

```
31646 Phase 1 cuurent for EPS Load U16 A 0.1 RO
31647 Phase 2 voltage for EPS Load U16 V 0.1 RO
31648 Phase 2 cuurent for EPS Load U16 A 0.1 RO
31649 Phase 3 voltage for EPS Load U16 V 0.1 RO
31650 Phase 3 cuurent for EPS Load U16 A 0.1 RO
31651~31652 Phase 1 active power for EPS Load U32 w 1 RO
31653~31654 Phase 1 reactive power for EPS
Load
```
```
S32 Var 1 RO
```
```
31655~31656 Phase 2 active power for EPS Load U32 w 1 RO
31657~31658 Phase 2 reactive power for EPS
Load
```
```
S32 Var 1 RO
```
```
31659~31660 Phase 3 active power for EPS Load U32 w 1 RO
31661~31662 Phase 3 reactive power for EPS
Load
```
```
S32 Var 1 RO
```
```
31663~31664 Phase 1 active power for Grid U32 w 1 RO
31665~31666 Phase 1 reactive power for Grid S32 Var 1 RO
31667~31668 Phase 2 active power for Grid U32 w 1 RO
31669~31670 Phase 2 reactive power for Grid S32 Var 1 RO
31671~31672 Phase 3 active power for Grid U32 w 1 RO
31673~31674 Phase 3 reactive power for Grid S32 Var 1 RO
31675~31676 Energy charge today for Grid U32 kWh 0.1 RO
31677~31678 Energy charge total for Grid U32 kWh 0.1 RO
31679 Battery insulation resistance U16 kΩ 1 RO
31680 Battery charge/discharge cycles U16 - 1 RO
31681 Environment temperature U16 % 0.1 RO
```

**MB001_ASW GEN-Modbus-en_V2.1. 3 12 Technical Information**

**Holding register**

```
ADDR(DEC) Description/number
code
```
```
Type Unit Gain Access
```
##### 40201

```
Remote switch com-
mand:
0 = POWER OFF
1 = POWER ON
170 = Initialization status
```
##### E16 - - RW

```
41001 RTC:Year U16 - - RW
41002 RTC:Month U16 - - RW
41003 RTC:Day U16 - - RW
41004 RTC:Hour U16 - - RW
41005 RTC:Minute U16 - - RW
41006 RTC:Seconds U16 - - RW
```
##### 41102

```
Storage Inverter
Switch：
1 - OFF
2 - ON
```
##### E16 - - RW

##### 41103

```
Type selection of energy
storage machine：
0 - Invalid
1 - Energy storage ma-
chine
2 - Grid off inveter
3 - Grid connected in-
verter
4 - Force charge with City
electricity (battery wake-
up)
```
##### E16 - - RW

##### 41104

```
Run mode：
0 - Invalid
1 - Off
2 - Self generating self
use
3 - Backup power supply
4 - Customer defined
```
##### E16 - - RW

```
41105 Battery manufacturer： E16 - - RW
```

**Technical Information 13 MB001_ASW GEN-Modbus-en_V2.1. 3**

##### 1 - PYLON

##### 2 - DYNESS

##### 3 - BYD

##### 4 - LG

##### 5 - AISWEI

##### 41108

```
Smart meter status：
0x000A - Meter Online
0x0005 - Meter Offline
```
##### E16 - - RW

##### 41109

```
Smart meter adjustment
flag bit：
0x000A = Start
0x0005 = Stop
```
##### E16 - - RW

```
41110 ~41111 Set target power value S32 w 1 RW
```
```
41112 ~41113 Current power value of
smart meter
```
```
S32 w 1 RW
```
##### 41114

```
Anti reverse current
flag：
0x000A = ON
0x0005 = OFF
```
##### E16 - - RW

##### 41115

```
Battery wake-up (Force
charge) sign：
0x000A = ON
0x0005 = OFF
0xFFFF =Not triggered
```
##### E16 - - RW

##### 41116

```
UPS function：
0 = Enable EPS function
1 = Enable UPS function
```
##### U16 - - RW

##### 41151

```
Commbox and cloud
communication status：
0x000A = Cloud Online
0x0005 = Cloud Offline
0x00AF = Network not
configured
```
##### E16 - - RW

##### 41152

```
Charge discharge flag
bit：
1 - Stop
2 - Charging
3 - Discharge
```
##### E16 - - RW

##### 41153

```
Charge and discharge
power command：
‘ – ’- charging power
```
##### S16 W 1 RW


**MB001_ASW GEN-Modbus-en_V2.1. 3 14 Technical Information**

```
‘ + ’- discharge power
41154 Charging SOC upper limit U16 % 0.01 RW
41155 Discharge SOC lower limit U16 % 0.01 RW
```
```
41156 Obtaining power ratio of
power grid
```
##### U16 % 0.01 RW

##### 44001

```
Active power control
function：
0 = Disable
1 = Enable
```
##### E16 - - RW

##### 44002

```
EEG control function：
0 = Disable
1 = Enable
```
##### E16 - - RW

##### 44003

```
Slope load function：
0 = Disable
1 = Enable
```
##### E16 - - RW

##### 44004

```
Overvoltage reduce
power function：
0 = Disable
1 = Enable
```
##### E16 - - RW

##### 44005

```
Overfrequency reduce
power function：
0 = Disable
1 = Enable
```
##### E16 - - RW

##### 44006

```
Reactive power control
fucntion：
0 = Disable
1 = Enable
```
##### E16 - - RW

##### 44007

```
LVRT Function：
0 = Disable
1 = Enable
```
##### E16 - - RW

##### 44008

```
HVRT Function：
0 = Disable
1 = Enable
```
##### E16 - - RW

##### 44009

```
10 Minutes Average
Overvoltage protect
fucntion
0 = Disable
1 = Enable
```
##### E16 - - RW

```
44010 Islanding protect func-
tion：
```
##### E16 - - RW


**Technical Information 15 MB001_ASW GEN-Modbus-en_V2.1. 3**

```
0 = Disable
1 = Enable
```
##### 44012

```
PE connnection check
function：
0 = Disable
1 = Enable
```
##### E16 - - RW

##### 44014

```
AFCI function：
0 = Disable
1 = Enable
```
##### E16 - - RW

##### 44015

```
PV string current moni-
toring function：
0 = Disable
1 = Enable
```
##### E16 - - RW

##### 44017

```
Overload function：
0 = Disable
1 = Enable
```
##### E16 - - RW

##### 44019

```
SPD detection function：
0 = Disable
1 = Enable
```
##### E16 - - RW

##### 44020

```
Low voltage increase
power function：
0 = Disable
1 = Enable
```
##### E16 - - RW

##### 44021

```
Low frequency increase
power function：
0 = Disable
1 = Enable
```
##### E16 - - RW

##### 44023

```
Primary low frequency
function(*)：
0 = Disable
1 = Enable
```
##### E16 - - RW

##### 44024

```
Communication loss de-
tection function(*)：
0 = Disable
1 = Enable
```
##### E16 - - RW

##### 44025

```
Shadow MPPT function:
0 = Disable
1 = Enable
```
##### E16 - - RW

##### 44026

```
External input signal
function： E16^ -^ -^ RW^
```

**MB001_ASW GEN-Modbus-en_V2.1. 3 16 Technical Information**

```
0 = Disable
1 = Enable
```
##### 44027

```
Sunspec write function：
0 = Disable
1 = Enable
```
##### E16 - - RW

```
45201 Grid code：please refer
to section 3.
```
##### E16 - - RW

##### 45202

```
Overvoltage protection
value of the first grid con-
nection
```
##### U16 V 0.1 RW

##### 45203

```
Overvoltage protection
value of the first grid con-
nection
```
##### U16 V 0.1 RW

##### 45204

```
Overvoltage protection
value of the first grid con-
nection
```
```
U16 Hz 0.01 RW
```
##### 45205

```
Underfrequency protec-
tion value for first grid
connection
```
```
U16 Hz 0.01 RW
```
```
45206 Grid Voltage High Limit3 U16 V 0.1 RW
```
```
45207~
```
```
Grid Voltage High Limit
Time3 U32^ ms^ 1.0^ RW^
45209 Grid Voltage High Limit2 U16 V 0.1 RW
```
```
45210~
```
```
Grid Voltage High Limit
Time2 U32^ ms^ 1.0^ RW^
45212 Grid Voltage High Limit1 U16 V 0.1 RW
```
```
45213~45214 Grid Voltage High Limit
Time
```
```
U32 ms 1.0 RW
```
```
45215 Grid Voltage Low Limit3 U16 V 0.1 RW
```
```
45216~45217 Grid Voltage Low Limit
Time
```
```
U32 ms 1.0 RW
```
```
45218 Grid Voltage Low Limit2 U16 V 0.1 RW
```
```
45219~45220 Grid Voltage Low Limit
Time
```
```
U32 ms 1.0 RW
```
```
45221 Grid Voltage Low Limit1 U16 V 0.1 RW
```
```
45222~
```
```
Grid Voltage Low Limit
Time1 U32^ ms^ 1.0^ RW^
```

**Technical Information 17 MB001_ASW GEN-Modbus-en_V2.1. 3**

##### 45224

```
10 Minutes Average
Overvoltage Threshold U16^ V^ 0.1^ RW^
```
```
45225
```
```
10 Minutes Average
Overvoltage Portect Time U16^ ms^ 1.0^ RW^
```
```
45226 Overvoltage recover
value
```
##### U16 V 0.1 RW

```
45227 Undervoltage recover
value
```
##### U16 V 0.1 RW

```
45228 Grid Frequency High
Limit3
```
```
U16 Hz 0.01 RW
```
```
45229~45230 Grid Frequency High
Limit Time3
```
```
U32 ms 1.0 RW
```
```
45231 Grid Frequency High
Limit2
```
```
U16 Hz 0.01 RW
```
##### 45232~45233

```
Grid Frequency High
Limit Time2 U32^ ms^ 1.0^ RW^
```
```
45234
```
```
Grid Frequency High
Limit1 U16^ Hz^ 0.01^ RW^
```
```
45235~45236
```
```
Grid Frequency High
Limit Time1 U32^ ms^ 1.0^ RW^
```
```
45237
```
```
Grid Frequency Low
Limit3 U16^ Hz^ 0.01^ RW^
```
```
45238~45239
```
```
Grid Frequency Low Limit
Time3 U32^ ms^ 1.0^ RW^
```
```
45240
```
```
Grid Frequency Low
Limit2 U16^ Hz^ 0.01^ RW^
```
```
45241~45242 Grid Frequency Low Limit
Time2
```
```
U32 ms 1.0 RW
```
```
45243 Grid Frequency Low
Limit1
```
```
U16 Hz 0.01 RW
```
```
45244~45245 Grid Frequency Low Limit
Time1
```
```
U32 ms 1.0 RW
```
```
45246 Vary rate of Frequecny^
protect value
```
```
U16 Hz/s 0.01 RW
```
##### 45247~45248

```
Vary rate of Frequecny
protect time
```
```
U32 ms 1.0 RW
```
##### 45249

```
Overfrequency recover
value U16^ Hz^ 0.01^ RW^
```

**MB001_ASW GEN-Modbus-en_V2.1. 3 18 Technical Information**

##### 45250

```
Underfrequency recover
value U16^ Hz^ 0.01^ RW^
```
```
45251
```
```
Time of first connection
to grid U16^ s^ 1.0^ RW^
```
```
45252 Time of re-connection to
grid
```
```
U16 s 1.0 RW
```
```
45253 ISO protect threshold U16 kΩ 1.0 RW
45254 DCI protect threshold U16 mA 1.0 RW
45255 DCI protect time U16 ms 1.0 RW
```
```
45401 Load rate of first connec-
tion to grid
```
```
U16 %Pn/min 1.0 RW
```
##### 45402

```
Load rate of re-connec-
tion to grid U16^ %Pn/min^ 1.0^ RW^
45403 Active Power Set U16 %Pn 0.01 RW
```
```
45404
```
```
Increase rate of active
power U16^ %Pn/min^ 0.01^ RW^
```
```
45405
```
```
Decrease rate of active
power U16^ %Pn/min^ 0.01^ RW^
```
##### 45408

```
Over frequency reduce
power mode：
0 = None
1 = Fixed reduction ratio,
non – hysteresis
2 = Fixed reduction ratio,
hysteresis
3 = Not fixed reduction
ratio, non – hysteresis
4 = Not fixed reduction
ratio, hysteresis
5 = Three points over fre-
quency reduce power,
non – hysteresis
6 = Three points over fre-
quency reduce power,
hysteresis
7 = Energy storage Italy
over frequency reduce
power, non – hysteresis
```
##### E16 - - RW


**Technical Information 19 MB001_ASW GEN-Modbus-en_V2.1. 3**

```
8 = Energy storage Italy
over frequency reduce
power, hysteresis
```
##### 45409

```
Over frequency reduce
power:
Start frequency
```
```
U16 Hz 0.01 RW
```
##### 45410

```
Over frequency reduce
power:
Stop frequecny
```
```
U16 Hz 0.01 RW
```
##### 45411

```
Over frequency reduce
power:
Back frequency
```
```
U16 Hz 0.01 RW
```
```
45412 The reduce ratio of over
frequency reduce power
```
```
U16 %Pnor%Pm 0.01 RW
```
##### 45413

```
Over frequency reduce
power :reduce power de-
lay time
```
```
U16 s 0.1 RW
```
##### 45414

```
Over frequency reduce
power:recover power de-
lay time
```
```
U16 s 0.1 RW
```
##### 45416

```
Speed of Over frequency
recover to Pn U16^ %Pn/min^ 0.01^ RW^
```
##### 45417

```
Over frequency reduce
power(*):
0 power frequency point
```
```
U16 Hz 0.01 RW
```
##### 45419

```
Over voltage reduce
power mode：
0 = None
1 = Not fixed reduction
ratio, non – hysteresis
2 = Not fixed reduction
ratio, hysteresis
3 = Fixed reduction ratio,
non – hysteresis
4 = Fixed reduction ratio,
hysteresis
5 = Taiwan's autonomous
power regulation
6 = Trina Solar customiza-
tion mode
```
##### E16 - - RW


**MB001_ASW GEN-Modbus-en_V2.1. 3 20 Technical Information**

##### 45420

```
Over voltage reduce
power:
Start voltage
```
```
U16 %Un 0.01 RW
```
##### 45422

```
Over voltage reduce
power:
Stop voltage
```
```
U16 %Un 0.01 RW
```
##### 45424

```
Over voltage reduce
power:
Back voltage
```
```
U16 %Un 0.01 RW
```
```
45426 The reduce ratio of over
voltage reduce power
```
```
U16 %Pnor%Pm 0.01 RW
```
```
45427 Over voltage reduce
power delay time
```
```
U16 s 0.1 RW
```
```
45428 Over voltage recover
power delay time
```
```
U16 s 0.1 RW
```
```
45429 Speed of Over voltage re-
cover to Pn
```
```
U16 %Pn/min 0.01 RW
```
##### 45432

```
Under frequency in-
crease power mode：
0 = None
1 = Fixed reduction ratio,
non – hysteresis
2 = Fixed reduction ratio,
hysteresis
3 = Not fixed reduction
ratio, non – hysteresis
4 = Not fixed reduction
ratio, hysteresis
5 = Three points under
frequency increase
power, non – hysteresis
6 = Three points under
frequency increase
power, hysteresis
7 = Energy storage Italy
under frequency increase
power, non – hysteresis
8 = Energy storage Italy
under frequency increase
power, hysteresis
```
##### E16 - - RW


**Technical Information 21 MB001_ASW GEN-Modbus-en_V2.1. 3**

##### 45433

```
Under frequency in-
crease power:
Start frequency
```
```
U16 Hz 0.01 RW
```
##### 45434

```
Under frequency in-
crease power:
Stop frequecny
```
```
U16 Hz 0.01 RW
```
##### 45435

```
Under frequency in-
crease power:
Back frequency
```
```
U16 Hz 0.01 RW
```
##### 45436

```
The increase ratio of un-
der frequency increase
power
```
```
U16 %Pnor%Pm 0.01 RW
```
```
45437 Under frequency in-
crease power delay time
```
```
U16 s 0.1 RW
```
```
45438 Under frequency recover
power delay time
```
```
U16 s 0.1 RW
```
```
45440 Speed of Under fre-
quency recover to Pn
```
```
U16 %Pn/min 0.01 RW
```
##### 45441

```
Under frequency in-
crease power 0 power
frequency point
```
```
U16 Hz 0.01 RW
```
##### 45443

```
Under voltage increase
power mode：
0 = None
1 = Fixed increase ratio,
non – hysteresis
2 = Fixed increase ratio,
hysteresis
3 = Not fixed increase ra-
tio, non – hysteresis
4 = Not fixed increase ra-
tio, hysteresis
```
##### E16 - - RW

##### 45444

```
Under voltage increase
power:
Start voltage
```
```
U16 %Un 0.01 RW
```
##### 45445

```
Under voltage increase
power:
Stop voltage
```
```
U16 %Un 0.01 RW
```
##### 45446

```
Under voltage increase
power: U16^ %Un^ 0.01^ RW^
```

**MB001_ASW GEN-Modbus-en_V2.1. 3 22 Technical Information**

```
Back voltage
```
##### 45447

```
The increase ratio of un-
der voltage increase
power
```
```
U16 %Pnor%Pm 0.01 RW
```
```
45448 Under voltage increase
power delay time
```
```
U16 s 0.1 RW
```
```
45449 Under voltage increase
power delay time
```
```
U16 s 0.1 RW
```
```
45450 Speed of under voltage
recover to Pn
```
```
U16 %Pn/min 0.01 RW
```
```
45451 Pav(*) S16 %Pn 0.01 RW
45452 DRMs Pval(*) U16 %Pn 0.01 RW
```
##### 45501

```
Reactive power control
mode：
0 = None
1 = Fixed power factor
2 = cos φ(P) curve
3 = Fixed Q value
4 = Fixed Q value of AU
DRMs
5 = Linear Q(U) curve
6 = Hysteresis Q(U) curve
7 = Taiwan's autonomous
control and regulation
```
##### E16 - - RW

##### 45502

```
Time constant of reactive
power curve U16^ s^ 1.0^ RW^
45503 Power factor S16 - 0.0001 RW
```
##### 45504

```
cos φ(P) curve：
Active power of the first
point
```
```
U16 %Pn 0.01 RW
```
##### 45505

```
cos φ(P) curve：
cos φ of the first point S16^ -^ 0.0001^ RW^
```
##### 45506

```
cos φ(P) curve：
Active power of the sec-
ond point
```
```
U16 %Pn 0.01 RW
```
```
45507 cos φ(P) curve：^
cos φ of the second point
```
##### S16 - 0.0001 RW

```
45508 cos φ(P) curve： U16 %Pn 0.01 RW
```

**Technical Information 23 MB001_ASW GEN-Modbus-en_V2.1. 3**

```
Active power of the third
point
```
```
45509
```
```
cos φ(P) curve：
cos φ of the third point S16^ -^ 0.0001^ RW^
```
##### 45510

```
cos φ(P) curve：
Active power of the
fourth point
```
```
U16 %Pn 0.01 RW
```
```
45511 cos φ(P) curve：^
cos φ of the fourth point
```
##### S16 - 0.0001 RW

```
45512 Lock in voltage (for cos
φ(P) curve）
```
```
U16 %Un 0.01 RW
```
```
45513 Lock out voltage (for cos
φ(P) curve)
```
```
U16 %Un 0.01 RW
```
```
45516 Q Set Value S16 %Sn 0.01 RW
```
```
45518
```
```
Q(U) curve：
U of the first point U16^ %Un^ 0.01^ RW^
```
```
45519
```
```
Q(U) curve：
Q of the first point S16^ %Sn^ 0.01^ RW^
```
```
45520
```
```
Q(U) curve：
U of the second point U16^ %Un^ 0.01^ RW^
```
```
45521
```
```
Q(U) curve：
Q of the second point S16^ %Sn^ 0.01^ RW^
```
```
45522
```
```
Q(U) curve：
U of the third point U16^ %Un^ 0.01^ RW^
```
```
45523
```
```
Q(U) curve：
Q of the third point S16^ %Sn^ 0.01^ RW^
```
```
45524 Q(U) curve：^
U of the fourth point
```
```
U16 %Un 0.01 RW
```
```
45525 Q(U) curve：^
Q of the fourth point
```
```
S16 %Sn 0.01 RW
```
```
45526 Lock in power（for Q(U)
curve）
```
```
U16 %Pn 0.01 RW
```
```
45527 Lock outpower（for Q(U)
curve）
```
```
U16 %Pn 0.01 RW
```
##### 45601

```
LVRT reactive current cal-
culation mode：
0 = None
1 = GB/T 19964
```
##### E16 - - RW


**MB001_ASW GEN-Modbus-en_V2.1. 3 24 Technical Information**

##### 2 = BDEW

##### 45602

```
LVRT three phase fault
reactive current limit U16^ %In^ 0.01^ RW^
```
##### 45603

```
LVRT single/double phase
fault reactive current
limit
```
```
U16 %In 0.01 RW
```
##### 45604

```
LVRT fault detection volt-
age type：
0 = Phase voltage
1 = Line voltage
2 = Positive sequence
voltage
```
##### E16 - - RW

##### 45605

```
Threshold of insensitive
area of positive sequence
voltage jump
```
```
U16 %Un 0.1 RW
```
```
45606 LVRT Trigger voltage U16 %Un 0.01 RW
```
```
45607 K-factor of positive se-
quence reactive current
```
##### U16 - 0.01 RW

##### 45608

```
reactive power mainte-
nance after LVRT voltage
recovery
```
```
U16 ms 1 RW
```
##### 45609

```
LVRT active power limit
mode：
0 = Active power first
1 = Active power reduced
to below 10% In
2 = Unlimited active
power
```
##### E16 - - RW

##### 45610

```
HVRT reactive current
calculation mode：
0 = None
1 = GB/T 19964
2 = BDEW
```
##### E16 - - RW

##### 45611

```
HVRT three phase fault
reactive current limit U16^ %In^ 0.01^ RW^
```
##### 45612

```
HVRT single/double
phase fault reactive cur-
rent limit
```
```
U16 %In 0.01 RW
```
```
45613 HVRT^ fault detection
voltage type:
```
##### E16 - - RW


**Technical Information 25 MB001_ASW GEN-Modbus-en_V2.1. 3**

```
0 = Phase voltage
1 = Line voltage
2 = Positive sequence
voltage
```
##### 45614

```
Threshold of insensitive
area of negative se-
quence voltage jump
```
```
U16 %Un 0.1 RW
```
```
45615 HVRT Trigger voltage U16 %Un 0.01 RW
```
```
45616 K-factor of negative se-
quence reactive current
```
##### U16 - 0.01 RW

##### 45617

```
reactive power mainte-
nance after HVRT voltage
recovery
```
```
U16 ms 1 RW
```
##### 45618

```
HVRT active power limit
mode:
0 = Active power first
1 = Active power reduced
to below 10% In
2 = Unlimited active
power
```
##### E16 - - RW

```
45619 Zero current threshold U16 %Un 0.01 RW
```
##### 46520

```
AFCI self-test status:
0 = self-test failed
1 = self-test successful
```
##### E16 - - RO

##### 46521

```
AFCI detection sensitivity
settings:
0 = high detection
accuracy
1 = low detection
accuracy
```
##### E16 - - RW

##### 46522

```
AFCI fault reset method
selection:
0 = automatic mode
1 = manual mode
```
##### E16 - - RW

##### 46523

```
Reset faults manually:
0 = no need
1 = Manual clear fault
(active in arc fault and
manual reset mode)
```
##### E16 - - RW

# CT Data


**MB001_ASW GEN-Modbus-en_V2.1. 3 26 Technical Information**

##### 46401

```
Phase 1 line to neutral
volts U16^ V^ 0.1^ RW^
```
```
46402
```
```
Phase 2 line to neutral
volts U16^ V^ 0.1^ RW^
```
```
46403 Phase 3 line to neutral
volts
```
##### U16 V 0.1 RW

```
46404 Phase 1 current U16 A 0.1 RW
46405 Phase 2 current U16 A 0.1 RW
46406 Phase 3 current U16 A 0.1 RW
46407 Phase 1 power S32 w 1 RW
46409 Phase 2 power S32 w 1 RW
46411 Phase 3 power S32 w 1 RW
46413 Phase 1 volt amps U32 VA 1 RW
46415 Phase 2 volt amps U32 VA 1 RW
46417 Phase 3 volt amps U32 VA 1 RW
```
```
46419 Phase 1 volt amps
reactive
```
```
S32 Var 1 RW
```
```
46421 Phase 2 volt amps
reactive
```
```
S32 Var 1 RW
```
```
46423 Phase 3 volt amps
reactive
```
```
S32 Var 1 RW
```
```
46425 Phase 1 power factor S16 - 0.01 RW
46426 Phase 2 power factor S16 - 0.01 RW
46427 Phase 3 power factor S16 - 0.01 RW
46428 Phase 1 phase angle U16 ° 1 RW
46429 Phase 2 phase angle U16 ° 1 RW
46430 Phase 3 phase angle U16 ° 1 RW
```
```
46431
```
```
Average line to neutral
volts U16^ V^ 0.1^ RW^
46432 Average line current U16 A 0.1 RW
46433 Sum of line currents U16 A 0.1 RW
46434 Total systempower S32 w 1 RW
46436 Total systemvolt amps U32 VA 1 RW
46438 Total system VAr S32 Var 1 RW
46440 Total systempower factor S16 - 0.01 RW
```

**Technical Information 27 MB001_ASW GEN-Modbus-en_V2.1. 3**

```
46441 Total systemphase angle U16 ° 1 RW
```
```
46442
```
```
Frequency of supply
voltages U16^ Hz^ 0.01^ RW^
```
```
46443 Import Wh since last
reset
```
```
U32 Wh 1 RW
```
```
46445 Export Wh since last
reset
```
```
U32 Wh 1 RW
```
```
46447 Import Varh since last
reset
```
```
U32 Varh 1 RW
```
```
46449 Export Varh since
lastreset
```
```
U32 Varh 1 RW
```
```
46451 Phase 1 line to neutral
volts
```
##### U16 V 0.1 RW

```
(*) ------ Supported on some models
31601~316 81 ，41102~411 56 Special for storage inverter
```

**MB001_ASW GEN-Modbus-en_V2.1. 3 28 Technical Information**

### 3.4 Warning and Error Codes

```
Warning Code Description
```
```
0 No warning
30 Recover from warning
150 SPD Damaged
156 Internal fan warning
157 External fan warning
163 String current abnormal
165 Ground connect warning
166 CPU self-test ---- Register abnornal
167 CPU self-test ---- RAM abnornal
168 CPU self-test ---- ROM abnornal
174 Low Air Temprature
175 Battery Soc Low
176 Battery Fault Status
177 Battery Communication DisConnect
178 EPS Output Over
179 Combox and Cloud Disconnect
180 PV string inverse
```

**Technical Information 29 MB001_ASW GEN-Modbus-en_V2.1. 3**

```
Error Code Description
```
```
1 Communication Fails between M-S
3 Relay check Fail
4 DC Injection High
5 The result of Auto Test Function is fail
6 DC bus is too high
8 AC HCT Failure
9 GFCI Device Failure
10 Device fault
32 ROCOF Fault
33 Fac Faulure :Fac Out of Range
34 AC Voltage Out of Range
35 Utility Loss
36 GFCI Failure
37 PV Over Voltage
38 Isolation Fault
40 Over temperature in Inverter
41 Consistent Fault :Vac differs for M-S
42 Consistent Fault :Fac differs for M-S
43 Consistent Fault :Groud I differs for M-S
44 Consistent Fault :DC inj. Differs for M-S
45 Consistent Fault :Fac,Vac differs for M-S
46 High DC bus
47 Consistent Fault
48 Average volt of ten minutes Fault
49 PV1 lightning arrester fault
50 PV2 lightning arrester fault
51 Fuse failure
52 Neutral line loss fault
```

**MB001_ASW GEN-Modbus-en_V2.1. 3 30 Technical Information**

```
53 Insulation impedance test: before enabling the constant current
source, the sampling value of insulation impedance measure-
ment voltage is greater than 300mV
54 Insulation impedance detection: after enabling constant current
source, the sampling value of insulation impedance measure-
ment voltage is out of range (1.37V±20%)
55 Insulation impedance detection: N-PE relay switches, and the in-
stantaneous value of insulation impedance measurement voltage
is less than 40mV
56 GFCI protect fault:30mA level
57 GFCI protect fault:60mA level
58 GFCI protect fault:150mA level
59 PV1 string current is abnormal
60 PV2 string current is abnormal
61 DRMS Communication Fails(S9 Open)
62 DRMS order disconnection device(S0 Close)
63 L-PE short-circuit protection error
64 PV input mode error
65 PE connection Fault
66 PV1 reverse connection fault
67 PV2 reverse connection fault
68 PV3 reverse connection fault
69 External input failure
70 AFCI self-test failure (including self-test circuit and CAN circuit
failure)
71 AFCI failure (PV1-10)
72 Parallel 485 communication fault
73 Parallel CAN communication fault
```

**Technical Information 31 MB001_ASW GEN-Modbus-en_V2.1. 3**

### 3.5 Grid Codes

```
Grid Code Description
```
##### 8 GR PPC

##### 35 NB/T32004:2018

##### 47 AU AS 4777.2 : 2015

##### 48 NZ AS 4777.2 : 2015

```
49 ENGG-50Hz
50 ENGG-60Hz
51 TOR Erzeuger Typ A V1.1
59 CNS15382:2018
64 EN 50549- 1
65 NL EN50549-1:2019
66 BR NBR 16149:2013
67 VDE0126- 1 - 1/A1/VFR
68 IEC 61727 50Hz
69 C10/11:2019
70 VDE-AR-N4105:2018
71 IEC 61727 60Hz
72 G98/1
73 G99/1
74 AU AS/NZS4777.2:2020 A
75 AU AS/NZS4777.2:2020 B
76 AU AS/NZS4777.2:2020 C
77 NZ AS/NZS4777.2:2020
78 IL SI4777.3
```
(^79) KR KS C 8565:2020
80 ES UNE206007- 1
81 CY EN50549- 1
82 CS PPDS A1
83 PL EN50549- 1


**MB001_ASW GEN-Modbus-en_V2.1. 3 32 Technical Information**

##### 84 CEI 0-21:2019

##### 85 DK EN50549- 1

##### 86 CH NA/EEA-NE7

##### 87 SE EIFS:2018

##### 88 FI EN50549- 1

```
89 RO Order208
90 SI EN50549- 1
91 LV EN50549- 1
92 VDE0126/VFR2019 IS (50Hz)
93 VDE0126/VFR2019 IS (60Hz)
94 ZA NRS 097- 2 - 1:2017
95 BR PORTARIA No.140
96 NTS 631 Type A
97 NTS 631 Type B
98 NO EN50549- 1
99 VDE-AR-N 4110
100 EN 50549- 2
101 DEWA:2016
102 DK1 EN50549- 1
103 ZA RPPs
```

**Technical Information 33 MB001_ASW GEN-Modbus-en_V2.1. 3**

### 3.6 Frame format

MODBUS protocol format: RTU format. Each communication data unit is composed of 1 bit
starting bit, 8 bit data bit and 1 bit stopping bit, no parity.

MODBUS function codes:

- Read Holding Register (0x03)
- Read Input Register (0x04)
- Write Holding Single Register (0x06)
- Write Holding Multiple Registers (0x10)
- Write Holding Multiple Registers (0x10) for broadcast

#### 3.6.1 Read Holding Register (Function Code: 0x03)

**Request:**

```
Device ID 1 Byte
Function code 1 Byte
Register Address(Hi) 1 Byte
Register Address(Lo) 1 Byte
Register length 2 Byte
CRC(Lo) 1 Byte
CRC(Hi) 1 Byte
```
**Response:**

```
Device ID 1 Byte
Function code 1 Byte
Byte count 1 Byte
Data N × 1 Byte
CRC(Lo) 1 Byte
CRC(Hi) 1 Byte
```
**Error** ：

```
Device ID 1 Byte
Function code + 0x80 1 Byte
Exception code 1 Byte
CRC(Lo) 1 Byte
CRC(Hi) 1 Byte
```

**MB001_ASW GEN-Modbus-en_V2.1. 3 34 Technical Information**

#### 3.6.2 Read Input Register (Function Code: 0x04)

**Request:**

```
Device ID 1 Byte
Function code 1 Byte
Register Address(Hi) 1 Byte
Register Address(Lo) 1 Byte
Register length 2 Byte
CRC(Lo) 1 Byte
CRC(Hi) 1 Byte
```
**Response:**

```
Device ID 1 Byte
Function code 1 Byte
Byte count 1 Byte
Data N × 1 Byte
CRC(Lo) 1 Byte
CRC(Hi) 1 Byte
```
**Error** ：

```
Device ID 1 Byte
Function code + 0x80 1 Byte
Exception code 1 Byte
CRC(Lo) 1 Byte
CRC(Hi) 1 Byte
```

**Technical Information 35 MB001_ASW GEN-Modbus-en_V2.1. 3**

#### 3.6.3 Write Single Holding Register (Function Code: 0x06)

**Request:**

```
Device ID 1 Byte
Function code 1 Byte
Register Address(Hi) 1 Byte
Register Address(Lo) 1 Byte
Data 2 Byte
CRC(Lo) 1 Byte
CRC(Hi) 1 Byte
```
**Response:**

```
Device ID 1 Byte
Function code 1 Byte
Register Address(Hi) 1 Byte
Register Address(Lo) 1 Byte
Data 2 Byte
CRC(Lo) 1 Byte
CRC(Hi) 1 Byte
```
**Error:**

```
Device ID 1 Byte
Function code + 0x80 1 Byte
Exception code 1 Byte
CRC(Lo) 1 Byte
CRC(Hi) 1 Byte
```

**MB001_ASW GEN-Modbus-en_V2.1. 3 36 Technical Information**

#### 3.6.4 Write Multiple Holding Registers (Function Code: 0x10)

**Request:**

```
Device ID 1 Byte
Function code 1 Byte
Register Address(Hi) 1 Byte
Register Address(Lo) 1 Byte
Register length 2 Byte
Data length 1 Byte
Data N × 1Byte
CRC(Lo) 1 Byte
CRC(Hi) 1 Byte
```
**Response:**

```
Device ID 1 Byte
Function code 1 Byte
Register Address(Hi) 1 Byte
Register Address(Lo) 1 Byte
Register length 2 Byte
CRC(Lo) 1 Byte
CRC(Hi) 1 Byte
```
**Error:**

```
Device ID 1 Byte
Function code + 0x80 1 Byte
Exception code 1 Byte
CRC(Lo) 1 Byte
CRC(Hi) 1 Byte
```

**Technical Information 37 MB001_ASW GEN-Modbus-en_V2.1. 3**

#### 3.6.5 Write Multiple Holding Registers (Function Code: 0x10) for broad-

#### cast

**Request:**

```
Device ID 1 Byte
Function code 1 Byte
Register Address(Hi) 1 Byte
Register Address(Lo) 1 Byte
Register length 2 Byte
Data length 1 Byte
Data N × 1Byte
CRC(Lo) 1 Byte
CRC(Hi) 1 Byte
```
**Response: none**

#### 3.6.6 Exception Codes

0x01 Illegal function

0x02 Illegal address

0x03 Illegal data

0x04 Slave device failure


**MB001_ASW GEN-Modbus-en_V2.1. 3 38 Technical Information**

## 4 Contact

If you experience any technical problems with our products, please contact the AISWEI Ser-
vice Hotline to provide you with the necessary assistance:

**AISWEI Technology Co., Ltd.**

Room 905B, 757 Mengzi Road, Huangpu District

200023 Shanghai(P.R. China)


