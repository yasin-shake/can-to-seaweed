//====================================================================================================
const char *szProgramInfo   = "Diesel Laptops, LLC - ECM Simulator (J1939, J1708/J1587)"             ;
const char *szCopyrightInfo = "Copyright (C) 2018-2021, Diesel Laptops, LLC - All Rights Reserved."  ;
const char *szRevision      = "Version 1.0.2"                                                        ;
const char *szDate          = "01/07/2021"                                                           ;
//====================================================================================================
//
//   Diesel Laptops, LLC - ECM Simulator
//
//      * J1939, J1708/J1587 ECM Simulator Software
//
//----------------------------------------------------------------------------------------------------
//
// This code is meant for databus engineers needing a simple J1939, J1708/J1587 simulator product
// that can be easily modified to send specific J1939 or J1708/J1587 messages.
//
//----------------------------------------------------------------------------------------------------
//
//  * J1939
//
//  This code provides a simple J1939 simulator that sends various parameters, faults (DTCs), and
//  component information.  J1939 messages over 8 bytes are sent via BAM transport messages.
//
//  * J1708/J1587
//
//  This code provides a simple J1708/J1587 send mechanism.  The simulator sends J1708/J1587 messages
//  without the need to implement the full J1708 protocol stack.  The J1708 portion ensures enough
//  delay time between messages that other modules on the databus will be able to discern "idle"
//  time (10 idle bits).
//
//====================================================================================================
//
// New to Version 1.0.2 is sending a proprietary J1939 PGN=65280 SRC=0x00 DST=0xFF to indicate the 
// switch and potentiometer (POT) statuses.  The same information goes out in a J1587 message with
// MID=128 and PID=254. This code was added to speed end-of-line testing to ensure the ECM simulator 
// is wired properly without having to use a diagnostic application to look for specific parameters
// that are controlled by the POTs and switches.
//
// This is the data packet defined for PGN 65280 and PID 254.
//
//  Byte  Bits  Length    Parameter                         BitRes  Range      Offset Notes
//  ----  ----  -------   --------------------------------  ------  ---------  -----  ------------
//  0-1         2 Bytes   A0 - Engine Speed Potentiometer   1.0     0 - 1023   0.0
//  2-3         2 Bytes   A2 - Vehicle Speed Potentiometer  1.0     0 - 1023   0.0
//  4-5         2 Bytes   A4 - Oil Pressure Potentiometer   1.0     0 - 1023   0.0
//  6     0     1 Bit     A1 - DTC 3554 button              1.0     0 - 1      0.0    1=pressed, 0=released
//  6     1     1 Bit     A3 - DTC 3107 button              1.0     0 - 1      0.0    1=pressed, 0=released
//  6     2     1 Bit     A5 - DTC 17858 button             1.0     0 - 1      0.0    1=pressed, 0=released
//  7           1 Byte    Reserved, set to FF
//
//====================================================================================================
//
//  *** Available Option - ISO15765 11-bit/29-bit CAN OBDII Simulator Software
//
//  Available for a separate price is the source code for an ISO15765 11-bit/29-bit CAN OBDII simulator
//  module that supports all OBDII modes for ISO15765.  The simulator code runs on the same hardware
//  platform as this J1939, J1708/J1587 code.
//
//  Most VDAs have a 9-pin Deutsch connector that takes CAN1 (legislated OBDII on the J1962 OBDII
//  connector) and connects it to CAN1 on the VDA.  This keeps the user from purchasing a VDA vendor's
//  J1962 OBDII cable.  If the VDA that you are using does not have a 9-pin Deutsch connector,
//  a 9-pin Deutsch to OBDII (16-pin J1962) adapter is available from Diesel Laptops.
//
//  This simulator provides much more information than some of the most expensive simulators on the
//  market while allowing the modification to support even more parameters and modes.
//
//----------------------------------------------------------------------------------------------------
//  Diesel Laptops, LLC - ECM Simulator and Source Code Part Numbers
//
//  * DL-ESIM1939    ECM Simulator J1939, J1708/J1587     - Hardware and Binary Executable Only
//  * DL-ESIM15765   ECM Simulator CAN/ISO15765           - Hardware and Binary Executable Only
//  * DL-ESIM1939S   ECM Simulator J1939, J1708/J1587 Kit - Hardware, Binary Executable, and Source Code
//  * DL-ESIM15765S  ECM Simulator CAN/ISO15765 Kit       - Hardware, Binary Executable, and Source Code
//  * DL-ESIMALL     ECM Simulator Complete Kit           - J1939, J1708/J1587 and CAN/ISO15765 Hardware,
//                                                          Binary Executables, and Source Code
//
//----------------------------------------------------------------------------------------------------
//
//  Hardware:       Arduino Mega 2560
//                  Seeed Studios CAN Shield @ 16 MHz
//                  CopperHill Technologies RS-485 Breakout Board 5VDC (for J1708/J1587)
//
//  Library(s)      https://github.com/coryjfowler/MCP_CAN_lib by Cory J Fowler
//  Needed:
//
//  Pin Usage:      Pins Used for General Purpose I/O
//                  ----------------------------------------------------------------------------------
//                  10k Potentiometers  [A0]  [A2]  [A4]      
//                  Switches To Ground  [A5]  [A3]  [A1]  (internal pullups activated in this code)
//
//                  POT A0 - Engine Speed          (PGN 61444 / SPN 190)
//                  POT A2 - Vehicle Speed         (PGN 65265 / SPN  84)
//                  POT A4 - Oil Pressure          (PGN 65263 / SPN 100)
//
//                  SW  A1 - DTC button: DM1 SPN 111, FMI 2 (alert 3554)
//                  SW  A3 - DTC button: DM1 SPN  97, FMI 3 (alert 3107)
//                  SW  A5 - DTC button: DM1 SPN 558, FMI 2 (alert 17858)
//
//                  The buttons no longer drive Park Brake / Brake Pedal / Clutch Pedal.  There are no
//                  continuous DTCs any more: each button PRESS sends exactly ONE DM1 (PGN 65226) frame
//                  carrying one DTC, and nothing is sent on release or while the button is held.
//                  The DM1 lamp byte (severity) of these DTCs is set with the serial command
//                  "BTN LAMP <0..255>" (default 0x10 = red stop lamp).
//
//                  J1708/J1587 Specific Pin Usage
//                  ----------------------------------------------------------------------------------
//                  19  - RX1 Serial Port 2 Receive
//                  18  - TX1 Serial Port 2 Transmit
//                  D22 - High to Receive J1708, Low to Send J1708
//
//                  J1939 Connector (J1939 Type 1) - Allows all VDA devices/cables to connect.
//                  ----------------------------------------------------------------------------------
//                  A  Ground                     - Provided for VDAs
//                  B  Power 12V                  - Provided for VDAs
//                  C  CAN1/J1939+                 
//                  D  CAN1/J1939-                 
//                  E  CAN1/J1939 Shield (Unused)  
//                  F  J1708/J1587+               
//                  G  J1708/J1587-
//                  H  Unused 
//                  J  Unused 
//
//  CAN Baud Rate:  The CAN/J1939 Baud Rate is set initially to 250k because the connector is a
//                  J1939 Type I (pre-J1939@500k).  The baud rate is changeable with the
//                  #define CAN_BAUD_RATE [ CAN_250KBPS | CAN_500KBPS ].
//
//  Development     
//  Environment:    Arduino IDE 1.8.6 or newer.
//
//  Debugging:      Serial Port 0 @ 115200 baud.  The Tools->Serial Monitor is enough to do simple
//                  debugging statements.  If advanced debugging is needed, a serial monitor such
//                  as the free PuTTY software works well.
// 
//  Notes:          Every effort has been made to use "plain C language" and to avoid Arduino
//                  specific compiler directives so that this code can easily be compiled and
//                  ported for use with an RP1210 Vehicle Diagnostic Adapter (VDA).  The code has also
//                  been written using the simplest code constructs so that individuals who are not
//                  familiar with C or Arduino can learn how to modify this code for their needs.
//
//  Hardware:       Dustin Carnes
//                  Training Manager
//                  Diesel Laptops, LLC
//                  dustin@diesellaptops.com
//
//  Software:       Ken DeGrant
//                  Engineering Manager
//                  Diesel Laptops, LLC 
//                  ken@diesellaptops.com
// 
//====================================================================================================
// LICENSE
//====================================================================================================
//
// This source code is licensed and is subject to an End User License Agreement (EULA).  By viewing or
// using this code in any manner, you are subject to this license agreement.
//
// A copy of the EULA can be found in the User Manual for this product or can be provided upon
// request by calling Diesel Laptops, LLC at 888-983-1975.  The EULA covers terms such
// as "licensee" and the responsibilities of the licensed party.
//
// Highlights of the EULA:
//
// 1.  This code is for the "Diesel Laptops, LLC - ECM Simulator", the "product".
//
// 2.  Porting of this source code or any portion of this source code for use on any device other
//     than the "product" is expressly forbidden.  This includes but is not limited to taking
//     the various circuit boards used and developing a similar product.
//
// 3.  This code is not "in the public domain" and is not to be distributed by the licensee in any
//     form, which includes the complete source code and any source code snippet.
//
// 4.  This source code is for use only by the "licensee" and entities working directly for
//     or on the behalf of the "licensee" (agents).
//
// 5.  The code herein may be modified by the licensee as long as the code remains solely for the
//     "product".  The code may be distributed in binary (object) form only in the event the
//     licensee wants to release a derivative product based on this product.
//
// 6.  Binary releases that include this compiled code are acceptable as long as the code is not used 
//     to compete in any way with this product.
//
// 7.  Libraries and source code used by this product may have separate licensing terms.  Some
//     of these licenses are "in the public domain".
//
// 8.  The comment block stating the licensing (LICENSE) and Copyright (COPYRIGHT) sections fully
//     must remain intact and part of any modifications or distribution of this code.
//
//====================================================================================================
// COPYRIGHT - Copyright (C) 2018-2021, Diesel Laptops, LLC - All Rights Reserved.
//====================================================================================================

//----------------------------------------------------------------------------------------------------
// Include files.
//----------------------------------------------------------------------------------------------------

#include <mcp_can.h>
#include <mcp_can_dfs.h>
#include <spi.h>

//----------------------------------------------------------------------------------------------------
// Define TRUE/FALSE.
//----------------------------------------------------------------------------------------------------

#ifndef TRUE                       
#define TRUE  1                    
#define FALSE 0                    
#endif                             

//----------------------------------------------------------------------------------------------------
// Define ON/OFF.  Used for the pushbuttons to indicate ON (pushed - LOW) or OFF (high-pullups).
//----------------------------------------------------------------------------------------------------

#ifndef ON                       
#define ON  1                    
#define OFF 0                    
#endif                             

//----------------------------------------------------------------------------------------------------
// Typedefs for common variable types.
//----------------------------------------------------------------------------------------------------

typedef unsigned char  U8  ;
typedef unsigned short U16 ;
typedef unsigned long  UL  ;

U8      ucOverrideActive     = FALSE;   // TRUE = values below override the physical pots/switches
float   fOverrideEngPct      = 0;       // 0..100
float   fOverrideVehPct      = 0;       // 0..100
float   fOverrideOilPct      = 0;       // 0..100
U8      ucOverridePark       = OFF;
U8      ucOverrideBrake      = OFF;
U8      ucOverrideClutch     = OFF;
String  szSerialCmdBuffer    = "";

//----------------------------------------------------------------------------------------------------
// QA extended override channels.
//
// These carry values in ENGINEERING UNITS (deg C, kPa, volts, percent) rather than POT travel,
// because there is no physical knob behind them - they exist only to be driven over USB.
//
// QA_OVR_UNSET means "not overridden", so the original firmware behaviour (the tradeshow sweep, or
// whichever POT the parameter was coupled to) is preserved until QA actually sets the channel.
// The sentinel sits below every legal value so genuinely negative temperatures still work.
//----------------------------------------------------------------------------------------------------

#define QA_OVR_UNSET          -999.0
#define QA_IS_SET( x )        ( ( x ) > -900.0 )

float   fOvrCoolantC         = QA_OVR_UNSET;   // SPN 110, deg C
float   fOvrOilPressKPA      = QA_OVR_UNSET;   // SPN 100, kPa
float   fOvrBoostKPA         = QA_OVR_UNSET;   // SPN 102, kPa
float   fOvrVoltage          = QA_OVR_UNSET;   // SPN 168, volts
float   fOvrTransOilC        = QA_OVR_UNSET;   // SPN 177, deg C
float   fOvrFuelPct          = QA_OVR_UNSET;   // SPN  96, percent
float   fOvrAmbientC         = QA_OVR_UNSET;   // SPN 171, deg C

//----------------------------------------------------------------------------------------------------
// QA DTC injection table, broadcast as a J1939 DM1 (PGN 65226).
//----------------------------------------------------------------------------------------------------

#define MAX_QA_DTCS  8

struct QADTC
{
  UL  ulSPN ;   // Suspect Parameter Number, 19 bits
  U8  ucFMI ;   // Failure Mode Identifier, 5 bits
  U8  ucOC  ;   // Occurrence Count, 7 bits
};

struct QADTC sQADTCs[ MAX_QA_DTCS ];

U8      ucQADTCCount         = 0;       // How many entries of sQADTCs are in use.
U8      ucQADTCLamp          = 0x00;    // DM1 lamp status byte.
U8      ucQADTCEnabled       = FALSE;   // TRUE = broadcast the QA DM1 every loop 1.
U8      ucSweepEnabled       = TRUE;    // FALSE = freeze the tradeshow ambient sweep.

//----------------------------------------------------------------------------------------------------
// Forward declarations for the QA additions.
//----------------------------------------------------------------------------------------------------

/*PROCEDURE*/ void  HandleSerialCommand   ( String szCmd );
/*PROCEDURE*/ void  ProcessSerialCommands ( void );
/*PROCEDURE*/ void  Send_QA_DM1           ( void );


//----------------------------------------------------------------------------------------------------
// Pins and baud rates.
//----------------------------------------------------------------------------------------------------

#define CAN_INT_PIN                 2   // Digital input pin for the CAN interrupt.
#define CAN_CS_PIN                  9   // Digital input pin for the SPI Chip Select (CS).
#define RS485_RXTXPIN              22   // Digital input pin for J1708 UART transmit/receive.
#define CONTINUOUS_DTC_BROADCAST      0  // 1 = also broadcast the fixed DTC set every cycle; 0 = button-press DTCs only
#define RS232_BAUD             115200   // Baud rate for the debugging serial port.
#define RS485_BAUD               9600   // Baud rate for the RS485/J1708 serial port (9600).
#define CAN_BAUD_RATE     CAN_250KBPS   // CAN baud rate of 250k.

//----------------------------------------------------------------------------------------------------
// Bit definitions for masking bits.  One-based because J1939 uses bit 1..X in the documentation.
//----------------------------------------------------------------------------------------------------

#define BIT1                     0x01  // Bit 0
#define BIT2                     0x02  // Bit 1
#define BIT3                     0x04  // Bit 2
#define BIT4                     0x08  // Bit 3
#define BIT5                     0x10  // Bit 4
#define BIT6                     0x20  // Bit 5
#define BIT7                     0x40  // Bit 6
#define BIT8                     0x80  // Bit 7

//----------------------------------------------------------------------------------------------------
// Macros to pull apart nibbles, bytes, and words from unsigned numbers.
//----------------------------------------------------------------------------------------------------

#define LONIBBLE_OF_CHAR( x ) ( ( x &       0x0F ) >>  0 ) 
#define HINIBBLE_OF_CHAR( x ) ( ( x &       0xF0 ) >>  4 )

#define LOBYTE_OF_WORD( x )   ( ( x &     0x00FF ) >>  0 )
#define HIBYTE_OF_WORD( x )   ( ( x &     0xFF00 ) >>  8 )

#define LOWORD_OF_UL( x )     ( ( x & 0x0000FFFF ) >>  0 )
#define HIWORD_OF_UL( x )     ( ( x & 0xFFFF0000 ) >> 16 )

#define BYTE0_OF_UL( x )      ( ( x & 0x000000FF ) >>  0 )
#define BYTE1_OF_UL( x )      ( ( x & 0x0000FF00 ) >>  8 )
#define BYTE2_OF_UL( x )      ( ( x & 0x00FF0000 ) >> 16 )
#define BYTE3_OF_UL( x )      ( ( x & 0xFF000000 ) >> 24 )

//----------------------------------------------------------------------------------------------------
// Potentiometer (POT) pin definitions and variables controlled by them.
//
//  POT A0 - Engine Speed          (PGN 61444 / SPN 190)
//  POT A2 - Vehicle Speed         (PGN 65265 / SPN  84)
//  POT A4 - Oil Pressure          (PGN 65263 / SPN 100)
//
// Keep an integer version of the POT value (0..1023) and also a percent variable for each.
//----------------------------------------------------------------------------------------------------

#define A0_ENGINE_SPEED_POT           A0   // 10k linear POT
#define A2_VEHICLE_SPEED_POT          A2   // 10k linear POT
#define A4_OIL_PRESSURE_POT           A4   // 10k linear POT

U16     usA0_EngineSpeedPosition   =  0;   // POT position from 0.00 to 1023.00.
U16     usA2_VehicleSpeedPosition  =  0;   // POT position from 0.00 to 1023.00.
U16     usA4_OilPressurePosition   =  0;   // POT position from 0.00 to 1023.00.

float   fA0_EngineSpeedPercent     =  0;   // POT percent from 0.00 to 100.00.
float   fA2_VehicleSpeedPercent    =  0;   // POT percent from 0.00 to 100.00.
float   fA4_OilPressurePercent     =  0;   // POT percent from 0.00 to 100.00.

//----------------------------------------------------------------------------------------------------
// On/Off switch pin definitions and variables controlled by them.
//
//   A1 - Park Brake Switch     (PGN 65265 / SPN  70)  
//   A3 - Brake Pedal Switch    (PGN 65265 / SPN 597)  
//   A5 - Clutch Pedal Switch   (PGN 65265 / SPN 598)
//
//----------------------------------------------------------------------------------------------------

#define A1_PARK_BRAKE_SWITCH          A1  // Switch On/Off
#define A3_BRAKE_PEDAL_SWITCH         A3  // Switch On/Off
#define A5_CLUTCH_PEDAL_SWITCH        A5  // Switch On/Off

U8      ucParkBrakeSwitch         = OFF;  // Legacy signal state - no longer driven by a button (always OFF).
U8      ucBrakePedalSwitch        = OFF;  // Legacy signal state - no longer driven by a button (always OFF).
U8      ucClutchPedalSwitch       = OFF;  // Legacy signal state - no longer driven by a button (always OFF).

//
// Physical button state (ON = pressed) and the previous sample, used for press-edge detection.
//

U8      ucBtnA1Pressed            = OFF;  // Alert 3554 button.
U8      ucBtnA3Pressed            = OFF;  // Alert 3107 button.
U8      ucBtnA5Pressed            = OFF;  // Alert 17858 button.
U8      ucBtnA1Prev               = OFF;
U8      ucBtnA3Prev               = OFF;
U8      ucBtnA5Prev               = OFF;

//
// DTC sent by each button press (SAE J1939-73 DM1).  The occurrence count increments per press.
//

#define BTN_A1_SPN   111   // Engine Coolant Level 1
#define BTN_A1_FMI     2   // Data erratic, intermittent or incorrect
#define BTN_A3_SPN    97   // Water In Fuel Indicator 1
#define BTN_A3_FMI     3   // Voltage above normal or shorted to high source
#define BTN_A5_SPN   558   // Accelerator Pedal 1 Low Idle Switch
#define BTN_A5_FMI     2   // Data erratic, intermittent or incorrect
#define BTN_DTC_LAMP  0x10 // Default lamp byte: red stop lamp on (bits 6-5 = 01); MIL, amber and protect off.

// Lamp status byte sent with each button DTC (= DTC severity).  Changed at run time with the serial
// command "BTN LAMP <0..255>" (the pipeline dashboard sends it).  J1939-73 byte 1 layout:
//   bits 8-7 MIL, bits 6-5 red stop, bits 4-3 amber warning, bits 2-1 protect (01 = on).
U8      ucBtnDTCLamp              = BTN_DTC_LAMP;

U8      ucBtnA1Count              = 0;
U8      ucBtnA3Count              = 0;
U8      ucBtnA5Count              = 0;

/*PROCEDURE*/ void  Send_Button_DTC       ( UL ulSPN, U8 ucFMI, U8 ucOC );
/*PROCEDURE*/ void  CheckButtonDTCs       ( void );

//----------------------------------------------------------------------------------------------------
// POT and "fPercent" controlled parameter maximums.
//----------------------------------------------------------------------------------------------------

#define MAX_RPM                             8000.00  // [A0] Max engine RPM for simulator.    
#define MAX_VEHICLE_SPEED_KPH                160.92  // [A2] Max vehicle speed for simulator. 
#define MAX_PRESS_KPA                       1000.00  // [A4] Max pressure for simulator.      
#define MAX_TEMP_C                           149.00  // Controlled by fPercent variable (see below).
#define MAX_VOLTAGE                           24.00  // Voltages

//----------------------------------------------------------------------------------------------------
// Constants for non-switch/non-POT values.
//----------------------------------------------------------------------------------------------------

#define TOTAL_PTO_HOURS                         123
#define TOTAL_ENGINE_HOURS                      912
#define TOTAL_VEHICLE_HOURS                    1200
#define TOTAL_ENGINE_REVOLUTIONS    (UL) 1234567000  // x1000=1234567.000
#define NUM_PENDING_FAULTS                        1
#define NUM_ALL_PENDING_FAULTS                    2
#define NUM_MIL_ON_DTCS                           3
#define NUM_INACTIVE_MIL_ON_DTCS                  4
#define NUM_PERMANENT_DTCS                        5
#define TOTAL_TRIP_DISTANCE                   99967
#define TOTAL_VEHICLE_DISTANCE              8965443

//----------------------------------------------------------------------------------------------------
// This is the delay in milliseconds after sending a J1939 message to keep bus utilization down.
//----------------------------------------------------------------------------------------------------

#define J1939_INTERMESSAGE_DELAY_TIME             5 

//----------------------------------------------------------------------------------------------------
// This is the delay in milliseconds after sending a J1587 message.
//
// 104.2 microseconds is about a bit time in J1708/J1587 running at 9600 baud.
// 1042  milliseconds is a byte (10 bits; Start bit/8 data bits/Stop bit).
// 1.042 milliseconds is a byte (10 bits; Start bit/8 data bits/Stop bit).
//
// This code uses this delay after sending a J1708 message to allow for the byte to exit the UART.
// Then it will allow enough delay that the receiver will see 10 idle bits (IDLE) and will allow us to
// transmit the next message without the messages "running" together.
//
// The formula for delaying is based off of the number of bytes sent.
//
//     DelayInMilliseconds = (BYTE_DELAY_J1587 * NumberOfBytesInJ1587Message)
//
//----------------------------------------------------------------------------------------------------

#define BYTE_DELAY_J1587                        1.6 

//----------------------------------------------------------------------------------------------------
// Tradeshow Mode
//----------------------------------------------------------------------------------------------------
// A mechanism was added to show movement of parameters for a tradeshow since a physical ECM is not
// very interesting and sends mostly all zeros.  The "tradeshow mode" is used to increment or
// decrement variables that may appear on a standard dashboard cluster or diagnostic program.
//
// The variable ucDirection controls whether we are incrementing (FORWARD) or decrementing (BACKWARD).
// The variable fPercent is where we are (0..100) based on incrementing or decrementing.  
//----------------------------------------------------------------------------------------------------

#define BACKWARD                   100  // To make gauges go down.
#define FORWARD                    101  // To make gauges go up.
#define INCREMENT           0.00009525  // How much to increment/decrement in each loop() iteration.

U8      ucDirection         = FORWARD;  // Directions for gauges in tradeshow mode.
float   fPercent            =     0.0;  // Where we are in tradeshow mode.

//----------------------------------------------------------------------------------------------------
// Define J1587 fault code structure.
//----------------------------------------------------------------------------------------------------

struct J1587DTC
{
  U8  bIsActive ;  // Active = TRUE, Inactive = FALSE.
  U8  ucCode    ;  // The actual trouble/fault code.
  U8  ucFMI     ;  // Failure Mode Indicator (FMI).
  U8  bIsSID    ;  // Subsystem Identifier (SID) = TRUE, Parameter Idenfier (PID) = FALSE.
  U8  bIsSTD    ;  // If TRUE = Standard Fault, if FALSE = PID Page 2 Fault.
  U8  bHasOC    ;  // Is Occurence Count (OC) included.
  U8  ucOC      ;  // If bHasOC, this is the OC.
};

//----------------------------------------------------------------------------------------------------
// Main functions provided for Arduino.
//----------------------------------------------------------------------------------------------------

/*PROCEDURE*/ void  setup                                   ( void );
/*PROCEDURE*/ void  loop                                    ( void );

//----------------------------------------------------------------------------------------------------
// POT controlled send functions.
//----------------------------------------------------------------------------------------------------

/*PROCEDURE*/ void  Send_PGN_61444_EngineSpeed              ( void );
/*PROCEDURE*/ void  Send_PGN_65265_VehicleSpeed             ( void );
/*PROCEDURE*/ void  Send_PGN_65263_OilPressure              ( void );
/*PROCEDURE*/ void  Send_PID_84_VehicleSpeed                ( void );
/*PROCEDURE*/ void  Send_PID_100_OilPressure                ( void );
/*PROCEDURE*/ void  Send_PID_190_EngineSpeed                ( void );
/*PROCEDURE*/ void  Send_J1587_SwitchStatuses               ( void );
/*PROCEDURE*/ void  Send_PGN_64976_IntakeManifoldPressure ( void );


//----------------------------------------------------------------------------------------------------
// Non-POT, tradeshow mode, controlled send functions based on fPercent variable.
//----------------------------------------------------------------------------------------------------

/*PROCEDURE*/ void  Send_PGN_65262_CoolantTemp              ( float fPercent );
/*PROCEDURE*/ void  Send_PGN_65269_AmbientAirTemp           ( float fPercent );
/*PROCEDURE*/ void  Send_PGN_65270_IntakeManifoldTemp       ( float fPercent );
/*PROCEDURE*/ void  Send_PGN_65271_Voltages                 ( float fPercent );
/*PROCEDURE*/ void  Send_PGN_65272_TransOilTemp             ( float fPercent );

//----------------------------------------------------------------------------------------------------
// Non-POT, non-tradeshow, J1939 fixed values.
//----------------------------------------------------------------------------------------------------

/*PROCEDURE*/ void  Send_PGN_65229_FreezeFrame        ( void );
/*PROCEDURE*/ void  Send_PGN_DM25_ExpandedFreezeFrame       ( void );
/*PROCEDURE*/ void  Send_PGN_65259_ComponentInfo      ( void );
/*PROCEDURE*/ void  Send_PGN_65242_SoftwareID         ( void );
/*PROCEDURE*/ void  Send_PGN_64965_ECUID              ( void );
/*PROCEDURE*/ void  Send_PGN_65260_VIN                ( void );
/*PROCEDURE*/ void  Send_PGN_65253_EngineHoursRevs          ( void );
/*PROCEDURE*/ void  Send_PGN_65248_Distances                ( void );
/*PROCEDURE*/ void  Send_PGN_65217_DistancesHR              ( void );
/*PROCEDURE*/ void  Send_PGN_65255_TotalHours               ( void );
/*PROCEDURE*/ void  Send_PGN_65267_VehiclePosition          ( void );

//----------------------------------------------------------------------------------------------------
// Non-POT, non-tradeshow, J1587 fixed values.
//----------------------------------------------------------------------------------------------------

/*PROCEDURE*/ void  Send_PID_244_TripDistance               ( void );
/*PROCEDURE*/ void  Send_PID_245_TotalVehicleDistance       ( void );
/*PROCEDURE*/ void  Send_PID_246_TotalVehicleHours          ( void );
/*PROCEDURE*/ void  Send_PID_247_EngineHours                ( void );
/*PROCEDURE*/ void  Send_PID_249_EngineRevolutions          ( void );
/*PROCEDURE*/ void  Send_J1587_FuelLevels                   ( void );
/*PROCEDURE*/ void  Send_J1939_FuelLevels                   ( void );

//----------------------------------------------------------------------------------------------------
// DTC functions (J1939).
//----------------------------------------------------------------------------------------------------

/*PROCEDURE*/ void  Send_J1939_DTCs                         ( U8 ucSRC );
/*PROCEDURE*/ void  Send_PGN_40448_DTCCounts         ( void );
/*PROCEDURE*/ void  Send_PGN_64906_J2012DTCs         ( void );

//----------------------------------------------------------------------------------------------------
// DTC functions (J1587).
//----------------------------------------------------------------------------------------------------

/*PROCEDURE*/ void  Build_J1587_DTC_Message                 ( U8 ucMID, struct J1587DTC sFaultCodes[], int iNumFaults, U8 *ucTxMsg, unsigned char ucSizeofTxMsg, unsigned char *ucOutLength );
/*PROCEDURE*/ void  Send_J1587_Engine_DTCs                  ( void );
/*PROCEDURE*/ void  Send_J1587_Transmission_DTCs            ( void );
/*PROCEDURE*/ void  Send_J1587_ABS_DTCs                     ( void );
/*PROCEDURE*/ void  Send_J1587_ComponentInfo                ( void );

//----------------------------------------------------------------------------------------------------
// Generic functions for sending messages (breaking down CANID, PGN, etc).
//----------------------------------------------------------------------------------------------------

/*PROCEDURE*/ void  CANIDToPGNAndParts           ( UL    ulCANID, UL *ulPGN   , U8 *ucPRI, U8 *ucEDP    , U8 *ucDP     , U8 *ucPF  , U8 *ucPS , U8 *ucSRC, U8 *ucDST  );
/*FUNCTION*/  UL    PGNToCANID                   ( UL    ulPGN  , U8  ucPRI   , U8  ucSRC, U8  ucDST                                                                  );
/*PROCEDURE*/ void  PrintJ1939Message            ( char *ucTxRx , UL  ulCANID , UL  ulPGN, U8  ucSRC    , U8  ucDST    , U8 *ucData, U8  ucLen                        );
/*PROCEDURE*/ void  PrintJ1587Message            ( char *ucTxRx , U8  *ucData , U8  ucLen                                                                             );
/*PROCEDURE*/ void  Send_BAM_Message             ( U8    ucPRI  , UL  ulMsgPGN, U8  ucSRC, U8 *ucMsgData, UL  ulDataLen                                               );
/*FUNCTION*/  U8    CalculateJ1587Checksum       ( U8 *ucMessage, short nLength                                                                                       );
/*PROCEDURE*/ void  PrintAnalogAndDigitalInputs  ( void );

//----------------------------------------------------------------------------------------------------
// Function declarations for creating LSB values (parameters) to send (J1939/J1587).
//----------------------------------------------------------------------------------------------------

/*FUNCTION*/  U8   *CreateTwoByteIntegerLSB  ( U16 usVal, U8 *szBuffer );
/*FUNCTION*/  U8   *CreateThreeByteIntegerLSB( UL  ulVal, U8 *szBuffer );
/*FUNCTION*/  U8   *CreateFourByteIntegerLSB ( UL  ulVal, U8 *szBuffer );

//----------------------------------------------------------------------------------------------------
// Functions to send the proprietary PGN from the POTs and switches.
//----------------------------------------------------------------------------------------------------

/*PROCEDURE*/ void Send_Proprietary_PGN_65280_PID_254_ECMSimulatorSwitchStatus( void );

//----------------------------------------------------------------------------------------------------
// Functions to read the analog and digital inputs.
//----------------------------------------------------------------------------------------------------

/*PROCEDURE*/ void ReadAnalogDigitalInputs( void );

//----------------------------------------------------------------------------------------------------
// Declare Cory J Fowler's CAN class at the CAN_CS_PIN.
//----------------------------------------------------------------------------------------------------

MCP_CAN CAN0( CAN_CS_PIN );

//----------------------------------------------------------------------------------------------------
// This allows grouping sends so the bus does not get overwhelmed all at once.  Typically sending
// about five messages per loop works well to keep the bus from being overwhelmed.
//----------------------------------------------------------------------------------------------------

U16     usLoopCount                        = 0; 

//----------------------------------------------------------------------------------------------------
// These variables control whether messages and input pins are debugged to the serial port.  The
// default for debugging is false since it slows down the loop responsible for sending messages.
//----------------------------------------------------------------------------------------------------

U8      ucDebugPrintReceivedJ1939Messages = FALSE; 
U8      ucDebugPrintSentJ1939Messages     = FALSE; 
U8      ucDebugPrintReceivedJ1587Messages = FALSE; 
U8      ucDebugPrintSentJ1587Messages     = FALSE; 
U8      ucDebugPrintAnalogDigitalInputs   = FALSE; 

///----------------------------------------------------------------------------------------------------
///<summary>
///  setup - Arduino provided function to set up the processor, pins, and ports.
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void setup( void )
{
  //
  // Set up the serial port so we can monitor communications.
  //
  
  Serial.begin( RS232_BAUD );
  
  //
  // Print program information to the serial port.
  //
  
  //
  // Print program banner information.
  //
  Serial.println("-----------------------------------------------------------------------------");
  Serial.println(  szProgramInfo                                                                );
  Serial.println(  szCopyrightInfo                                                              );
  Serial.println(  szRevision                                                                   );
  Serial.println(  szDate                                                                       );
  Serial.println("-----------------------------------------------------------------------------");
  
  //
  // Set up SCL pin as an input for the /INT when a CAN message is ready to be read.
  //

  pinMode( CAN_INT_PIN, INPUT );
  
  //
  // Set switch pins to be digital inputs with internal pullup.
  //

  pinMode( A1_PARK_BRAKE_SWITCH   , INPUT_PULLUP );
  pinMode( A3_BRAKE_PEDAL_SWITCH  , INPUT_PULLUP );
  pinMode( A5_CLUTCH_PEDAL_SWITCH , INPUT_PULLUP );
  
  //
  // Set up the J1708 serial port so we can monitor communications (9600, N-8-1).
  //

  Serial1.begin( RS485_BAUD, SERIAL_8N1 );
  
  //
  // Enable J1708/J1587 reception.
  //

  digitalWrite( RS485_RXTXPIN, HIGH );
  
  //
  // Initialize Seeed Studio CAN shield at 16MHz, masks and filters disabled.
  //

  if( CAN_OK != CAN0.begin( MCP_ANY, CAN_BAUD_RATE, MCP_16MHZ ) )
    {
      Serial.println( "Failed to initialize the Seeed Studio CAN shield..." );
    }
  
  //
  // Set operation mode to normal so the MCP2515 sends acks to received data.
  //

  CAN0.setMode( MCP_NORMAL );                     
  
}//setup()

///----------------------------------------------------------------------------------------------------
///<summary>
///  loop() - Arduino provided interrupt function to process data periodically.
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void loop( void )
{
  ProcessSerialCommands();
  //---------------------------------------------------------------------------------------------------- 
  // Always check to see if a CAN message has been received and read the message.  This can be used
  // in the event that the developer needs to create a specific receive/send scenario.
  //----------------------------------------------------------------------------------------------------

  if( ! digitalRead( CAN_INT_PIN ) ) 
    {
      UL ulCANID        = 0;
      UL ulPGN          = 0;
      U8 ucPRI          = 0;
      U8 ucEDP          = 0;
      U8 ucDP           = 0;
      U8 ucPF           = 0;
      U8 ucPS           = 0;
      U8 ucSRC          = 0;
      U8 ucDST          = 0;
      U8 ucLen          = 0;
      U8 ucData   [ 8 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
      
      //
      // Read the message.
      //
      
      CAN0.readMsgBuf( &ulCANID, &ucLen, ucData );
      
      //
      // Convert the CANID into the J1939 parts.
      //

      CANIDToPGNAndParts( ulCANID, &ulPGN, &ucPRI, &ucEDP, &ucDP, &ucPF, &ucPS, &ucSRC, &ucDST  );

      //
      // Print message to the serial port if the user has debugging turned on.
      //

      PrintJ1939Message( (char *) "Rx", ulCANID, ulPGN, ucSRC, ucDST, ucData, ucLen  );

      if( ulPGN == 59904 )
        {
          UL ulPGNRequested = UnPackThreeByteIntegerLSB( &ucData[ 0 ] );

          //
          // If VIN requested, send the VIN.  It is sent in a loop, but this is a simple request example.
          //
          
          if( ulPGNRequested == 65260 )
            {
              Send_PGN_65260_VIN();
            }
          
        }// Request PGN 
      
    }// Reading of a CAN message

  //----------------------------------------------------------------------------------------------------
  // Loop variable controls which messages get sent each interrupt cycle.
  //
  // It is set here so if you want to do something special, you can just set it to a specific "loop"
  // value up front and only execute that code.  For example, a usLoopCount of 1 sends the messages
  // associated with the analog and digital inputs.
  //----------------------------------------------------------------------------------------------------

  usLoopCount = usLoopCount + 1;
  
  //----------------------------------------------------------------------------------------------------
  // Loop 1.  Check inputs and send messages.  Then increment/decrement the fPercent value.
  //----------------------------------------------------------------------------------------------------

  if( usLoopCount == 1 )
    {
      //----------------------------------------------------------------------------------------------------
      // Read the analog and digital inputs.  Note the POT readings are an average of 5 reads.
      //----------------------------------------------------------------------------------------------------
      
      ReadAnalogDigitalInputs();

      //----------------------------------------------------------------------------------------------------
      // Send message(s) affected by the POTs and switches.
      //----------------------------------------------------------------------------------------------------

      Send_PGN_64976_IntakeManifoldPressure ( );  // A0 Pot - Intake Manifold Pressure
      Send_PGN_65262_CoolantTemp            ( );  // A2 Pot - Coolant Temperature
      Send_PGN_65263_FuelDeliveryPressure   ( );  // A4 Pot - Fuel Delivery Pressure
      
      Send_PID_190_EngineSpeed      ( );
      Send_PID_84_VehicleSpeed      ( );
      Send_PID_100_OilPressure      ( );

      Send_J1587_SwitchStatuses     ( );
      
      //----------------------------------------------------------------------------------------------------
      // Send the proprietary PGN/PID to test the switches at the end of line tester.
      //----------------------------------------------------------------------------------------------------
      
      Send_Proprietary_PGN_65280_PID_254_ECMSimulatorSwitchStatus();

      //----------------------------------------------------------------------------------------------------
      // A button press (OFF -> ON) sends exactly one DM1 with one DTC.  Nothing else sends DTCs.
      //----------------------------------------------------------------------------------------------------

      CheckButtonDTCs();

      //----------------------------------------------------------------------------------------------------
      // Broadcast the QA-injected DM1 fault list (PGN 65226) if DTC injection is armed.
      //----------------------------------------------------------------------------------------------------

      if( ucQADTCEnabled )
        {
          Send_QA_DM1();
        }
      
      //----------------------------------------------------------------------------------------------------
      // Increment or decrement fPercent variable so the program can show motion (tradeshow mode).
      //----------------------------------------------------------------------------------------------------

      //
      // QA: the sweep can be frozen from the console so injected DTCs and held sensor values are
      // not competing with four ambient parameters that never stop moving.
      //

      if( ucSweepEnabled )
        {
      if( ucDirection == FORWARD )
        {        
          fPercent = fPercent + INCREMENT;
          
          //
          // If we go over 1, start going backward.
          //
          
          if( fPercent >= 1.00 )
            {
              fPercent = fPercent - INCREMENT;
              
              ucDirection = BACKWARD;
            }
        }
      else 
        {
          fPercent = fPercent - INCREMENT;
          
          //
          // If we go under 0, start going forward.
          //
          
          if( fPercent <= 0.00 )
            {
              fPercent = fPercent + INCREMENT;
              
              ucDirection = FORWARD;
            }
          
        }// BACKWARD
        }// if( ucSweepEnabled )
      
      //---------------------------------------------------------------------------------------------------- 
      // If you want to print out the analog and digital inputs.
      //----------------------------------------------------------------------------------------------------

      if( ucDebugPrintAnalogDigitalInputs )
        {
          PrintAnalogAndDigitalInputs();
        }
      
    }// Loop 1
  
  //----------------------------------------------------------------------------------------------------
  // Loop 2.  Send generic variables controlled by the generic fPercent variable.
  //----------------------------------------------------------------------------------------------------
  
  else if( usLoopCount == 2 )
    {
      Send_PGN_65269_AmbientAirTemp    ( fPercent );
      Send_PGN_65270_IntakeManifoldTemp( fPercent );
      Send_PGN_65271_Voltages          ( fPercent );
      Send_PGN_65272_TransOilTemp      ( fPercent );
      
    }// Loop 2
  
  //----------------------------------------------------------------------------------------------------
  // Loop 3.  
  //----------------------------------------------------------------------------------------------------

  else if( usLoopCount == 3 )
    {
      Send_PGN_65260_VIN();            
      Send_PGN_65259_ComponentInfo();  
      Send_PGN_65242_SoftwareID();
      Send_PGN_64965_ECUID();          

      Send_J1587_ComponentInfo();
      Send_PID_237_VIN();

    }// Loop 3
  
  //----------------------------------------------------------------------------------------------------
  // Loop 4.
  //----------------------------------------------------------------------------------------------------

  else if( usLoopCount == 4 )
    {
      //
      // Constant DTC traffic (the original behaviour).  Set CONTINUOUS_DTC_BROADCAST to 0 at the top
      // of the file to go back to "DTCs only when a button is pressed" (CheckButtonDTCs()).
      //
#if CONTINUOUS_DTC_BROADCAST
      Send_J1939_DTCs( 0 );                    // 22 generic DTCs (DM1, DM2, DM6, ... SPN 1000..1021)
      Send_PGN_64906_J2012DTCs();
      Send_PGN_40448_DTCCounts();
      Send_PGN_DM25_ExpandedFreezeFrame();
#endif
      Send_PGN_DM24_SPNSupport();

      Send_PGN_65248_Distances();
      Send_PGN_65217_DistancesHR();
      Send_PGN_65255_TotalHours();
      Send_PGN_65267_VehiclePosition();
      Send_PGN_65253_EngineHoursRevs();
      
    }// Loop 4
  
  //----------------------------------------------------------------------------------------------------
  // Loop 5.
  //----------------------------------------------------------------------------------------------------

  else if( usLoopCount == 5 )
    {
      Send_PID_190_EngineSpeed();
      Send_PID_244_TripDistance();
      Send_PID_245_TotalVehicleDistance();
      Send_PID_246_TotalVehicleHours();
      Send_PID_247_EngineHours();
      Send_PID_249_EngineRevolutions();

      Send_J1587_Fuel_Levels();
      Send_J1939_Fuel_Levels();
      
    }//Loop 5
  
  //----------------------------------------------------------------------------------------------------
  // This is the last loop, setting the loop variable back to zero to start again.
  //----------------------------------------------------------------------------------------------------
  else
    {
      //
      // Reset loop counter back to zero.
      //
      usLoopCount = 0;
    }

}//loop()

//****************************************************************************************************
//****************************************************************************************************
//****************************************************************************************************
//****************************************************************************************************
//****************************************************************************************************
// Utility Functions
//****************************************************************************************************
//****************************************************************************************************
//****************************************************************************************************
//****************************************************************************************************
//****************************************************************************************************

/*PROCEDURE*/ void HandleSerialCommand( String szCmd )
{
  szCmd.trim();
  szCmd.toUpperCase();

  //--------------------------------------------------------------------------------------------------
  // PING - answered immediately with PONG plus board uptime in ms.  Cleanest proof that the USB
  // read path works end to end: the console sends four bytes and gets back a line it could never
  // have generated locally.
  //--------------------------------------------------------------------------------------------------

  if( szCmd == "PING" )
    {
      Serial.print( "PONG " ); Serial.println( millis() );
      return;
    }

  //--------------------------------------------------------------------------------------------------
  // STATUS - dump current state so the console can resync after reconnecting.
  //--------------------------------------------------------------------------------------------------

  if( szCmd == "STATUS" )
    {
      Serial.print( "ST SWEEP "     ); Serial.println( ucSweepEnabled   ? 1 : 0 );
      Serial.print( "ST POTS_OVR "  ); Serial.println( ucOverrideActive ? 1 : 0 );
      Serial.print( "ST DTC_ARMED " ); Serial.println( ucQADTCEnabled   ? 1 : 0 );
      Serial.print( "ST DTC_COUNT " ); Serial.println( ucQADTCCount );
      Serial.print( "ST DTC_LAMP "  ); Serial.println( ucQADTCLamp  );
      Serial.print( "ST BTN_LAMP "  ); Serial.println( ucBtnDTCLamp );
      Serial.print( "ST SWEEP_PCT " ); Serial.println( fPercent * 100.0 );
      Serial.println( "ST END" );
      return;
    }

  //--------------------------------------------------------------------------------------------------
  // SWEEP ON | SWEEP OFF | SWEEP SET <percent>
  //
  // "Tradeshow mode" drifts ambient air temp, intake manifold temp, voltages and transmission oil
  // temp up and down forever.  Useful on a show floor, actively unhelpful when QA is trying to read
  // a steady value or watch for a fault code.  SWEEP SET parks it at a fixed point.
  //--------------------------------------------------------------------------------------------------

  if( szCmd == "SWEEP ON" )
    {
      ucSweepEnabled = TRUE;
      Serial.println( "ACK SWEEP ON" );
      return;
    }

  if( szCmd == "SWEEP OFF" )
    {
      ucSweepEnabled = FALSE;
      Serial.println( "ACK SWEEP OFF" );
      return;
    }

  if( szCmd.startsWith( "SWEEP SET " ) )
    {
      float fPct = szCmd.substring( 10 ).toFloat();

      ucSweepEnabled = FALSE;
      fPercent       = constrain( fPct, 0, 100 ) / 100.0;

      Serial.print( "ACK SWEEP SET " ); Serial.println( fPct );
      return;
    }


  //--------------------------------------------------------------------------------------------------
  // LIVE ON | LIVE OFF - toggle Tx debug printing on the USB serial port.
  //--------------------------------------------------------------------------------------------------

  if( szCmd == "LIVE ON" )
    {
      ucDebugPrintSentJ1939Messages = TRUE;
      ucDebugPrintSentJ1587Messages = TRUE;
      Serial.println( "ACK LIVE ON" );
      return;
    }

  if( szCmd == "LIVE OFF" )
    {
      ucDebugPrintSentJ1939Messages = FALSE;
      ucDebugPrintSentJ1587Messages = FALSE;
      Serial.println( "ACK LIVE OFF" );
      return;
    }

  //--------------------------------------------------------------------------------------------------
  // RELEASE - hand every channel back to the physical POTs, switches and tradeshow sweep.
  //--------------------------------------------------------------------------------------------------

  if( szCmd == "RELEASE" )
    {
      ucOverrideActive = FALSE;

      fOvrCoolantC    = QA_OVR_UNSET;
      fOvrOilPressKPA = QA_OVR_UNSET;
      fOvrBoostKPA    = QA_OVR_UNSET;
      fOvrVoltage     = QA_OVR_UNSET;
      fOvrTransOilC   = QA_OVR_UNSET;
      fOvrFuelPct     = QA_OVR_UNSET;
      fOvrAmbientC    = QA_OVR_UNSET;

      Serial.println( "ACK RELEASE" );
      return;
    }

  //--------------------------------------------------------------------------------------------------
  // AUTO <channel> - return one single channel to its firmware default.
  //--------------------------------------------------------------------------------------------------

  if( szCmd.startsWith( "AUTO " ) )
    {
      String szChan = szCmd.substring( 5 );
      szChan.trim();

      if(      szChan == "COOLANT"  ) fOvrCoolantC    = QA_OVR_UNSET;
      else if( szChan == "OILPRESS" ) fOvrOilPressKPA = QA_OVR_UNSET;
      else if( szChan == "BOOST"    ) fOvrBoostKPA    = QA_OVR_UNSET;
      else if( szChan == "VOLT"     ) fOvrVoltage     = QA_OVR_UNSET;
      else if( szChan == "TRANSOIL" ) fOvrTransOilC   = QA_OVR_UNSET;
      else if( szChan == "FUEL"     ) fOvrFuelPct     = QA_OVR_UNSET;
      else if( szChan == "AMBIENT"  ) fOvrAmbientC    = QA_OVR_UNSET;
      else if( szChan == "POTS"     ) ucOverrideActive = FALSE;
      else
        {
          Serial.print( "ERR Unknown channel: " ); Serial.println( szChan );
          return;
        }

      Serial.print( "ACK " ); Serial.println( szCmd );
      return;
    }

  //--------------------------------------------------------------------------------------------------
  // DTC commands - inject / clear active J1939 DM1 fault codes.
  //--------------------------------------------------------------------------------------------------

  if( szCmd == "DTC ON" )
    {
      ucQADTCEnabled = TRUE;
      Serial.println( "ACK DTC ON" );
      return;
    }

  if( szCmd == "DTC OFF" )
    {
      ucQADTCEnabled = FALSE;
      Serial.println( "ACK DTC OFF" );
      return;
    }

  if( szCmd == "DTC CLEAR" )
    {
      ucQADTCCount = 0;
      Serial.println( "ACK DTC CLEAR COUNT=0" );
      return;
    }

  if( szCmd.startsWith( "DTC LAMP " ) )
    {
      ucQADTCLamp = (U8) szCmd.substring( 9 ).toInt();
      Serial.print( "ACK DTC LAMP " ); Serial.println( ucQADTCLamp );
      return;
    }

  // BTN LAMP <0..255> - lamp status byte (severity) of the DTC sent by each switch press.
  if( szCmd.startsWith( "BTN LAMP " ) )
    {
      ucBtnDTCLamp = (U8) szCmd.substring( 9 ).toInt();
      Serial.print( "ACK BTN LAMP " ); Serial.println( ucBtnDTCLamp );
      return;
    }

  if( szCmd.startsWith( "DTC ADD " ) )
    {
      String szArgs = szCmd.substring( 8 );
      int    iSp1   = 0;
      int    iSp2   = 0;
      UL     ulSPN  = 0;
      U8     ucFMI  = 0;
      U8     ucOC   = 1;

      szArgs.trim();

      iSp1 = szArgs.indexOf( ' ' );

      if( iSp1 < 0 )
        {
          Serial.println( "ERR DTC ADD needs <spn> <fmi> [oc]" );
          return;
        }

      if( ucQADTCCount >= MAX_QA_DTCS )
        {
          Serial.print( "ERR DTC table full, max " ); Serial.println( MAX_QA_DTCS );
          return;
        }

      ulSPN = (UL) szArgs.substring( 0, iSp1 ).toInt();

      String szRest = szArgs.substring( iSp1 + 1 );
      szRest.trim();

      iSp2 = szRest.indexOf( ' ' );

      if( iSp2 < 0 )
        {
          ucFMI = (U8) szRest.toInt();
        }
      else
        {
          ucFMI = (U8) szRest.substring( 0, iSp2 ).toInt();
          ucOC  = (U8) szRest.substring( iSp2 + 1 ).toInt();
        }

      if( ulSPN > 524287UL )   // SPN is 19 bits
        {
          Serial.println( "ERR SPN out of range (0..524287)" );
          return;
        }

      sQADTCs[ ucQADTCCount ].ulSPN = ulSPN;
      sQADTCs[ ucQADTCCount ].ucFMI = ( ucFMI & 0x1F );
      sQADTCs[ ucQADTCCount ].ucOC  = ( ucOC  & 0x7F );

      ucQADTCCount++;
      ucQADTCEnabled = TRUE;

      Serial.print( "ACK DTC ADD SPN=" ); Serial.print( ulSPN   );
      Serial.print( " FMI="            ); Serial.print( ucFMI   );
      Serial.print( " OC="             ); Serial.print( ucOC    );
      Serial.print( " COUNT="          ); Serial.println( ucQADTCCount );
      return;
    }

  //--------------------------------------------------------------------------------------------------
  // SET <channel> <value>
  //
  //   POT / switch channels (value 0..100, or 0/1 for switches):
  //     ENG VEH OIL PARK BRAKE CLUTCH
  //
  //   Extended sensor channels (value in engineering units):
  //     COOLANT  deg C     OILPRESS kPa      BOOST kPa
  //     VOLT     volts     TRANSOIL deg C    FUEL  percent    AMBIENT deg C
  //--------------------------------------------------------------------------------------------------

  if( szCmd.startsWith( "SET " ) )
    {
      int    iSep = szCmd.indexOf( ' ', 4 );
      String szChan;
      float  fVal = 0;

      if( iSep < 0 )
        {
          Serial.println( "ERR SET needs <channel> <value>" );
          return;
        }

      szChan = szCmd.substring( 4, iSep );
      fVal   = szCmd.substring( iSep + 1 ).toFloat();

      //
      // Only the POT/switch channels take over ReadAnalogDigitalInputs().  Setting an extended
      // sensor channel must NOT hijack the physical knobs.
      //

      if( ( szChan == "ENG"   ) || ( szChan == "VEH"   ) || ( szChan == "OIL"    ) ||
          ( szChan == "PARK"  ) || ( szChan == "BRAKE" ) || ( szChan == "CLUTCH" ) )
        {
          ucOverrideActive = TRUE;
        }

      if(      szChan == "ENG"      ) fOverrideEngPct  = constrain( fVal,    0,  100 );
      else if( szChan == "VEH"      ) fOverrideVehPct  = constrain( fVal,    0,  100 );
      else if( szChan == "OIL"      ) fOverrideOilPct  = constrain( fVal,    0,  100 );
      else if( szChan == "PARK"     ) ucOverridePark   = ( fVal != 0 ) ? ON : OFF;
      else if( szChan == "BRAKE"    ) ucOverrideBrake  = ( fVal != 0 ) ? ON : OFF;
      else if( szChan == "CLUTCH"   ) ucOverrideClutch = ( fVal != 0 ) ? ON : OFF;
      else if( szChan == "COOLANT"  ) fOvrCoolantC     = constrain( fVal,  -40,  210 );
      else if( szChan == "OILPRESS" ) fOvrOilPressKPA  = constrain( fVal,    0, 1000 );
      else if( szChan == "BOOST"    ) fOvrBoostKPA     = constrain( fVal,    0,  500 );
      else if( szChan == "VOLT"     ) fOvrVoltage      = constrain( fVal,    0,   32 );
      else if( szChan == "TRANSOIL" ) fOvrTransOilC    = constrain( fVal,  -40,  210 );
      else if( szChan == "FUEL"     ) fOvrFuelPct      = constrain( fVal,    0,  100 );
      else if( szChan == "AMBIENT"  ) fOvrAmbientC     = constrain( fVal,  -40,  210 );
      else
        {
          Serial.print( "ERR Unknown channel: " ); Serial.println( szChan );
          return;
        }

      Serial.print( "ACK " ); Serial.println( szCmd );
      return;
    }

  Serial.print( "ERR Unknown command: " ); Serial.println( szCmd );

}//HandleSerialCommand

/*PROCEDURE*/ void ProcessSerialCommands( void )
{
  while( Serial.available() > 0 )
    {
      char c = (char) Serial.read();

      if( ( c == '\n' ) || ( c == '\r' ) )
        {
          if( szSerialCmdBuffer.length() > 0 )
            {
              HandleSerialCommand( szSerialCmdBuffer );
              szSerialCmdBuffer = "";
            }
        }
      else
        {
          szSerialCmdBuffer += c;

          if( szSerialCmdBuffer.length() > 40 )   // guard against runaway input
            {
              szSerialCmdBuffer = "";
            }
        }
    }
}//ProcessSerialCommands

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_QA_DM1 - Broadcast the QA-injected active DTC list as a J1939 DM1 (PGN 65226).
///
///   DM1 layout (SAE J1939-73):
///
///     Byte 1      Lamp status        (MIL / red stop / amber warning / protect)
///     Byte 2      Flash lamp status
///     Byte 3      SPN, 8 least significant bits
///     Byte 4      SPN, second byte
///     Byte 5      bits 8-6 = SPN 3 most significant bits, bits 5-1 = FMI
///     Byte 6      bit 8    = SPN conversion method, bits 7-1 = occurrence count
///     Byte 7-8    0xFF padding (single-DTC frame)
///
///   One DTC fits in a single 8-byte frame.  Two or more require 2 + (4 * n) bytes and therefore
///   go out over BAM transport, which Send_BAM_Message() already handles.
///
///   Note this packs the SPN correctly, including the 3 most significant bits.  The pre-existing
///   Send_J1939_DTCs() in this file overwrites those bits with the FMI, which works only because
///   its synthetic SPNs (1000..1021) fit inside 16 bits.
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_QA_DM1( void )
{
  U8 ucPRI               = 6;
  UL ulPGN               = 65226;
  UL ulCANID             = 0;
  U8 ucSRC               = 0;
  U8 ucDST               = 0xFF;
  U8 ucIdx               = 0;
  UL ulLen               = 0;
  U8 ucData      [ 2 + ( 4 * MAX_QA_DTCS ) ];

  //
  // Nothing injected: send the standard "no active faults" DM1 so a receiving tool sees the
  // fault list actively cleared rather than simply going quiet.
  //

  ulCANID = PGNToCANID( ulPGN, ucPRI, ucSRC, ucDST );

  memset( ucData, 0xFF, sizeof( ucData ) );

  if( 0 == ucQADTCCount )
    {
      U8 ucNone[ 8 ] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF };

      CAN0.sendMsgBuf( ulCANID, 1, 8, ucNone ); delay( J1939_INTERMESSAGE_DELAY_TIME );

      PrintJ1939Message( (char *) "Tx", ulCANID, ulPGN, ucSRC, ucDST, ucNone, 8 );

      return;
    }

  //
  // Byte 1/2 - lamp status and flash lamp status.
  //

  ucData[ 0 ] = ucQADTCLamp;
  ucData[ 1 ] = 0x00;

  //
  // Byte 3..n - four bytes per DTC.
  //

  for( ucIdx = 0; ucIdx < ucQADTCCount; ucIdx++ )
    {
      UL ulSPN = sQADTCs[ ucIdx ].ulSPN;
      U8 ucFMI = sQADTCs[ ucIdx ].ucFMI;
      U8 ucOC  = sQADTCs[ ucIdx ].ucOC;

      ucData[ 2 + ( ucIdx * 4 ) + 0 ] = (U8) (   ulSPN         & 0xFF );
      ucData[ 2 + ( ucIdx * 4 ) + 1 ] = (U8) ( ( ulSPN >>  8 ) & 0xFF );
      ucData[ 2 + ( ucIdx * 4 ) + 2 ] = (U8) ( ( ( ( ulSPN >> 16 ) & 0x07 ) << 5 ) | ( ucFMI & 0x1F ) );
      ucData[ 2 + ( ucIdx * 4 ) + 3 ] = (U8) ( ucOC & 0x7F );  // bit 8 = SPN conversion method = 0
    }

  ulLen = 2 + ( (UL) ucQADTCCount * 4 );

  //
  // Up to 8 bytes goes out as a single frame, anything larger over BAM transport.
  //

  if( ulLen <= 8 )
    {
      U8 ucFrame[ 8 ];

      memset( ucFrame, 0xFF, 8 );
      memcpy( ucFrame, ucData, ulLen );

      CAN0.sendMsgBuf( ulCANID, 1, 8, ucFrame ); delay( J1939_INTERMESSAGE_DELAY_TIME );

      PrintJ1939Message( (char *) "Tx", ulCANID, ulPGN, ucSRC, ucDST, ucFrame, 8 );
    }
  else
    {
      Send_BAM_Message( ucPRI, ulPGN, ucSRC, ucData, ulLen );
    }

}//Send_QA_DM1


///----------------------------------------------------------------------------------------------------
///<summary>
///   PrintAnalogAndDigitalInputs
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void PrintAnalogAndDigitalInputs( void )
{
  Serial.print( "A0 Value              = [" );  Serial.print( usA0_EngineSpeedPosition  )               ; Serial.println( "]" ); 
  Serial.print( "A2 Value              = [" );  Serial.print( usA2_VehicleSpeedPosition )               ; Serial.println( "]" ); 
  Serial.print( "A4 Value              = [" );  Serial.print( usA4_OilPressurePosition  )               ; Serial.println( "]" );

  Serial.print( "A0 = Engine Speed  %  = [" );  Serial.print( fA0_EngineSpeedPercent  * 100.0 )         ; Serial.println( "]" ); 
  Serial.print( "A2 = Vehicle Speed %  = [" );  Serial.print( fA2_VehicleSpeedPercent * 100.0 )         ; Serial.println( "]" ); 
  Serial.print( "A4 = Oil Pressure  %  = [" );  Serial.print( fA4_OilPressurePercent  * 100.0 )         ; Serial.println( "]" );
  
  Serial.print( "A1 = DTC btn (alert 3554)  0/1 = [" );  Serial.print( ucBtnA1Pressed == ON ? "On" : "Off" ); Serial.println( "]" );
  Serial.print( "A3 = DTC btn (alert 3107)  0/1 = [" );  Serial.print( ucBtnA3Pressed == ON ? "On" : "Off" ); Serial.println( "]" );
  Serial.print( "A5 = DTC btn (alert 17858) 0/1 = [" );  Serial.print( ucBtnA5Pressed == ON ? "On" : "Off" ); Serial.println( "]" );
  
  Serial.println( "" );
  
}//PrintAnalogAndDigitalInputs

///----------------------------------------------------------------------------------------------------
///<summary>
///   CANIDToPGNAndParts - Get J1939 fields from a CANID.
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void CANIDToPGNAndParts( UL ulCANID, UL *ulPGN, U8 *ucPRI, U8 *ucEDP, U8 *ucDP, U8 *ucPF, U8 *ucPS, U8 *ucSRC, U8 *ucDST  )
{
  //
  // Mask out SRC, PS, PF, DP, EDP, PRI.
  //
  
  *ucSRC = (U8) ( ( ulCANID & 0x000000FF ) >>  0 ); // Source address in bits  8,  7,  6,  5,  4,  3,  2,  1
  *ucPS  = (U8) ( ( ulCANID & 0x0000FF00 ) >>  8 ); // PDU Specific in bits   16, 15, 14, 13, 12, 11, 10,  9      
  *ucPF  = (U8) ( ( ulCANID & 0x00FF0000 ) >> 16 ); // PDU Format in bits     24, 23, 22, 21, 20, 19, 18, 17
  *ucDP  = (U8) ( ( ulCANID & 0x01000000 ) >> 24 ); // Data Page in bit       25
  *ucEDP = (U8) ( ( ulCANID & 0x02000000 ) >> 25 ); // Ext Data Page in bit   26
  *ucPRI = (U8) ( ( ulCANID & 0x1C000000 ) >> 26 ); // Priority in bits       29, 28, 27

  //
  // Mask out the PGN.
  //
  
  *ulPGN  =     ( ( ulCANID & 0x03FFFF00 ) >>  8 ); // PGN in bits 26 .. 9
  
  // 
  // if PF >= 240 then PDU2-Global, keep PS as part of PGN
  //
  
  if( *ucPF >= 240 )
    {
      //
      // DST = Global
      //
      
      *ucDST = 255;  
    }
  else
    {
      //
      // DST = PS
      //
      
      *ucDST = *ucPS;     
      *ulPGN  = ( *ulPGN & 0xFFFF00 );
    }
  
}//CANIDToPGNAndParts

///----------------------------------------------------------------------------------------------------
///<summary>
///   PGNToCANID - Convert a PGN, PRI, SRC, DST into a CANID.
///</summary>
///----------------------------------------------------------------------------------------------------
/*FUNCTION*/ UL PGNToCANID( UL ulPGN, U8 ucPRI, U8 ucSRC, U8 ucDST )
{
  UL ulCANID         = 0x00000000;
  U8 ucPDUFormat     = 0x00;
  U8 ucPDUSpecific   = 0x00;
  U8 ucDataPage      = 0x00;        
  U8 ucExtDataPage   = 0x00;        
  
  ucPDUSpecific = ( ( ulPGN & 0x0000FF ) >>  0 );    // PDU Specific in bits        8,  7,  6,  5,  4,  3,  2,  1
  ucPDUFormat   = ( ( ulPGN & 0x00FF00 ) >>  8 );    // PDU Format in bits         16, 15, 14, 13, 12, 11, 10,  9 
  ucDataPage    = ( ( ulPGN & 0x010000 ) >> 16 );    // Data Page in bit           17
  ucExtDataPage = ( ( ulPGN & 0x020000 ) >> 17 );    // Extended Data Page in bit  18
  
  ucPRI         = ( ucPRI         & 0x07 );          // Only 3 bits for priority  
  ucExtDataPage = ( ucExtDataPage & 0x01 );          // Only 1 bit for EDP         
  ucDataPage    = ( ucDataPage    & 0x01 );          // Only 1 bit for DP          
  
  ulCANID      |= (UL)( (UL) ucPRI         << 26 );  // Priority in bits           29, 28, 27
  ulCANID      |= (UL)( (UL) ucExtDataPage << 25 );  // Extended Data Page in bits 26
  ulCANID      |= (UL)( (UL) ucDataPage    << 24 );  // Data page in bit           25
  ulCANID      |= (UL)( (UL) ucPDUFormat   << 16 );  // PDU Format in bits         24, 23, 22, 21, 20, 19, 18, 17
  
  if( ucPDUFormat <= 239 )
    {
      ulCANID  |= ( (UL) ucDST           <<  8 );    // If PDU1 Dest Specific, Use the destination in byte 1
    }
  else
    {
      ulCANID  |= ( (UL) ucPDUSpecific   <<  8 );    // PDU2 - Global, PDU Specific in byte 2
    }
  
  ulCANID      |= ( ucSRC );                         // Source Address in bits      8,  7,  6,  5,  4,  3,  2,  1
  
  return ulCANID;
  
}//PGNToCANID

///----------------------------------------------------------------------------------------------------
///<summary>
///   PrintJ1939Message
///
///       Rx CANID[0xFFFFFFFF] PGN[0xFFFFFF] SRC[FF] DST[FF] DATA[FF][FF][FF][FF][FF][FF][FF][FF]
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void PrintJ1939Message( char *ucTxRx, UL ulCANID, UL ulPGN, U8 ucSRC, U8 ucDST, U8 *ucData, U8 ucDataLen  )
{
  U8  ucCANIDByte0 = 0;
  U8  ucCANIDByte1 = 0;
  U8  ucCANIDByte2 = 0;
  U8  ucCANIDByte3 = 0;
  
  U8  ucPGNByte0   = 0;
  U8  ucPGNByte1   = 0;
  U8  ucPGNByte2   = 0;
  
  //
  // If we print received messages.
  //
  
  if( 0 == strcmp( ucTxRx, "Rx" ) )
    {
      if( FALSE == ucDebugPrintReceivedJ1939Messages )
        {
          return;
        }
    }
  
  //
  // If we print transmitted messages.
  //
  
  if( 0 == strcmp( ucTxRx, "Tx" ) )
    {
      if( FALSE == ucDebugPrintSentJ1939Messages )
        {
          return;
        }
    }
  
  //
  // Serial.print() is not like printf() so have to make do to make the string the same lengths.
  //

  ucCANIDByte0 = ( ulCANID & 0x000000FF ) >>  0;
  ucCANIDByte1 = ( ulCANID & 0x0000FF00 ) >>  8;
  ucCANIDByte2 = ( ulCANID & 0x00FF0000 ) >> 16;
  ucCANIDByte3 = ( ulCANID & 0xFF000000 ) >> 24;
  
  //
  // Serial.print() is not like printf() so have to make do to make the string the same lengths.
  //

  ucPGNByte0 = ( ulPGN & 0x000000FF ) >>  0;
  ucPGNByte1 = ( ulPGN & 0x0000FF00 ) >>  8;
  ucPGNByte2 = ( ulPGN & 0x00FF0000 ) >> 16;
  
  //
  // Print the CANID.
  //

  Serial.print( ucTxRx    ); Serial.print( " J1939 " ); Serial.print( "CANID["   );
  if( ucCANIDByte3 < 0x10 ) Serial.print( "0" ); Serial.print( ucCANIDByte3, HEX );
  if( ucCANIDByte2 < 0x10 ) Serial.print( "0" ); Serial.print( ucCANIDByte2, HEX );
  if( ucCANIDByte1 < 0x10 ) Serial.print( "0" ); Serial.print( ucCANIDByte1, HEX );
  if( ucCANIDByte0 < 0x10 ) Serial.print( "0" ); Serial.print( ucCANIDByte0, HEX );
  Serial.print( "] " );
  
  //
  // Print the PGN.
  //

  Serial.print( "PGN[0x" );
  if( ucPGNByte2 < 0x10 ) Serial.print( "0" ); Serial.print( ucPGNByte2, HEX );
  if( ucPGNByte1 < 0x10 ) Serial.print( "0" ); Serial.print( ucPGNByte1, HEX );
  if( ucPGNByte0 < 0x10 ) Serial.print( "0" ); Serial.print( ucPGNByte0, HEX );
  Serial.print( "|"  );  Serial.print( ulPGN );  Serial.print( "] " );
  
  //
  // Print the Source and Destination address.
  //

  Serial.print( "SRC[" ); if( ucSRC < 0x10 ) Serial.print( "0" ); Serial.print( ucSRC, HEX ); Serial.print( "] " );
  Serial.print( "DST[" ); if( ucDST < 0x10 ) Serial.print( "0" ); Serial.print( ucDST, HEX ); Serial.print( "] " );
  
  //
  // Print the data bytes.
  //

  Serial.print( "DATA" );
  
  for( int i = 0; i < ucDataLen; i++ )
    {
      Serial.print( "[" ); if( ucData[ i ] < 0x10 ) Serial.print( "0" ); Serial.print( ucData[ i ], HEX ); Serial.print( "]" );
    }
  
  //
  // Print a newline.
  //

  Serial.println();
  
}//PrintJ1939Message

///----------------------------------------------------------------------------------------------------
///<summary>
///   PrintJ1587Message
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void PrintJ1587Message( char *ucTxRx , U8 *ucData , U8 ucLen )
{
  //
  // If we print received messages.
  //

  if( 0 == strcmp( ucTxRx, "Rx" ) )
    {
      if( FALSE == ucDebugPrintReceivedJ1587Messages )
        {
          return;
        }
    }
  
  //
  // If we print transmitted messages.
  //

  if( 0 == strcmp( ucTxRx, "Tx" ) )
    {
      if( FALSE == ucDebugPrintSentJ1587Messages )
        {
          return;
        }
    }
  
  //
  // Print the CANID.
  //

  Serial.print( ucTxRx ); Serial.print( " J1587 " );
  Serial.print( "MID[" ); Serial.print( ucData[ 0 ] ); Serial.print( "] " );
  Serial.print( "PID[" ); Serial.print( ucData[ 1 ] ); Serial.print( "] " );
  
  //
  // Print the data bytes.
  //

  Serial.print( "DATA" );
  
  for( int i = 0; i < ( ucLen - 2 ); i++ )
    {
      Serial.print( "[" ); if( ucData[i+2] < 0x10 ) Serial.print( "0" ); Serial.print( ucData[i+2], HEX ); Serial.print( "]" );
    }
  
  //
  // Print a newline.
  //

  Serial.println();
  
}//PrintJ1587Message

///----------------------------------------------------------------------------------------------------
///<summary>
///   CalculateJ1587Checksum
///</summary>
///----------------------------------------------------------------------------------------------------
/*FUNCTION*/ U8 CalculateJ1587Checksum( U8 *ucMessage, short nLength )
{
  int    i         = 0;
  short  nCheckSum = 0;

  //
  // The checksum is the twos complement of the sum of the 8 bit data bytes.  One byte in length.
  //

  for( i = 0; i < nLength; i++ )
    {
      nCheckSum = ( nCheckSum + ucMessage[ i ] ) % 256 ;
    }
  
  return( (U8) ( 256 - nCheckSum ) );
  
}//CalculateJ1587Checksum

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_BAM_Message  - Send a J1939 message greater than 8 bytes in length.
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_BAM_Message( U8 ucPRI, UL ulMsgPGN, U8 ucSRC, U8 *ucMsgData, UL ulDataLen )
{
  //
  // Annoucement message.
  //

  U8 ucBAM_Announcement_PRI    = ucPRI;
  U8 ucBAM_Announcement_SRC    = ucSRC;
  U8 ucBAM_Announcement_DST    = 0xFF;
  UL ulBAM_Announcement_PGN    = 60416;
  UL ulBAM_Announcement_CANID  = 0;
  
  //
  // Data transfer message.
  //

  U8 ucBAM_DataTransfer_PRI    = ucPRI;
  U8 ucBAM_DataTransfer_SRC    = ucSRC;
  U8 ucBAM_DataTransfer_DST    = 0xFF;
  UL ulBAM_DataTransfer_PGN    = 60160;
  UL ulBAM_DataTransfer_CANID  = 0;
  
  //
  // The number of packes to send.  There are 7 bytes per packet, round up.
  //

  U8 ucTotalPackets            = (U8) ceil( ( (double) ulDataLen / (double) 7.00 ) ); 
  U8 ucSequenceNumber          = 0;
  U8 ucPacketNum               = 0;
  
  //
  // Start copying bytes of the message starting at index 0.  Increment from there.
  //

  U8 *ucMsgPtr                 = &ucMsgData[ 0 ];
  
  //
  // How many data bytes of the packet have been sent.  How many bytes left.
  //
  
  UL  ulDataBytesSent          = 0;
  UL  ulBytesLeftToSend        = 0;
  
  //
  // 8-byte message to send.
  //

  U8 ucData              [ 8 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  
  //
  // Calculate the CANID for the announcement/data transfer PGNs.
  //

  ulBAM_Announcement_CANID  = PGNToCANID( ulBAM_Announcement_PGN , ucBAM_Announcement_PRI , ucBAM_Announcement_SRC , ucBAM_Announcement_DST );
  ulBAM_DataTransfer_CANID  = PGNToCANID( ulBAM_DataTransfer_PGN , ucBAM_DataTransfer_PRI , ucBAM_DataTransfer_SRC , ucBAM_DataTransfer_DST );
  
  //----------------------------------------------------------------------------------------------------
  // PGN 60416
  //          [ 0 ]       = Control Byte = 32 = BAM
  //          [ 1 ][ 2 ]    = Message Size LSB
  //          [ 3 ]       = Total Packets
  //          [ 4 ]       = 0xFF
  //          [ 5 ][ 6 ][ 7 ] = PGN of message being sent.
  //----------------------------------------------------------------------------------------------------
  
  //
  // ucData[ 0 ] = Control byte = 32 = BAM.
  //

  ucData[ 0 ] = 32;
  
  //
  // ucData[1..2] = Message size in bytes.
  //

  CreateTwoByteIntegerLSB( ulDataLen, &ucData[ 1 ] );
  
  //
  // ucData[ 3 ] = Total packets.
  //

  ucData[ 3 ] = ucTotalPackets;
  
  //
  // ucData[ 4 ] = Reserved = 0xFF.
  //

  ucData[ 4 ] = 0xFF;
  
  //
  // ucData[5..7] = PGN of message being sent.
  //

  CreateThreeByteIntegerLSB( ulMsgPGN, &ucData[ 5 ] );
  
  //
  // Send the announcement, then delay.
  //

  CAN0.sendMsgBuf( ulBAM_Announcement_CANID, 1, 8, ucData );  delay( J1939_INTERMESSAGE_DELAY_TIME );
  
  //
  // Print message to Arduino serial port.
  //

  PrintJ1939Message( (char *) "Tx", ulBAM_Announcement_CANID, ulBAM_Announcement_PGN, ucBAM_Announcement_SRC, ucBAM_Announcement_DST, ucData, 8 );
  
  //----------------------------------------------------------------------------------------------------
  // PGN 60160
  //          [ 0 ]       = Sequence number (1..x)
  //          [1..7]    = Data - up to 7 bytes
  //----------------------------------------------------------------------------------------------------
  
  for( ucPacketNum = 0, ucSequenceNumber = 1; ucPacketNum < ucTotalPackets; ucPacketNum++, ucSequenceNumber++ )
    {
      //
      // Set all unused bytes to 0xFF
      //
      
      memset( ucData, 0xFF, sizeof( ucData ) );
      
      //
      // ucData[ 0 ] = Sequence number.
      //

      ucData[ 0 ] = ucSequenceNumber;
      
      //
      // How many bytes are left to send.
      //

      ulBytesLeftToSend = ( ulDataLen - ulDataBytesSent );
      
      //
      // If there are more than 7 bytes left, copy 7 bytes and increment pointers.
      //

      if( ulBytesLeftToSend > 7 )
        {
          memcpy( &ucData[ 1 ], ucMsgPtr, 7 );
          
          ulDataBytesSent += 7;
          
          ucMsgPtr        += 7;
        }
      else
        {
          //
          // Last packet since there are less than 7 bytes to send.
          //
          
          memcpy( &ucData[ 1 ], ucMsgPtr, ulBytesLeftToSend );
        }
      
      //
      // Send the Data Transfer (CM.DT) message and then delay.
      //
      
      CAN0.sendMsgBuf( ulBAM_DataTransfer_CANID, 1, 8, ucData );  delay( J1939_INTERMESSAGE_DELAY_TIME );
      
      //
      // Print message to Arduino serial port.
      //

      PrintJ1939Message( (char *) "Tx", ulBAM_DataTransfer_CANID, ulBAM_DataTransfer_PGN, ucBAM_DataTransfer_SRC, ucBAM_DataTransfer_DST, ucData, 8 );
      
    }// for all packets
  
}// Send_BAM_Message

///----------------------------------------------------------------------------------------------------
///<summary>
///   CreateTwoByteIntegerLSB
///</summary>
///----------------------------------------------------------------------------------------------------
/*FUNCTION*/ U8 *CreateTwoByteIntegerLSB( U16 usVal, U8 *szBuffer )
{
  UL ulVal = usVal;
  
  szBuffer[ 0 ] = (U8) BYTE0_OF_UL( ulVal );
  szBuffer[ 1 ] = (U8) BYTE1_OF_UL( ulVal );
  
  return ( szBuffer );
  
}// CreateTwoByteIntegerLSB

///----------------------------------------------------------------------------------------------------
///<summary>
///   CreateThreeByteIntegerLSB
///</summary>
///----------------------------------------------------------------------------------------------------
/*FUNCTION*/ U8 *CreateThreeByteIntegerLSB( UL ulVal, U8 *szBuffer )
{
  szBuffer[ 0 ] = (U8) BYTE0_OF_UL( ulVal );
  szBuffer[ 1 ] = (U8) BYTE1_OF_UL( ulVal );
  szBuffer[ 2 ] = (U8) BYTE2_OF_UL( ulVal );
  
  return ( szBuffer );
  
}// CreateThreeByteIntegerLSB

///----------------------------------------------------------------------------------------------------
///<summary>
///   CreateFourByteIntegerLSB
///</summary>
///----------------------------------------------------------------------------------------------------
/*FUNCTION*/ U8 *CreateFourByteIntegerLSB( UL ulVal, U8 *szBuffer )
{
  szBuffer[ 0 ] = (U8) BYTE0_OF_UL( ulVal );
  szBuffer[ 1 ] = (U8) BYTE1_OF_UL( ulVal );
  szBuffer[ 2 ] = (U8) BYTE2_OF_UL( ulVal );
  szBuffer[ 3 ] = (U8) BYTE3_OF_UL( ulVal );
  
  return ( szBuffer );
  
}// CreateFourByteIntegerLSB

//****************************************************************************************************
//****************************************************************************************************
//****************************************************************************************************
//****************************************************************************************************
//****************************************************************************************************
// J1587 Messages
//****************************************************************************************************
//****************************************************************************************************
//****************************************************************************************************
//****************************************************************************************************
//****************************************************************************************************

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_PID_84_VehicleSpeed - Vehicle speed based off of POT A2.
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_PID_84_VehicleSpeed( void )
{
  //
  // Generic variables.
  //
  
  U8  ucBytesWritten = 0;
  UL  ulVehicleSpeed = 0;
  
  //
  // Set up a message to send. MID=128, PID=84
  //

  U8  ucMsg[ 15 ]    = { 128, 84, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
  
  //
  // Calculate vehicle speed based off POT A2.
  //

  ulVehicleSpeed = (UL) ( ( (float) MAX_VEHICLE_SPEED_KPH * (float) fA2_VehicleSpeedPercent ) / (float) 0.805 );
  
  //
  // Put it in byte 2.
  //

  ucMsg[ 2 ] = (U8) ulVehicleSpeed;
  
  //
  // Calculate the checksum for the message.
  //

  ucMsg[ 3 ] = CalculateJ1587Checksum( ucMsg, 3 );
  
  //
  // Turn the RS485 pin to LOW to enable transmission.
  //

  digitalWrite( RS485_RXTXPIN, LOW ); 
  
  //
  // Send the J1708/J1587 message.
  //

  ucBytesWritten = Serial1.write( ucMsg, 4 );
  
  if( 4 != ucBytesWritten )
    {
      Serial.print ( "Error writing to the serial port.\r\n" ); 
    }
  else
    {
      PrintJ1587Message( (char *) "Tx", ucMsg, ucBytesWritten );
    }
  
  //
  // Turn the RS485 pin to HIGH to disable transmission (enable reception).
  //

  digitalWrite( RS485_RXTXPIN, HIGH );
  
  //
  // Delay to keep the UART from getting backed up.
  //

  delay( ucBytesWritten * BYTE_DELAY_J1587 );
  
}//Send_PID_84_VehicleSpeed

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_PID_100_OilPressure - Oil pressure based off of POT A4.
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_PID_100_OilPressure( void )
{
  //
  // Generic variables.
  //
  
  U8  ucBytesWritten       = 0;
  UL  ulEngineOilPressure  = 0;
  
  //
  // Set up a message to send.  MID=128, PID=100
  //

  U8  ucMsg[ 15 ]          = { 128, 100, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
  
  //
  // Calculate oil pressure based off POT A4.
  //

  ulEngineOilPressure = (UL) ( ( (float) MAX_PRESS_KPA * (float) fA4_OilPressurePercent ) / (float) 3.45 );
  
  //
  // Put it in byte 2.
  //

  ucMsg[ 2 ] = (U8) ulEngineOilPressure;
  
  //
  // Calculate the checksum for the message.
  //

  ucMsg[ 3 ] = CalculateJ1587Checksum( ucMsg, 3 );
  
  //
  // Turn the RS485 pin to LOW to enable transmission.
  //

  digitalWrite( RS485_RXTXPIN, LOW ); 
  
  //
  // Send the J1708/J1587 message.
  //

  ucBytesWritten = Serial1.write( ucMsg, 4 );
  
  if( 4 != ucBytesWritten )
    {
      Serial.print ( "Error writing to the serial port.\r\n" ); 
    }
  else
    {
      PrintJ1587Message( (char *) "Tx", ucMsg, ucBytesWritten );
    }
  
  //
  // Turn the RS485 pin to HIGH to disable transmission (enable reception).
  //

  digitalWrite( RS485_RXTXPIN, HIGH );
  
  //
  // Delay to keep the UART from getting backed up.
  //

  delay( ucBytesWritten * BYTE_DELAY_J1587 );
  
}//Send_PID_100_OilPressure

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_PID_190_EngineSpeed - Calculate engine speed based off POT A0.
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_PID_190_EngineSpeed( void )
{
  //
  // Generic variables.
  //

  U8  ucBytesWritten = 0;
  UL  ulEngineSpeed  = 0;
  
  //
  // Set up message to send.  MID=128, PID=190
  //

  U8  ucMsg[ 15 ]    = { 128, 190, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
  
  //
  // Calculate engine speed based off POT A0.
  //
  
  ulEngineSpeed = (UL) ( ( (float) MAX_RPM * (float) fA0_EngineSpeedPercent ) / (float) 0.25 );
  
  //
  // Put it in bytes 2-3
  //

  CreateTwoByteIntegerLSB( ulEngineSpeed, &ucMsg[ 2 ] );
  
  //
  // Calculate the checksum for the message.
  //

  ucMsg[ 4 ] = CalculateJ1587Checksum( ucMsg, 4 );
  
  //
  // Turn the RS485 pin to LOW to enable transmission.
  //

  digitalWrite( RS485_RXTXPIN, LOW ); 
  
  //
  // Send the J1708/J1587 message.
  //

  ucBytesWritten = Serial1.write( ucMsg, 5 );
  
  if( 5 != ucBytesWritten )
    {
      Serial.print ( "Error writing to the serial port.\r\n" ); 
    }
  else
    {
      PrintJ1587Message( (char *) "Tx", ucMsg, ucBytesWritten );
    }
  
  //
  // Turn the RS485 pin to HIGH to disable transmission (enable reception).
  //

  digitalWrite( RS485_RXTXPIN, HIGH );
  
  //
  // Delay to keep the UART from getting backed up.
  //

  delay( ucBytesWritten * BYTE_DELAY_J1587 );
  
}//Send_PID_190_EngineSpeed

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_PID_237_VIN - VIN
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_PID_237_VIN( void )
{
  U8    ucBytesWritten   = 0;
  
  //
  // MID=128, PID=237, N=17
  //                           0,   1,  2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,  15,  16,  17,  18, 19, 20=CSUM
  U8    ucVINKW   [ 21 ] = { 128, 237, 17, '1', 'X', 'K', 'D', 'P', '4', 'E', 'X', '5', 'D', 'J', '3', '4', '6', '5', '2', '7', 0x00};
  
  //
  // Calculate the checksum for the message.
  //
  
  ucVINKW[ 20 ] = CalculateJ1587Checksum( ucVINKW, 20 );
  
  //
  // Turn the RS485 pin to LOW to enable transmission.
  //
  
  digitalWrite( RS485_RXTXPIN, LOW ); 
  
  //
  // Send the J1708/J1587 message.
  //

  ucBytesWritten = Serial1.write( ucVINKW, 21 );
  
  if( 21 != ucBytesWritten )
    {
      Serial.print ( "Error writing to the serial port.\r\n" ); 
    }
  else
    {
      PrintJ1587Message( (char *) "Tx", ucVINKW, ucBytesWritten );
    }
  
  //
  // Turn the RS485 pin to HIGH to disable transmission (enable reception).
  //

  digitalWrite( RS485_RXTXPIN, HIGH );
  
  //
  // Delay to keep the UART from getting backed up.
  //

  delay( ucBytesWritten * BYTE_DELAY_J1587 );
  
}//Send_PID_237_VIN

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_J1587_Fuel_Levels - Send J1587 fuel levels.
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_J1587_Fuel_Levels( void )
{
  U8    ucMsg          [ 8 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  U8    ucBytesWritten       = 0;
  
  //
  // MID=128, PID=38, Data = 50% or 1/2 tank
  //
  
  ucMsg[ 0 ] = 128; // MID = Engine
  ucMsg[ 1 ] =  38; // PID = 38 = Fuel Level Right Side
  ucMsg[ 2 ] = (U8) ( ( QA_IS_SET( fOvrFuelPct ) ? fOvrFuelPct : 50.0 ) * 2.0 ); // 0.5%/bit

  //
  // Calculate the checksum for the message.
  //

  ucMsg[ 3 ] =  CalculateJ1587Checksum( ucMsg, 3 );
  
  //
  // Turn the RS485 pin to LOW to enable transmission.
  //

  digitalWrite( RS485_RXTXPIN, LOW ); 
  
  //
  // Send the J1708/J1587 message.
  //

  ucBytesWritten = Serial1.write( ucMsg, 4 );
  
  if( 4 != ucBytesWritten )
    {
      Serial.print ( "Error writing to the serial port.\r\n" ); 
    }
  else
    {
      PrintJ1587Message( (char *) "Tx", ucMsg, ucBytesWritten );
    }
  
  //
  // Turn the RS485 pin to HIGH to disable transmission (enable reception).
  //

  digitalWrite( RS485_RXTXPIN, HIGH );
  
  //
  // Delay to keep the UART from getting backed up.
  //

  delay( ucBytesWritten * BYTE_DELAY_J1587 );

  //
  // MID=128, PID=96, Data = 50% or 1/2 tank
  //

  ucMsg[ 0 ] = 128; // MID = Engine
  ucMsg[ 1 ] =  96; // PID = 38 = Fuel Level Right Side
  ucMsg[ 2 ] = (U8) ( ( QA_IS_SET( fOvrFuelPct ) ? fOvrFuelPct : 50.0 ) * 2.0 ); // 0.5%/bit

  //
  // Calculate the checksum for the message.
  //

  ucMsg[ 3 ] =  CalculateJ1587Checksum( ucMsg, 3 );
  
  //
  // Turn the RS485 pin to LOW to enable transmission.
  //

  digitalWrite( RS485_RXTXPIN, LOW ); 
  
  //
  // Send the J1708/J1587 message.
  //

  ucBytesWritten = Serial1.write( ucMsg, 4 );
  
  if( 4 != ucBytesWritten )
    {
      Serial.print ( "Error writing to the serial port.\r\n" ); 
    }
  else
    {
      PrintJ1587Message( (char *) "Tx", ucMsg, ucBytesWritten );
    }
  
  //
  // Turn the RS485 pin to HIGH to disable transmission (enable reception).
  //

  digitalWrite( RS485_RXTXPIN, HIGH );
  
  //
  // Delay to keep the UART from getting backed up.
  //

  delay( ucBytesWritten * BYTE_DELAY_J1587 );

}//Send_J1587_Fuel_Levels

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_PID_244_TripDistance - Send J1587 Total Trip Miles based on TOTAL_TRIP_DISTANCE.
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_PID_244_TripDistance( void )
{
  U8    ucMsg          [ 8 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  U8    ucBytesWritten       = 0;
  
  //
  // MID=128, PID=244, n=4
  //
  
  ucMsg[ 0 ] = 128; // MID = Engine
  ucMsg[ 1 ] = 244; // PID = 244
  ucMsg[ 2 ] =   4; // n   = 4
  
  //
  // Put value in ucMsg[ 3 ].
  //

  CreateFourByteIntegerLSB( (UL) ( (UL) TOTAL_TRIP_DISTANCE / 0.1 ), &ucMsg[ 3 ] );
  
  //
  // Calculate the checksum for the message.
  //

  ucMsg[ 7 ] =  CalculateJ1587Checksum( ucMsg, 7 );
  
  //
  // Turn the RS485 pin to LOW to enable transmission.
  //

  digitalWrite( RS485_RXTXPIN, LOW ); 
  
  //
  // Send the J1708/J1587 message.
  //

  ucBytesWritten = Serial1.write( ucMsg, 8 );
  
  if( 8 != ucBytesWritten )
    {
      Serial.print ( "Error writing to the serial port.\r\n" ); 
    }
  else
    {
      PrintJ1587Message( (char *) "Tx", ucMsg, ucBytesWritten );
    }
  
  //
  // Turn the RS485 pin to HIGH to disable transmission (enable reception).
  //

  digitalWrite( RS485_RXTXPIN, HIGH );
  
  //
  // Delay to keep the UART from getting backed up.
  //

  delay( ucBytesWritten * BYTE_DELAY_J1587 );
  
}//Send_PID_244_TripDistance

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_PID_245_TripDistance - Send J1587 Total Vehicle Distance based on TOTAL_VEHICLE_DISTANCE.
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_PID_245_TotalVehicleDistance( void )
{
  U8    ucMsg          [ 8 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
  U8    ucBytesWritten       = 0;
  
  //
  // MID=128, PID=245, n=4
  //
  
  ucMsg[ 0 ] = 128; // MID=Engine
  ucMsg[ 1 ] = 245; // PID=245
  ucMsg[ 2 ] =   4; // n  = 4
  
  //
  // Put value in ucMsg[ 3 ].
  //

  CreateFourByteIntegerLSB( (UL) ( (UL) TOTAL_VEHICLE_DISTANCE / 0.1 ), &ucMsg[ 3 ] );
  
  //
  // Calculate the checksum for the message.
  //

  ucMsg[ 7 ] =  CalculateJ1587Checksum( ucMsg, 7 );
  
  //
  // Turn the RS485 pin to LOW to enable transmission.
  //

  digitalWrite( RS485_RXTXPIN, LOW ); 
  
  //
  // Send the J1708/J1587 message.
  //

  ucBytesWritten = Serial1.write( ucMsg, 8 );
  
  if( 8 != ucBytesWritten )
    {
      Serial.print ( "Error writing to the serial port.\r\n" ); 
    }
  else
    {
      PrintJ1587Message( (char *) "Tx", ucMsg, ucBytesWritten );
    }
  
  //
  // Turn the RS485 pin to HIGH to disable transmission (enable reception).
  //

  digitalWrite( RS485_RXTXPIN, HIGH );
  
  //
  // Delay to keep the UART from getting backed up.
  //

  delay( ucBytesWritten * BYTE_DELAY_J1587 );
  
}//Send_PID_245_TripDistance

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_PID_246_TotalVehicleHours - Send J1587 Vehicle hours based on TOTAL_VEHICLE_HOURS.
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_PID_246_TotalVehicleHours( void )
{
  U8    ucMsg          [ 8 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
  U8    ucBytesWritten       = 0;
  
  //
  // MID=128, PID=246, n=4
  //
  
  ucMsg[ 0 ] = 128; // MID=Engine
  ucMsg[ 1 ] = 246; // PID=246
  ucMsg[ 2 ] =   4; // n  = 4
  
  //
  // Put value in ucMsg[ 3 ].
  //

  CreateFourByteIntegerLSB( (UL) ( TOTAL_VEHICLE_HOURS / 0.05 ), &ucMsg[ 3 ] );
  
  //
  // Calculate the checksum for the message.
  //

  ucMsg[ 7 ] =  CalculateJ1587Checksum( ucMsg, 7 );
  
  //
  // Turn the RS485 pin to LOW to enable transmission.
  //

  digitalWrite( RS485_RXTXPIN, LOW ); 
  
  //
  // Send the J1708/J1587 message.
  //

  ucBytesWritten = Serial1.write( ucMsg, 8 );
  
  if( 8 != ucBytesWritten )
    {
      Serial.print ( "Error writing to the serial port.\r\n" ); 
    }
  else
    {
      PrintJ1587Message( (char *) "Tx", ucMsg, ucBytesWritten );
    }
  
  //
  // Turn the RS485 pin to HIGH to disable transmission (enable reception).
  //

  digitalWrite( RS485_RXTXPIN, HIGH );
  
  //
  // Delay to keep the UART from getting backed up.
  //

  delay( ucBytesWritten * BYTE_DELAY_J1587 );
  
}//Send_PID_246_TotalVehicleHours

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_PID_247_EngineHours - Send J1587 Engine Hours based on TOTAL_ENGINE_HOURS.
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_PID_247_EngineHours( void )
{
  U8    ucMsg          [ 8 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
  U8    ucBytesWritten       = 0;
  
  //
  // MID=128, PID=247, n=4
  //
  
  ucMsg[ 0 ] = 128; // MID=Engine
  ucMsg[ 1 ] = 247; // PID=247
  ucMsg[ 2 ] =   4; // n  = 4
  
  //
  // Put value in ucMsg[ 3 ].
  //

  CreateFourByteIntegerLSB( (UL) ( TOTAL_ENGINE_HOURS / 0.05 ), &ucMsg[ 3 ] );
  
  //
  // Calculate the checksum for the message.
  //

  ucMsg[ 7 ] =  CalculateJ1587Checksum( ucMsg, 7 );
  
  //
  // Turn the RS485 pin to LOW to enable transmission.
  //

  digitalWrite( RS485_RXTXPIN, LOW ); 
  
  //
  // Send the J1708/J1587 message.
  //

  ucBytesWritten = Serial1.write( ucMsg, 8 );
  
  if( 8 != ucBytesWritten )
    {
      Serial.print ( "Error writing to the serial port.\r\n" ); 
    }
  else
    {
      PrintJ1587Message( (char *) "Tx", ucMsg, ucBytesWritten );
    }
  
  //
  // Turn the RS485 pin to HIGH to disable transmission (enable reception).
  //

  digitalWrite( RS485_RXTXPIN, HIGH );
  
  //
  // Delay to keep the UART from getting backed up.
  //

  delay( ucBytesWritten * BYTE_DELAY_J1587 );
  
}//Send_PID_247_EngineHours

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_PID_249_EngineRevolutions - Send J1587 Engine Revolutions
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_PID_249_EngineRevolutions( void )
{
  U8    ucMsg          [ 8 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
  U8    ucBytesWritten       = 0;
  
  //
  // MID=128, PID=247, n=4
  //
  
  ucMsg[ 0 ] = 128; // MID=Engine
  ucMsg[ 1 ] = 249; // PID=249
  ucMsg[ 2 ] =   4; // n  = 4
  
  //
  // Put value in ucMsg[ 3 ].
  //

  CreateFourByteIntegerLSB( (UL) ( TOTAL_ENGINE_REVOLUTIONS / 1000.00 ), &ucMsg[ 3 ] );
  
  //
  // Calculate the checksum for the message.
  //

  ucMsg[ 7 ] =  CalculateJ1587Checksum( ucMsg, 7 );
  
  //
  // Turn the RS485 pin to LOW to enable transmission.
  //

  digitalWrite( RS485_RXTXPIN, LOW ); 
  
  //
  // Send the J1708/J1587 message.
  //
  
  ucBytesWritten = Serial1.write( ucMsg, 8 );
  
  if( 8 != ucBytesWritten )
    {
      Serial.print ( "Error writing to the serial port.\r\n" ); 
    }
  else
    {
      PrintJ1587Message( (char *) "Tx", ucMsg, ucBytesWritten );
    }
  
  //
  // Turn the RS485 pin to HIGH to disable transmission (enable reception).
  //

  digitalWrite( RS485_RXTXPIN, HIGH );
  
  //
  // Delay to keep the UART from getting backed up.
  //

  delay( ucBytesWritten * BYTE_DELAY_J1587 );
  
}//Send_PID_249_EngineRevolutions

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_J1587_SwitchStatuses - Based off of ECM Simulator switches.
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_J1587_SwitchStatuses( void )
{
  //
  // Generic variables.
  //

  U8  ucBytesWritten = 0;
  
  //
  // MID/PID
  //

  U8  ucMID          = 0;
  U8  ucPID          = 0;
  U8  ucMsgByte      = 0;
  
  //
  // Set up a message to send.
  //
  U8  ucMsg[ 15 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  
  //
  // MID=128, PID=70
  //
  
  ucMID = 128; // Engine.
  ucPID =  70; // A.70 PARKING BRAKE SWITCH STATUS.
  
  if( ON == ucParkBrakeSwitch )
    {
      ucMsgByte = 0xFF; // Bit 8=1, 7-1 undefined.
    }
  else
    {
      ucMsgByte = 0x7F; // Bit 8=0, 7-1 undefined.
    }
  
  //
  // MID, PID, 1 data byte.
  //

  ucMsg[ 0 ] = ucMID;
  ucMsg[ 1 ] = ucPID;
  ucMsg[ 2 ] = ucMsgByte;
  
  //
  // Calculate the checksum for the message.
  //

  ucMsg[ 3 ] = CalculateJ1587Checksum( ucMsg, 3 );
  
  //
  // Turn the RS485 pin to LOW to enable transmission.
  //

  digitalWrite( RS485_RXTXPIN, LOW ); 
  
  //
  // Send the J1708/J1587 message.
  //

  ucBytesWritten = Serial1.write( ucMsg, 4 );
  
  if( 4 != ucBytesWritten )
    {
      Serial.print ( "Error writing to the serial port.\r\n" ); 
    }
  else
    {
      PrintJ1587Message( (char *) "Tx", ucMsg, ucBytesWritten );
    }
  
  //
  // Turn the RS485 pin to HIGH to disable transmission (enable reception).
  //

  digitalWrite( RS485_RXTXPIN, HIGH );
  
  //
  // Delay to keep the UART from getting backed up.
  //

  delay( ucBytesWritten * BYTE_DELAY_J1587 );
  
  //----------------------------------------------------------------------------------------------------
  
  ucMID = 128; // Engine
  ucPID =  65; // A.65 BRAKE SWITCH STATUS.
  
  if( ON == ucBrakePedalSwitch )
    {
      ucMsgByte = 0xF5; // Bit 8-5=1, 43=Brake Switch Status 21=ServiceBrake 0101 On/On.
    }
  else
    {
      ucMsgByte = 0xF4; // Bit 8-5=1, 43=Brake Switch Status 21=ServiceBrake 0100 On/Off.
    }
  
  //
  // MID, PID, Data Byte.
  //

  ucMsg[ 0 ] = ucMID;
  ucMsg[ 1 ] = ucPID;
  ucMsg[ 2 ] = ucMsgByte;
  
  //
  // Calculate the checksum for the message.
  //

  ucMsg[ 3 ] = CalculateJ1587Checksum( ucMsg, 3 );
  
  //
  // Turn the RS485 pin to LOW to enable transmission.
  //

  digitalWrite( RS485_RXTXPIN, LOW ); 
  
  //
  // Send the J1708/J1587 message.
  //

  ucBytesWritten = Serial1.write( ucMsg, 4 );
  
  if( 4 != ucBytesWritten )
    {
      Serial.print ( "Error writing to the serial port.\r\n" ); 
    }
  else
    {
      PrintJ1587Message( (char *) "Tx", ucMsg, ucBytesWritten );
    }
  
  //
  // Turn the RS485 pin to HIGH to disable transmission (enable reception).
  //

  digitalWrite( RS485_RXTXPIN, HIGH );
  
  //
  // Delay to keep the UART from getting backed up.
  //

  delay( ucBytesWritten * BYTE_DELAY_J1587 );
  
  //----------------------------------------------------------------------------------------------------
  
  ucMID = 128; // Engine.
  ucPID =  85; // A.85 CRUISE CONTROL STATUS.
  
  //   Bit 8: cruise mode           1=active/0=not active
  //   Bit 7: clutch switch         1=on/0=off
  //   Bit 6: brake switch          1=on/0=off
  //   Bit 5: accel switch          1=on/0=off
  //   Bit 4: resume switch         1=on/0=off
  //   Bit 3: coast switch          1=on/0=off
  //   Bit 2: set switch            1=on/0=off
  //   Bit 1: cruise control switch 1=on/0=off
  
  if( ON == ucClutchPedalSwitch )
    {
      ucMsgByte = 0x40; // Bit 7=1.
    }
  else
    {
      ucMsgByte = 0x00; // Bit 7=0.
    }
  
  //
  // MID, PID, Data Byte.
  //

  ucMsg[ 0 ] = ucMID;
  ucMsg[ 1 ] = ucPID;
  ucMsg[ 2 ] = ucMsgByte;
  
  //
  // Calculate the checksum for the message.
  //

  ucMsg[ 3 ] = CalculateJ1587Checksum( ucMsg, 3 );
  
  //
  // Turn the RS485 pin to LOW to enable transmission.
  //

  digitalWrite( RS485_RXTXPIN, LOW ); 
  
  //
  // Send the J1708/J1587 message.
  //

  ucBytesWritten = Serial1.write( ucMsg, 4 );
  
  if( 4 != ucBytesWritten )
    {
      Serial.print ( "Error writing to the serial port.\r\n" ); 
    }
  else
    {
      PrintJ1587Message( (char *) "Tx", ucMsg, ucBytesWritten );
    }
  
  //
  // Turn the RS485 pin to HIGH to disable transmission (enable reception).
  //

  digitalWrite( RS485_RXTXPIN, HIGH );
  
  //
  // Delay to keep the UART from getting backed up.
  //

  delay( ucBytesWritten * BYTE_DELAY_J1587 );
  
}//Send_J1587_SwitchStatuses

///----------------------------------------------------------------------------------------------------
///<summary>
///   Build_J1587_DTC_Message - Build a message based on sFaultCodes array.
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Build_J1587_DTC_Message( U8 ucMID, struct J1587DTC sFaultCodes[], int iNumFaults, U8 *ucTxMsg, U8 ucSizeofTxMsg, U8 *ucOutLength )
{
  U8     ucN       = 0;
  U8     ucA       = 0;
  U8     ucB       = 0;
  U8     ucIndex   = 0;
  
  //
  // Initialize the out parameter completely.
  //

  for( int i = 0; i < ucSizeofTxMsg; i++ )
    {
      ucTxMsg[ i ] = 0;
    }
  
  //
  // Set up the static part of the Arduino message.
  //

  ucTxMsg[ 0 ]    =  ucMID     ; // MID
  ucTxMsg[ 1 ]    =  194       ; // PID
  ucTxMsg[ 2 ]    =  ucN       ; // n   - will be overwritten later, here for clarity.
  
  //
  // Set the index to where the first "a" character will be so we can build the message from there.
  //

  ucIndex        =  3;
  
  //
  // For all fault codes, add them to the transmit message.
  //
  
  for( int i = 0; i < iNumFaults; i++ )
    {
      //
      // The code is in byte A
      //
      
      ucA                  = sFaultCodes[ i ].ucCode;
      
      //
      // Start byte B out as 0.
      //
      
      ucB                  = 0x00;
      
      //
      // B bit 8 - is OC included.
      //
      
      if( sFaultCodes[ i ].bHasOC     == true )
        {
          ucB             |= 0x80; 
        }
      
      //
      // B bit 7 - if it is "inactive" then set bit 7.
      //
      
      if( sFaultCodes[ i ].bIsActive  == false )
        {
          ucB             |= 0x40; 
        }
      
      //
      // B bit 6 - if it is "STD" fault (not PID Page 2) then set bit 6
      //
      
      if( sFaultCodes[ i ].bIsSTD     == true )
        {
          ucB             |= 0x20; 
        }
      
      //
      // B bit 5 - if it is "SID" then set bit 5
      //
      
      if( sFaultCodes[ i ].bIsSID     == true )
        {
          ucB             |= 0x10; 
        }
      
      //
      // The FMI is in bits 4..1
      //
      
      ucB                 |= (byte) ( sFaultCodes[ i ].ucFMI & 0x0F );
      
      //
      // Add A and B now that they are known.
      //
      
      ucTxMsg[ucIndex]     =  ucA                 ; ucIndex++;  // a
      ucTxMsg[ucIndex]     =  ucB                 ; ucIndex++;  // b
      
      //
      // The OC if included
      //
      
      if( sFaultCodes[ i ].bHasOC  == true )
        {
          ucTxMsg[ucIndex] =  sFaultCodes[ i ].ucOC ; ucIndex++;  // c
        }
      
    }//for
  
  //
  // Set the number of bytes.
  //
  
  ucN          = (byte) ( ucIndex - 3 );
  ucTxMsg[ 2 ] = ucN;
  
  *ucOutLength = ucIndex;  
  
}//Build_J1587_DTC_Message                                  

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_J1587_Engine_DTCs
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_J1587_Engine_DTCs( void )
{
  //
  // A table of J1587 fault codes, and a message to put them in.
  //
  
  struct J1587DTC sFaultCodes  [  10 ];
  U8              ucMsg        [ 100 ];

  //
  // Initialize complex variables.
  //

  memset( &sFaultCodes[ 0 ], 0x00, sizeof( sFaultCodes ) );
  memset( &ucMsg[ 0 ]      , 0x00, sizeof( ucMsg       ) );
  
  //
  // Simple parameters
  //

  byte            ucOutLength      = 0;
  byte            ucMID            = 128;
  byte            ucBytesWritten   = 0;
  
  //
  // A table of J1587 engine type fault codes.  
  //   * A PID Page 1/FMI/OC of 100/6/10
  //   * A PID Page 2/FMI/OC of 341/12/No OC
  //   * A SID Page 1/FMI/OC of 2/10/10
  //   * A SID Page 2/FMI/OC of 277/6/No OC
  //

  sFaultCodes[ 0 ].bIsActive  = true ;  // ACTIVE
  sFaultCodes[ 0 ].ucCode     = 100  ;  // PID 100 fault = Engine Oil Pressure
  sFaultCodes[ 0 ].ucFMI      = 6    ;  // FMI   6       = Current above normal or grounded circuit
  sFaultCodes[ 0 ].bIsSID     = false;  // Not a SID, but a PID 100 fault
  sFaultCodes[ 0 ].bIsSTD     = true ;  // STD = true, PID Page 1
  sFaultCodes[ 0 ].bHasOC     = true ;  // Has OC
  sFaultCodes[ 0 ].ucOC       = 10   ;  // OC = 10 times
  
  sFaultCodes[ 1 ].bIsActive  = true ;  // ACTIVE
  sFaultCodes[ 1 ].ucCode     = 85   ;  // PID Page 2 - (85 + 256) = 341 ENGINE SHUTDOWN OVERRIDE SWITCH
  sFaultCodes[ 1 ].ucFMI      = 12   ;  // FMI 12 Bad intelligent device or component
  sFaultCodes[ 1 ].bIsSID     = false;  // PID fault,
  sFaultCodes[ 1 ].bIsSTD     = false;  // STD = false, PID Page 2
  sFaultCodes[ 1 ].bHasOC     = false;  // No OC
  sFaultCodes[ 1 ].ucOC       = 0    ;  // No OC
  
  sFaultCodes[ 2 ].bIsActive  = false;  // INACTIVE
  sFaultCodes[ 2 ].ucCode     = 2    ;  // SID  2 Injector Cylinder #2
  sFaultCodes[ 2 ].ucFMI      = 10   ;  // FMI 10 Abnormal rate of change
  sFaultCodes[ 2 ].bIsSID     = true ;  // SID fault
  sFaultCodes[ 2 ].bIsSTD     = true ;  // STD = true
  sFaultCodes[ 2 ].bHasOC     = true ;  // Has OC
  sFaultCodes[ 2 ].ucOC       = 11   ;  // OC = 10 times
  
  sFaultCodes[ 3 ].bIsActive  = false;  // INACTIVE
  sFaultCodes[ 3 ].ucCode     = 21   ;  // SID Page 2 - (21 + 256) = 277 Exhaust Gas Recirculation (EGR) Mass Flow Sensor
  sFaultCodes[ 3 ].ucFMI      = 7    ;  // FMI 7 Mechanical system not responding properly
  sFaultCodes[ 3 ].bIsSID     = true ;  // SID fault,
  sFaultCodes[ 3 ].bIsSTD     = false;  // STD = false, SID Page 2
  sFaultCodes[ 3 ].bHasOC     = true ;  // No OC
  sFaultCodes[ 3 ].ucOC       = 15   ;  // No OC
  
  //
  // Build the transmit string, and then add the checksum.
  //

  Build_J1587_DTC_Message( ucMID, sFaultCodes, 4, &ucMsg[ 0 ], sizeof( ucMsg ), &ucOutLength );
  
  //
  // Calculate the checksum for the message.
  //

  ucMsg[ ucOutLength ] = CalculateJ1587Checksum( ucMsg, ucOutLength );
  
  //
  // Turn the RS485 pin to LOW to enable transmission.
  //

  digitalWrite( RS485_RXTXPIN, LOW ); 
  
  //
  // Send the J1708/J1587 message.
  //

  ucBytesWritten = Serial1.write( ucMsg, ( ucOutLength + 1 ) );
  
  if( ( ucOutLength + 1 ) != ucBytesWritten )
    {
      Serial.print ( "Error writing to the serial port.\r\n" ); 
    }
  else
    {
      PrintJ1587Message( (char *) "Tx", ucMsg, ucBytesWritten );
    }
  
  //
  // Turn the RS485 pin to HIGH to disable transmission (enable reception).
  //

  digitalWrite( RS485_RXTXPIN, HIGH );
  
  //
  // Delay to keep the UART from getting backed up.
  //

  delay( ucBytesWritten * BYTE_DELAY_J1587 );
  
}//Send_J1587_Engine_DTCs

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_J1587_Transmission_DTCs
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_J1587_Transmission_DTCs( void )
{
  //
  // A table of J1587 fault codes, and a message to put them in.
  //

  struct J1587DTC sFaultCodes[  10 ];
  U8              ucMsg      [ 100 ];
  
  //
  // Initialize complex variables.
  //

  memset( &sFaultCodes[ 0 ], 0x00, sizeof( sFaultCodes ) );
  memset( &ucMsg[ 0 ]      , 0x00, sizeof( ucMsg       ) );

  //
  // Simple parameters
  //

  byte            ucOutLength      = 0;
  byte            ucMID            = 130;
  byte            ucBytesWritten   = 0;
  
  //
  // A table of J1587 transmission type fault codes.  
  //   * A PID Page 1/FMI/OC of 127/6/10
  //   * A PID Page 2/FMI/OC of 418(162)/12/No OC
  //   * A SID Page 1/FMI/OC of 6/10/10
  //   * A SID Page 2/FMI/OC of 279(23)/7/No OC
  //

  sFaultCodes[ 0 ].bIsActive  = true ;  // ACTIVE
  sFaultCodes[ 0 ].ucCode     = 8    ;  // PID Page 1 - A.8 BRAKE SYSTEM AIR PRESSURE LOW WARNING SWITCH STATUS
  sFaultCodes[ 0 ].ucFMI      = 6    ;  // FMI   6       = Current above normal or grounded circuit
  sFaultCodes[ 0 ].bIsSID     = false;  // Not a SID, but a PID fault
  sFaultCodes[ 0 ].bIsSTD     = true ;  // STD = true, PID Page 1
  sFaultCodes[ 0 ].bHasOC     = true ;  // Has OC
  sFaultCodes[ 0 ].ucOC       = 10   ;  // OC = 10 times
  
  sFaultCodes[ 1 ].bIsActive  = true ;  // ACTIVE
  sFaultCodes[ 1 ].ucCode     = 89   ;  // PID Page 2 - A.345 (89) SUSPENSION CONTROL STATUS #1
  sFaultCodes[ 1 ].ucFMI      = 12   ;  // FMI 12 Bad intelligent device or component
  sFaultCodes[ 1 ].bIsSID     = false;  // PID fault,
  sFaultCodes[ 1 ].bIsSTD     = false;  // STD = false, PID Page 2
  sFaultCodes[ 1 ].bHasOC     = false;  // No OC
  sFaultCodes[ 1 ].ucOC       = 0    ;  // No OC
  
  sFaultCodes[ 2 ].bIsActive  = false;  // INACTIVE
  sFaultCodes[ 2 ].ucCode     = 12   ;  // SID Page 1 - 12 Pressure Modulation Valve ABS Axle 3 Right
  sFaultCodes[ 2 ].ucFMI      = 10   ;  // FMI 10 Abnormal rate of change
  sFaultCodes[ 2 ].bIsSID     = true ;  // SID fault
  sFaultCodes[ 2 ].bIsSTD     = true ;  // STD = true
  sFaultCodes[ 2 ].bHasOC     = true ;  // Has OC
  sFaultCodes[ 2 ].ucOC       = 11   ;  // OC = 10 times
  
  sFaultCodes[ 3 ].bIsActive  = false;  // INACTIVE
  sFaultCodes[ 3 ].ucCode     = 24   ;  // SID Page 2 - (24 + 256) = 280 Reserved
  sFaultCodes[ 3 ].ucFMI      = 7    ;  // FMI 7 Mechanical system not responding properly
  sFaultCodes[ 3 ].bIsSID     = true ;  // SID fault,
  sFaultCodes[ 3 ].bIsSTD     = false;  // STD = false, SID Page 2
  sFaultCodes[ 3 ].bHasOC     = true ;  // No OC
  sFaultCodes[ 3 ].ucOC       = 15   ;  // No OC
  
  //
  // Build the transmit string, and then add the checksum.
  //

  Build_J1587_DTC_Message( ucMID, sFaultCodes, 4, &ucMsg[ 0 ], sizeof( ucMsg ), &ucOutLength );
  
  //
  // Calculate the checksum for the message.
  //

  ucMsg[ ucOutLength ] = CalculateJ1587Checksum( ucMsg, ucOutLength );
  
  //
  // Turn the RS485 pin to LOW to enable transmission.
  //

  digitalWrite( RS485_RXTXPIN, LOW ); 
  
  //
  // Send the J1708/J1587 message.
  //

  ucBytesWritten = Serial1.write( ucMsg, ( ucOutLength + 1 ) );
  
  if( ( ucOutLength + 1 ) != ucBytesWritten )
    {
      Serial.print ( "Error writing to the serial port.\r\n" ); 
    }
  else
    {
      PrintJ1587Message( (char *) "Tx", ucMsg, ucBytesWritten );
    }
  
  //
  // Turn the RS485 pin to HIGH to disable transmission (enable reception).
  //

  digitalWrite( RS485_RXTXPIN, HIGH );   
  
  //
  // Delay to keep the UART from getting backed up.
  //

  delay( ucBytesWritten * BYTE_DELAY_J1587 );
  
}//Send_J1587_Transmission_DTCs

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_J1587_ABS_DTCs
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_J1587_ABS_DTCs( void )
{
  //
  // Simple parameters
  //

  byte            ucOutLength      = 0;
  byte            ucMID            = 136;
  byte            ucBytesWritten   = 0;
  
  //
  // A table of J1587 fault codes, and a message to put them in.
  //

  struct J1587DTC sFaultCodes[  10 ];
  U8              ucMsg      [ 100 ];
  
  //
  // Initialize complex variables.
  //

  memset( &sFaultCodes[ 0 ], 0x00, sizeof( sFaultCodes ) );
  memset( &ucMsg[ 0 ]      , 0x00, sizeof( ucMsg       ) );

  //
  // A table of J1587 engine type fault codes.  
  //   * A PID Page 1/FMI/OC of 100/6/10
  //   * A PID Page 2/FMI/OC of 341/12/No OC
  //   * A SID Page 1/FMI/OC of 2/10/10
  //   * A SID Page 2/FMI/OC of 277/6/No OC
  //

  sFaultCodes[ 0 ].bIsActive  = true ;  // ACTIVE
  sFaultCodes[ 0 ].ucCode     = 100  ;  // PID 100 fault = Engine Oil Pressure
  sFaultCodes[ 0 ].ucFMI      = 6    ;  // FMI   6       = Current above normal or grounded circuit
  sFaultCodes[ 0 ].bIsSID     = false;  // Not a SID, but a PID 100 fault
  sFaultCodes[ 0 ].bIsSTD     = true ;  // STD = true, PID Page 1
  sFaultCodes[ 0 ].bHasOC     = true ;  // Has OC
  sFaultCodes[ 0 ].ucOC       = 10   ;  // OC = 10 times
  
  sFaultCodes[ 1 ].bIsActive  = true ;  // ACTIVE
  sFaultCodes[ 1 ].ucCode     = 85   ;  // PID Page 2 - (85 + 256) = 341 ENGINE SHUTDOWN OVERRIDE SWITCH
  sFaultCodes[ 1 ].ucFMI      = 12   ;  // FMI 12 Bad intelligent device or component
  sFaultCodes[ 1 ].bIsSID     = false;  // PID fault,
  sFaultCodes[ 1 ].bIsSTD     = false;  // STD = false, PID Page 2
  sFaultCodes[ 1 ].bHasOC     = false;  // No OC
  sFaultCodes[ 1 ].ucOC       = 0    ;  // No OC
  
  sFaultCodes[ 2 ].bIsActive  = false;  // INACTIVE
  sFaultCodes[ 2 ].ucCode     = 2    ;  // SID  2 Injector Cylinder #2
  sFaultCodes[ 2 ].ucFMI      = 10   ;  // FMI 10 Abnormal rate of change
  sFaultCodes[ 2 ].bIsSID     = true ;  // SID fault
  sFaultCodes[ 2 ].bIsSTD     = true ;  // STD = true
  sFaultCodes[ 2 ].bHasOC     = true ;  // Has OC
  sFaultCodes[ 2 ].ucOC       = 11   ;  // OC = 10 times
  
  sFaultCodes[ 3 ].bIsActive  = false;  // INACTIVE
  sFaultCodes[ 3 ].ucCode     = 21   ;  // SID Page 2 - (21 + 256) = 277 Exhaust Gas Recirculation (EGR) Mass Flow Sensor
  sFaultCodes[ 3 ].ucFMI      = 7    ;  // FMI 7 Mechanical system not responding properly
  sFaultCodes[ 3 ].bIsSID     = true ;  // SID fault,
  sFaultCodes[ 3 ].bIsSTD     = false;  // STD = false, SID Page 2
  sFaultCodes[ 3 ].bHasOC     = true ;  // No OC
  sFaultCodes[ 3 ].ucOC       = 15   ;  // No OC
  
  //
  // Build the transmit string, and then add the checksum.
  //

  Build_J1587_DTC_Message( ucMID, sFaultCodes, 4, &ucMsg[ 0 ], sizeof( ucMsg ), &ucOutLength );
  
  //
  // Calculate the checksum for the message.
  //

  ucMsg[ ucOutLength ] = CalculateJ1587Checksum( ucMsg, ucOutLength );
  
  //
  // Turn the RS485 pin to LOW to enable transmission.
  //

  digitalWrite( RS485_RXTXPIN, LOW ); 
  
  //
  // Send the J1708/J1587 message.
  //

  ucBytesWritten = Serial1.write( ucMsg, ( ucOutLength + 1 ) );
  
  if( ( ucOutLength + 1 ) != ucBytesWritten )
    {
      Serial.print ( "Error writing to the serial port.\r\n" ); 
    }
  else
    {
      PrintJ1587Message( (char *) "Tx", ucMsg, ucBytesWritten );
    }
  
  //
  // Turn the RS485 pin to HIGH to disable transmission (enable reception).
  //

  digitalWrite( RS485_RXTXPIN, HIGH );
  
  //
  // Delay to keep the UART from getting backed up.
  //

  delay( ucBytesWritten * BYTE_DELAY_J1587 );
  
}//Send_J1587_ABS_DTCs


///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_J1587_ComponentInfo
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_J1587_ComponentInfo( void )
{
  U8    ucBytesWritten = 0;
  
  //
  // A.243 COMPONENT IDENTIFICATION PARAMETER (Make, Model, Serial #)
  //
  //
  //-------------------------------------------------------------------------------------------------------------------------------------------------------------------------
  //                            0,   1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33=CSUM
  U8    ucMMSNEng    [34] = { 128, 243, 30, 128, 'C','M','M','N','S','*','I','S','X','9','4','5','*','9','8','7','6','7','8','*','D','o','o','r','#','5','4','3','*',0x00};
  U8    ucMMSNTrans  [34] = { 130, 243, 30, 130, 'E','A','T','O','N','*','A','S','X','2','1','0','*','8','9','7','6','7','8','*','E','T','X','A','F','5','4','3','*',0x00};
  U8    ucMMSNABS    [34] = { 136, 243, 30, 136, 'W','A','B','C','O','*','4','S','4','M','V','1','*','1','3','5','7','9','B','*','X','O','2','F','F','4','4','3','*',0x00};
  
  //
  // Calculate the checksums.
  //
  
  ucMMSNEng    [33] = CalculateJ1587Checksum( ucMMSNEng  , 33 );
  ucMMSNTrans  [33] = CalculateJ1587Checksum( ucMMSNTrans, 33 );
  ucMMSNABS    [33] = CalculateJ1587Checksum( ucMMSNABS  , 33 );
  
  //****************************************************************************************************
  // ENGINE
  //****************************************************************************************************
  
  //
  // Turn the RS485 pin to LOW to enable transmission.
  //

  digitalWrite( RS485_RXTXPIN, LOW ); 
  
  //
  // Send the J1708/J1587 message.
  //

  ucBytesWritten = Serial1.write( ucMMSNEng  , 34 );
  
  if( 34 != ucBytesWritten )
    {
      Serial.print ( "Error writing to the serial port.\r\n" ); 
    }
  else
    {
      PrintJ1587Message( (char *) "Tx", ucMMSNEng, ucBytesWritten );
    }
  
  //
  // Turn the RS485 pin to HIGH to disable transmission (enable reception).
  //

  digitalWrite( RS485_RXTXPIN, HIGH );
  
  //
  // Delay to keep the UART from getting backed up.
  //

  delay( ucBytesWritten * BYTE_DELAY_J1587 );
  
  //****************************************************************************************************
  // TRANSMISSION
  //****************************************************************************************************
  
  //
  // Turn the RS485 pin to LOW to enable transmission.
  //

  digitalWrite( RS485_RXTXPIN, LOW ); 
  
  //
  // Send the J1708/J1587 message.
  //

  ucBytesWritten =   Serial1.write( ucMMSNTrans, 34 );
  
  if( 34 != ucBytesWritten )
    {
      Serial.print ( "Error writing to the serial port.\r\n" ); 
    }
  else
    {
      PrintJ1587Message( (char *) "Tx", ucMMSNTrans, ucBytesWritten );
    }
  
  //
  // Turn the RS485 pin to HIGH to disable transmission (enable reception).
  //

  digitalWrite( RS485_RXTXPIN, HIGH );
  
  //
  // Delay to keep the UART from getting backed up.
  //

  delay( ucBytesWritten * BYTE_DELAY_J1587 );
  
  //****************************************************************************************************
  // ABS
  //****************************************************************************************************
  
  //
  // Turn the RS485 pin to LOW to enable transmission.
  //

  digitalWrite( RS485_RXTXPIN, LOW ); 
  
  //
  // Send the J1708/J1587 message.
  //

  ucBytesWritten = Serial1.write( ucMMSNABS  , 34 );
  
  if( 34 != ucBytesWritten )
    {
      Serial.print ( "Error writing to the serial port.\r\n" ); 
    }
  else
    {
      PrintJ1587Message( (char *) "Tx", ucMMSNABS, ucBytesWritten );
    }
  
  //
  // Turn the RS485 pin to HIGH to disable transmission (enable reception).
  //

  digitalWrite( RS485_RXTXPIN, HIGH );
  
  //
  // Delay to keep the UART from getting backed up.
  //

  delay( ucBytesWritten * BYTE_DELAY_J1587 );
  
  //****************************************************************************************************
  //****************************************************************************************************
  //****************************************************************************************************
  
  //
  // A.234 SOFTWARE IDENTIFICATION
  //                            0,   1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18=CSUM
  U8    ucSoftEng    [18] = { 128, 234, 14, '1','2','3','4','5','6','7','8','9','A','B','C','D','E', 0x00};
  U8    ucSoftTrans  [18] = { 130, 234, 14, '1','2','3','4','5','6','7','8','9','A','B','C','D','E', 0x00};
  U8    ucSoftABS    [18] = { 136, 234, 14, '1','2','3','4','5','6','7','8','9','A','B','C','D','E', 0x00};
  
  //
  // Calculate the checksums.
  //

  ucSoftEng    [17] = CalculateJ1587Checksum( ucSoftEng  , 17 );
  ucSoftTrans  [17] = CalculateJ1587Checksum( ucSoftTrans, 17 );
  ucSoftABS    [17] = CalculateJ1587Checksum( ucSoftABS  , 17 );
  
  //****************************************************************************************************
  // ENGINE
  //****************************************************************************************************
  
  //
  // Turn the RS485 pin to LOW to enable transmission.
  //

  digitalWrite( RS485_RXTXPIN, LOW ); 
  
  ucBytesWritten = Serial1.write( ucSoftEng  , 18 );
  
  if( 18 != ucBytesWritten )
    {
      Serial.print ( "Error writing to the serial port.\r\n" ); 
    }
  else
    {
      PrintJ1587Message( (char *) "Tx", ucSoftEng, ucBytesWritten );
    }
  
  //
  // Turn the RS485 pin to HIGH to disable transmission (enable reception).
  //

  digitalWrite( RS485_RXTXPIN, HIGH );
  
  //
  // Delay to keep the UART from getting backed up.
  //

  delay( ucBytesWritten * BYTE_DELAY_J1587 );
  
  //****************************************************************************************************
  // TRANSMISSION
  //****************************************************************************************************
  
  //
  // Turn the RS485 pin to LOW to enable transmission.
  //

  digitalWrite( RS485_RXTXPIN, LOW ); 
  
  ucBytesWritten = Serial1.write( ucSoftTrans, 18 ); 
  
  if( 18 != ucBytesWritten )
    {
      Serial.print ( "Error writing to the serial port.\r\n" ); 
    }
  else
    {
      PrintJ1587Message( (char *) "Tx", ucSoftTrans, ucBytesWritten );
    }
  
  //
  // Turn the RS485 pin to HIGH to disable transmission (enable reception).
  //

  digitalWrite( RS485_RXTXPIN, HIGH );
  
  //
  // Delay to keep the UART from getting backed up.
  //

  delay( ucBytesWritten * BYTE_DELAY_J1587 );
  
  //****************************************************************************************************
  // ABS
  //****************************************************************************************************
  
  //
  // Turn the RS485 pin to LOW to enable transmission.
  //

  digitalWrite( RS485_RXTXPIN, LOW ); 
  
  ucBytesWritten = Serial1.write( ucSoftABS  , 18 ); 
  
  if( 18 != ucBytesWritten )
    {
      Serial.print ( "Error writing to the serial port.\r\n" ); 
    }
  else
    {
      PrintJ1587Message( (char *) "Tx", ucSoftABS, ucBytesWritten );
    }
  
  //
  // Turn the RS485 pin to HIGH to disable transmission (enable reception).
  //

  digitalWrite( RS485_RXTXPIN, HIGH );
  
  //
  // Delay to keep the UART from getting backed up.
  //

  delay( ucBytesWritten * BYTE_DELAY_J1587 );
  
  //****************************************************************************************************
  //****************************************************************************************************
  //****************************************************************************************************
  
  //
  // A.233 UNIT NUMBER
  //                            0,   1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12=CSUM
  U8    ucUnitEng    [ 12 ] = { 128, 233,  9, 'D','o','o','r','#','5','4','3',0x00};
  U8    ucUnitTrans  [ 12 ] = { 130, 233,  9, 'E','T','X','A','F','5','4','3',0x00};
  U8    ucUnitABS    [ 12 ] = { 136, 233,  9, 'X','O','2','F','F','4','4','3',0x00};
  
  ucUnitEng    [ 11 ] = CalculateJ1587Checksum( ucUnitEng  , 11 );
  ucUnitTrans  [ 11 ] = CalculateJ1587Checksum( ucUnitTrans, 11 );
  ucUnitABS    [ 11 ] = CalculateJ1587Checksum( ucUnitABS  , 11 );
  
  //****************************************************************************************************
  // ENGINE
  //****************************************************************************************************
  
  //
  // Turn the RS485 pin to LOW to enable transmission.
  //

  digitalWrite( RS485_RXTXPIN, LOW ); 
  
  ucBytesWritten = Serial1.write( ucUnitEng  , 12 );
  
  if( 12 != ucBytesWritten )
    {
      Serial.print ( "Error writing to the serial port.\r\n" ); 
    }
  else
    {
      PrintJ1587Message( (char *) "Tx", ucUnitEng, ucBytesWritten );
    }
  
  //
  // Turn the RS485 pin to HIGH to disable transmission (enable reception).
  //

  digitalWrite( RS485_RXTXPIN, HIGH );
  
  //
  // Delay to keep the UART from getting backed up.
  //

  delay( ucBytesWritten * BYTE_DELAY_J1587 );
  
  //****************************************************************************************************
  // TRANSMISSION
  //****************************************************************************************************
  
  //
  // Turn the RS485 pin to LOW to enable transmission.
  //

  digitalWrite( RS485_RXTXPIN, LOW ); 
  
  ucBytesWritten = Serial1.write( ucUnitTrans, 12 );
  
  if( 12 != ucBytesWritten )
    {
      Serial.print ( "Error writing to the serial port.\r\n" ); 
    }
  else
    {
      PrintJ1587Message( (char *) "Tx", ucUnitTrans, ucBytesWritten );
    }
  
  //
  // Turn the RS485 pin to HIGH to disable transmission (enable reception).
  //

  digitalWrite( RS485_RXTXPIN, HIGH );
  
  //
  // Delay to keep the UART from getting backed up.
  //

  delay( ucBytesWritten * BYTE_DELAY_J1587 );
  
  //****************************************************************************************************
  // ABS
  //****************************************************************************************************
  
  //
  // Turn the RS485 pin to LOW to enable transmission.
  //

  digitalWrite( RS485_RXTXPIN, LOW ); 
  
  ucBytesWritten = Serial1.write( ucUnitABS  , 12 );
  
  if( 12 != ucBytesWritten )
    {
      Serial.print ( "Error writing to the serial port.\r\n" ); 
    }
  else
    {
      PrintJ1587Message( (char *) "Tx", ucUnitABS, ucBytesWritten );
    }
  
  //
  // Turn the RS485 pin to HIGH to disable transmission (enable reception).
  //

  digitalWrite( RS485_RXTXPIN, HIGH );
  
  //
  // Delay to keep the UART from getting backed up.
  //

  delay( ucBytesWritten * BYTE_DELAY_J1587 );
  
}// Send_J1587_ComponentInfo


//****************************************************************************************************
//****************************************************************************************************
//****************************************************************************************************
//****************************************************************************************************
//****************************************************************************************************
// J1939 Messages
//****************************************************************************************************
//****************************************************************************************************
//****************************************************************************************************
//****************************************************************************************************
//****************************************************************************************************

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_J1939_Fuel_Levels - Send J1939 fuel levels.
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_J1939_Fuel_Levels( void )
{
  U8 ucPRI              = 6;
  UL ulPGN              = 65276;
  UL ulCANID            = 0;
  U8 ucSRC              = 0;
  U8 ucDST              = 0xFF;
  U8 ucLen              = 8;
  U8 ucData         [ 8 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

  //
  // Calculate the CANID.
  //

  ulCANID = PGNToCANID( ulPGN, ucPRI, ucSRC, ucDST );

  //
  // We have about 50% of fuel left.
  //

  ucData[ 1 ] = (unsigned char) ( ( QA_IS_SET( fOvrFuelPct ) ? fOvrFuelPct : 50.0 ) / 0.4 ); // Fuel Level, Left Side
  ucData[ 6 ] = (unsigned char) ( ( QA_IS_SET( fOvrFuelPct ) ? fOvrFuelPct : 50.0 ) / 0.4 ); // Fuel Level, Right Side
  
  //
  // Send the message.
  //

  CAN0.sendMsgBuf( ulCANID, 1, 8, ucData ); delay( J1939_INTERMESSAGE_DELAY_TIME );
  
  //
  // Print message to Arduino serial port.
  //

  PrintJ1939Message( (char *) "Tx", ulCANID, ulPGN, ucSRC, ucDST, ucData, ucLen  );
  
}//Send_J1939_Fuel_Levels

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_PGN_61444_EngineSpeed
///
///       Bit    Length   SPN Description                                           SPN
///       1.1    4 bits   Engine Torque Mode                                        899
///       2      1 byte   Driver's Demand Engine - Percent Torque                   512
///       3      1 byte   Actual Engine - Percent Torque                            513
///       4-5    2 bytes  Engine Speed                                              190
///       6      1 byte   Source Address of Controlling Device for Engine Control  1483
///       7.1    4 bits   Engine Starter Mode                                      1675
///       8      1 byte   Engine Demand - Percent Torque                           2432
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_PGN_61444_EngineSpeed( void )
{
  U8 ucPRI              = 6;
  UL ulPGN              = 61444;
  UL ulCANID            = 0;
  U8 ucSRC              = 0;
  U8 ucDST              = 0xFF;
  U8 ucLen              = 8;
  UL ulEngineSpeed      = 0;
  U8 ucData         [ 8 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  
  //
  // Calculate the CANID.
  //

  ulCANID = PGNToCANID( ulPGN, ucPRI, ucSRC, ucDST );
  
  //
  // SPN 190.
  //

  ulEngineSpeed = (UL) ( ( (float) MAX_RPM * (float) fA0_EngineSpeedPercent ) / (float) 0.125 );
  
  //
  // Put it in bytes 3-4.
  //

  CreateTwoByteIntegerLSB( ulEngineSpeed, &ucData[ 3 ] );
  
  //
  // SPN 1483 Source Address of Controlling Device for Engine, SRC=0.
  //

  ucData[ 5 ] = 0x00;
  
  //
  // Send the message.
  //

  CAN0.sendMsgBuf( ulCANID, 1, 8, ucData ); delay( J1939_INTERMESSAGE_DELAY_TIME );
  
  //
  // Print message to Arduino serial port.
  //

  PrintJ1939Message( (char *) "Tx", ulCANID, ulPGN, ucSRC, ucDST, ucData, ucLen  );
  
}//Send_PGN_61444_EngineSpeed

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_PGN_65265_VehicleSpeed
///
///       Bit   Length SPN Description                           SPN
///       1.1   2 bits Two Speed Axle Switch                      69
///       1.3   2 bits Parking Brake Switch                       70
///       1.5   2 bits Cruise Control Pause Switch              1633
///       2-3   2 bytes Wheel-Based Vehicle Speed                 84
///       4.1   2 bits Cruise Control Active                     595
///       4.3   2 bits Cruise Control Enable Switch              596
///       4.5   2 bits Brake Switch                              597
///       4.7   2 bits Clutch Switch                             598
///       5.1   2 bits Cruise Control Set Switch                 599
///       5.3   2 bits Cruise Control Coast (Decelerate) Switch  600
///       5.5   2 bits Cruise Control Resume Switch              601
///       5.7   2 bits Cruise Control Accelerate Switch          602
///       6     1 byte Cruise Control Set Speed                   86
///       7.1   5 bits PTO State                                 976
///       7.6   3 bits Cruise Control States                     527
///       8.1   2 bits Idle Increment Switch                     968
///       8.3   2 bits Idle Decrement Switch                     967
///       8.5   2 bits Engine Test Mode Switch                   966
///       8.7   2 bits Engine Shutdown Override Switch          1237
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_PGN_65265_VehicleSpeed( void )
{
  U8    ucPRI                   = 6;
  UL    ulPGN                   = 65265;
  UL    ulCANID                 = 0;
  U8    ucSRC                   = 0;
  U8    ucDST                   = 0xFF;
  U8    ucLen                   = 8;
  U8    ucData              [ 8 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  float fSpeed                  = 0;
  
  //
  // Calculate the CANID.
  //

  ulCANID = PGNToCANID( ulPGN, ucPRI, ucSRC, ucDST );
  
  //
  // Byte 0, bits 2-3.
  //

  if( ucParkBrakeSwitch )
    {
      //       1111 [01]11
      ucData[ 0 ] = 0xF7;
    }
  else
    {
      //       1111 [00]11
      ucData[ 0 ] = 0xF3;
    }
  
  //
  // SPN 84 Wheel Based Vehicle Speed 0.00390625 kmh / 0.
  //

  fSpeed = (float) ( MAX_VEHICLE_SPEED_KPH * fA2_VehicleSpeedPercent ) / 0.00390625;
  
  CreateTwoByteIntegerLSB( (UL) fSpeed, &ucData[ 1 ] );
  
  //
  // Byte 3, bits 4-5 Brake.
  // Byte 3, bits 6-7 Clutch.
  //

  if(      ( ON == ucClutchPedalSwitch ) && ( ON == ucBrakePedalSwitch ) )
    {
      //        CL  BK
      //       [01][01] 1111
      ucData[ 3 ] = 0x5F;
    }
  else if( ( ON == ucClutchPedalSwitch ) && ( OFF  == ucBrakePedalSwitch ) )
    {
      //        CL  BK
      //       [01][00] 1111
      ucData[ 3 ] = 0x4F;
    }
  else if( ( OFF  == ucClutchPedalSwitch ) && ( ON == ucBrakePedalSwitch ) )
    {
      //        CL  BK
      //       [00][01] 1111
      ucData[ 3 ] = 0x1F;
    }
  else if( ( OFF  == ucClutchPedalSwitch ) && ( OFF  == ucBrakePedalSwitch ) )
    {
      //        CL  BK
      //       [00][00] 1111
      ucData[ 3 ] = 0x0F;
    }
  
  //
  // Send the message.
  //

  CAN0.sendMsgBuf( ulCANID, 1, 8, ucData ); delay( J1939_INTERMESSAGE_DELAY_TIME );
  
  //
  // Print message to Arduino serial port.
  //

  PrintJ1939Message( (char *) "Tx", ulCANID, ulPGN, ucSRC, ucDST, ucData, ucLen  );
  
}//Send_PGN_65265_VehicleSpeed

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_PGN_65263_OilPressure
///
///       Bit   Length SPN Description                         SPN
///       1     1 byte Fuel Delivery Pressure                   94
///       2     1 byte Extended Crankcase Blow-by Pressure      22
///       3     1 byte Engine Oil Level                         98
///       4     1 byte Engine Oil Pressure                     100
///       5-6   2 bytes Crankcase Pressure                     101
///       7     1 byte Coolant Pressure                        109
///       8     1 byte Coolant Level                           111

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_PGN_65263_FuelDeliveryPressure
///
///       Bit   Length SPN Description                         SPN
///       1     1 byte Fuel Delivery Pressure                   94   <-- NOW CONTROLLED BY A4 POT
///       2     1 byte Extended Crankcase Blow-by Pressure      22
///       3     1 byte Engine Oil Level                         98
///       4     1 byte Engine Oil Pressure                     100
///       5-6   2 bytes Crankcase Pressure                     101
///       7     1 byte Coolant Pressure                        109
///       8     1 byte Coolant Level                           111
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_PGN_65263_FuelDeliveryPressure( void )
{
  U8    ucPRI              = 6;
  UL    ulPGN              = 65263;
  UL    ulCANID            = 0;
  U8    ucSRC              = 0;
  U8    ucDST              = 0xFF;
  U8    ucLen              = 8;
  U8    ucData         [ 8 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  float fFuelPressKPA      = 0;
  
  //
  // Calculate the CANID.
  //
  
  ulCANID = PGNToCANID( ulPGN, ucPRI, ucSRC, ucDST );
  
  //
  // Calculate fuel delivery pressure using A4 potentiometer
  // SPN 94: Fuel Delivery Pressure - Range 0-1000 kPa, Resolution 4 kPa/bit, Offset 0
  //
  
  fFuelPressKPA = (float) ( MAX_PRESS_KPA * fA4_OilPressurePercent );
  
  //
  // SPN 94 Fuel Delivery Pressure - Byte 1 (Index 0), 4 kPa per bit, 0 offset
  //
  
  ucData[ 0 ] = (U8) ( ( fFuelPressKPA + 0 ) / 4 );

  //
  // SPN 100 Engine Oil Pressure - Byte 4 (Index 3), 4 kPa per bit, 0 offset.
  // The original firmware left this byte at 0xFF, so J1939 consumers never saw oil pressure
  // at all (only the J1587 PID 100 message carried it).  Now populated.
  //

  if( QA_IS_SET( fOvrOilPressKPA ) )
    {
      ucData[ 3 ] = (U8) ( fOvrOilPressKPA / 4.0 );
    }
  else
    {
      ucData[ 3 ] = (U8) ( ( MAX_PRESS_KPA * fA4_OilPressurePercent ) / 4.0 );
    }
  
  //
  // Send the message.
  //
  
  CAN0.sendMsgBuf( ulCANID, 1, 8, ucData ); delay( J1939_INTERMESSAGE_DELAY_TIME );
  
  //
  // Print message to Arduino serial port.
  //
  
  PrintJ1939Message( (char *) "Tx", ulCANID, ulPGN, ucSRC, ucDST, ucData, ucLen  );
  
}//Send_PGN_65263_FuelDeliveryPressure
///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_PGN_65262_CoolantTemp - NOW CONTROLLED BY A2 POT
///
///       Bit   Length   SPN Description                          SPN
///       1     1 byte   Engine Coolant Temperature               110  <-- NOW CONTROLLED BY A2
///       2     1 byte   Fuel Temperature                         174
///       3-4   2 bytes  Engine Oil Temperature 1                 175
///       5-6   2 bytes  Turbo Oil Temperature                    176
///       7     1 byte   Engine Intercooler Temperature            52
///       8     1 byte   Engine Intercooler Thermostat Opening   1134
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_PGN_65262_CoolantTemp( void )
{
  U8    ucPRI              = 6;
  UL    ulPGN              = 65262;
  UL    ulCANID            = 0;
  U8    ucSRC              = 0;
  U8    ucDST              = 0xFF;
  U8    ucLen              = 8;
  U8    ucData         [ 8 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  float fTempC             = 0;
  
  //
  // Calculate the CANID.
  //
  
  ulCANID = PGNToCANID( ulPGN, ucPRI, ucSRC, ucDST );
  
  //
  // Base temperature controlled by A2 potentiometer
  // SPN 110: Engine Coolant Temperature - Range -40 to 210°C, Resolution 1°C/bit, Offset -40°C
  //
  
  fTempC = QA_IS_SET( fOvrCoolantC ) ? fOvrCoolantC : (float) ( MAX_TEMP_C * fA2_VehicleSpeedPercent );
  
  //
  // 1C/0 offset temps.
  //
  
  ucData[ 0 ] = (U8) ( ( fTempC + 40 ) / 1 );  // SPN 110, Coolant Temp (A2 POT CONTROL)
  ucData[ 1 ] = (U8) ( ( fTempC + 40 ) / 1 );  // SPN 174, Fuel Temp
  ucData[ 6 ] = (U8) ( ( fTempC + 40 ) / 1 );  // SPN  52, Engine Intercooler Temp
  
  //
  // 0.03125C/-273 temps.
  //
  
  fTempC = (float) ( ( ( QA_IS_SET( fOvrCoolantC ) ? fOvrCoolantC : (MAX_TEMP_C * fA2_VehicleSpeedPercent) ) + 273.00 ) / 0.03125 );
  
  CreateTwoByteIntegerLSB( (UL) fTempC, &ucData[ 2 ] );// SPN 175, Engine Oil Temp
  CreateTwoByteIntegerLSB( (UL) fTempC, &ucData[ 4 ] );// SPN 176, Turbo Oil Temp
  
  //
  // Send the message.
  //
  
  CAN0.sendMsgBuf( ulCANID, 1, 8, ucData ); delay( J1939_INTERMESSAGE_DELAY_TIME );
  
  //
  // Print message to Arduino serial port.
  //
  
  PrintJ1939Message( (char *) "Tx", ulCANID, ulPGN, ucSRC, ucDST, ucData, ucLen  );
  
}//Send_PGN_65262_CoolantTemp


// ============================================================================
// NEW FUNCTION 3: Send_PGN_64976_IntakeManifoldPressure
// Add this NEW function to your code
// Controlled by A0 potentiometer
// ============================================================================

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_PGN_64976_IntakeManifoldPressure - CONTROLLED BY A0 POT
///
///       Bit   Length   SPN Description                                    SPN
///       1-2   2 bytes  Engine Intake Manifold #1 Absolute Pressure       3563  <-- A0 CONTROL
///       3     1 byte   Engine Intake Manifold #1 Relative Pressure       3564
///       4-5   2 bytes  Engine Intake Manifold #2 Absolute Pressure       3565
///       6     1 byte   Engine Intake Manifold #2 Relative Pressure       3566
///       7     1 byte   Reserved
///       8     1 byte   Reserved
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_PGN_64976_IntakeManifoldPressure( void )
{
  U8    ucPRI              = 6;
  UL    ulPGN              = 64976;
  UL    ulCANID            = 0;
  U8    ucSRC              = 0;
  U8    ucDST              = 0xFF;
  U8    ucLen              = 8;
  U8    ucData         [ 8 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  float fManifoldPressKPA  = 0;
  UL    ulPressureValue    = 0;
  
  //
  // Calculate the CANID.
  //
  
  ulCANID = PGNToCANID( ulPGN, ucPRI, ucSRC, ucDST );
  
  //
  // Calculate intake manifold pressure using A0 potentiometer
  // SPN 3563: Engine Intake Manifold #1 Absolute Pressure
  // Range: 0-500 kPa, Resolution: 0.0078125 kPa/bit (0.5 kPa / 64), Offset: 0
  //
  
  fManifoldPressKPA = (float) ( 500.0 * fA0_EngineSpeedPercent ); // Max 500 kPa
  
  //
  // Convert to J1939 format: value = (kPa - offset) / resolution
  // Resolution = 0.0078125, so multiply by 128 (1/0.0078125 = 128)
  //
  
  ulPressureValue = (UL) ( ( fManifoldPressKPA + 0 ) / 0.0078125 );
  
  //
  // SPN 3563 - Bytes 1-2 (Index 0-1), LSB first
  //
  
  CreateTwoByteIntegerLSB( ulPressureValue, &ucData[ 0 ] );
  
  //
  // Send the message.
  //
  
  CAN0.sendMsgBuf( ulCANID, 1, 8, ucData ); delay( J1939_INTERMESSAGE_DELAY_TIME );
  
  //
  // Print message to Arduino serial port.
  //
  
  PrintJ1939Message( (char *) "Tx", ulCANID, ulPGN, ucSRC, ucDST, ucData, ucLen  );
  
}//Send_PGN_64976_IntakeManifoldPressure

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_PGN_65269_AmbientAirTemp
///
///       Bit   Length  SPN Description            SPN
///       1     1 byte  Barometric Pressure        108
///       2-3   2 bytes Cab Interior Temperature   170
///       4-5   2 bytes Ambient Air Temperature    171
///       6     1 byte  Air Inlet Temperature      172
///       7-8   2 bytes Road Surface Temperature    79
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_PGN_65269_AmbientAirTemp( float fPercent )
{
  U8    ucPRI              = 6;
  UL    ulPGN              = 65269;
  UL    ulCANID            = 0;
  U8    ucSRC              = 0;
  U8    ucDST              = 0xFF;
  U8    ucLen              = 8;
  U8    ucData         [ 8 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  float TempC              = 0;
  
  //
  // Calculate the CANID.
  //
  
  ulCANID = PGNToCANID( ulPGN, ucPRI, ucSRC, ucDST );
  
  //
  // Calculate the 0.03125/-273 values.
  //
  
  TempC = ( ( ( QA_IS_SET( fOvrAmbientC ) ? fOvrAmbientC : (MAX_TEMP_C * fPercent) ) + 273.00 ) / 0.03125 );
  
  CreateTwoByteIntegerLSB( (UL) TempC, &ucData[ 1 ] );    // SPN 170, Cab Interior Temperature.
  CreateTwoByteIntegerLSB( (UL) TempC, &ucData[ 3 ] );    // SPN 171, Ambient Air Temperature.
  CreateTwoByteIntegerLSB( (UL) TempC, &ucData[ 6 ] );    // SPN 79, Road Surface Temperature.
  
  //
  // Calculate the 1/-40 value.
  //
  
  ucData[ 5 ] = ( ( ( QA_IS_SET( fOvrAmbientC ) ? fOvrAmbientC : (MAX_TEMP_C * fPercent) ) + 40 ) / 1 ); // SPN 172, Air Inlet Temperature.
  
  //
  // Send the message.
  //
  
  CAN0.sendMsgBuf( ulCANID, 1, 8, ucData ); delay( J1939_INTERMESSAGE_DELAY_TIME );
  
  //
  // Print message to Arduino serial port.
  //
  
  PrintJ1939Message( (char *) "Tx", ulCANID, ulPGN, ucSRC, ucDST, ucData, ucLen  );
  
}//Send_PGN_65269_AmbientAirTemp

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_PGN_65270_IntakeManifoldTemp
///
///       Bit  Length   SPN Description                           SPN
///       1    1 byte   Particulate Trap Inlet Pressure            81
///       2    1 byte   Boost Pressure                            102
///       3    1 byte   Intake Manifold 1 Temperature             105
///       4    1 byte   Air Inlet Pressure                        106
///       5    1 byte   Air Filter 1 Differential Pressure        107
///       6-7  2 bytes  Exhaust Gas Temperature                   173
///       8    1 byte   Coolant Filter Differential Pressure      112
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_PGN_65270_IntakeManifoldTemp( float fPercent )
{
  U8    ucPRI              = 6;
  UL    ulPGN              = 65270;
  UL    ulCANID            = 0;
  U8    ucSRC              = 0;
  U8    ucDST              = 0xFF;
  U8    ucLen              = 8;
  U8    ucData       [ 8 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  float fTempC             = 0;
  
  //
  // Calculate the CANID.
  //
  
  ulCANID = PGNToCANID( ulPGN, ucPRI, ucSRC, ucDST );
  
  //
  // SPN 105 Intake Manifold 1 Temperature 1C/-40.
  //
  
  ucData[ 2 ] = ( ( (MAX_TEMP_C * fPercent) + 40 ) / 1 ); 

  //
  // SPN 102 Boost Pressure - Byte 2 (Index 1), 2 kPa per bit, 0 offset.
  // The original firmware left this byte at 0xFF (not available); QA can now drive it.
  //

  if( QA_IS_SET( fOvrBoostKPA ) )
    {
      ucData[ 1 ] = (U8) ( fOvrBoostKPA / 2.0 );
    }
  
  //
  // SPN 173 Exhaust Gas Temperature 0.03125/-273.
  //
  
  fTempC = ( ( (MAX_TEMP_C * fPercent) + 273 ) / 0.03125 ); 
  
  CreateTwoByteIntegerLSB( (UL) fTempC, &ucData[ 5 ] );
  
  //
  // Send the message.
  //
  
  CAN0.sendMsgBuf( ulCANID, 1, 8, ucData ); delay( J1939_INTERMESSAGE_DELAY_TIME );
  
  //
  // Print message to Arduino serial port.
  //
  
  PrintJ1939Message( (char *) "Tx", ulCANID, ulPGN, ucSRC, ucDST, ucData, ucLen  );
  
}//Send_PGN_65270_IntakeManifoldTemp

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_PGN_65271_Voltages
///
///       Bit  Length   SPN Description                         SPN
///       1    1 byte   Net Battery Current                     114
///       2    1 byte   Alternator Current                      115
///       3-4  2 bytes  Alternator Potential (Voltage)          167
///       5-6  2 bytes  Electrical Potential (Voltage)          168
///       7-8  2 bytes  Battery Potential (Voltage), Switched   158
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_PGN_65271_Voltages( float fPercent )
{
  U8    ucPRI              = 6;
  UL    ulPGN              = 65271;
  UL    ulCANID            = 0;
  U8    ucSRC              = 0;
  U8    ucDST              = 0xFF;
  U8    ucLen              = 8;
  U8    ucData       [ 8 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  float fVoltage           = 0;
  
  //
  // Calculate the CANID.
  //
  
  ulCANID = PGNToCANID( ulPGN, ucPRI, ucSRC, ucDST );
  
  //
  // All voltages are 0.05/0
  //
  
  fVoltage = (float) ( ( QA_IS_SET( fOvrVoltage ) ? fOvrVoltage : ( MAX_VOLTAGE * fPercent ) ) / 0.05 );
  
  //
  // SPN 167 Alternator Potential (Voltage).
  // SPN 168 Electrical Potential (Voltage).
  // SPN 158 Battery Potential (Voltage).
  //
  
  CreateTwoByteIntegerLSB( (UL) fVoltage, &ucData[ 2 ] );
  CreateTwoByteIntegerLSB( (UL) fVoltage, &ucData[ 4 ] );
  CreateTwoByteIntegerLSB( (UL) fVoltage, &ucData[ 6 ] );
  
  //
  // Send the messsage.
  //
  
  CAN0.sendMsgBuf( ulCANID, 1, 8, ucData ); delay( J1939_INTERMESSAGE_DELAY_TIME );
  
  //
  // Print message to Arduino serial port.
  //
  
  PrintJ1939Message( (char *) "Tx", ulCANID, ulPGN, ucSRC, ucDST, ucData, ucLen  );
  
}//Send_PGN_65271_Voltages

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_PGN_65272_TransOilTemp
///
///       Bit  Length  SPN Description                                SPN
///       1    1 byte  Clutch Pressure                                123
///       2    1 byte  Transmission Oil Level                         124
///       3    1 byte  Transmission Filter Differential Pressure      126
///       4    1 byte  Transmission Oil Pressure                      127
///       5-6  2 bytes Transmission Oil Temperature                   177
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_PGN_65272_TransOilTemp( float fPercent )
{
  U8    ucPRI              = 6;
  UL    ulPGN              = 65272;
  UL    ulCANID            = 0;
  U8    ucSRC              = 3;
  U8    ucDST              = 0xFF;
  U8    ucLen              = 8;
  U8    ucData       [ 8 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  float fTempC             = 0;
  
  //
  // Calculate the CANID.
  //
  
  ulCANID = PGNToCANID( ulPGN, ucPRI, ucSRC, ucDST );
  
  //
  // Calculate the temp.
  //
  
  fTempC = ( ( ( QA_IS_SET( fOvrTransOilC ) ? fOvrTransOilC : (MAX_TEMP_C * fPercent) ) + 273 ) / 0.03125 );
  
  //
  // SPN 177.
  //
  
  CreateTwoByteIntegerLSB( (UL) fTempC, &ucData[ 4 ] ); 
  
  //
  // Send the message.
  //
  
  CAN0.sendMsgBuf( ulCANID, 1, 8, ucData ); delay( J1939_INTERMESSAGE_DELAY_TIME );
  
  //
  // Print message to Arduino serial port.
  //
  
  PrintJ1939Message( (char *) "Tx", ulCANID, ulPGN, ucSRC, ucDST, ucData, ucLen  );
  
}//Send_PGN_65272_TransOilTemp

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_J1939_DTCs - Send all 22 generic DTCs that follow the same format.
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_J1939_DTCs( U8 ucSRC )
{
  U8    ucPRI              = 6;
  UL    ulPGN              = 0;
  UL    ulSPN              = 0;
  U8    ucFMI              = 0;
  U8    ucOC               = 0;
  UL    ulCANID            = 0;
  U8    ucDST              = 0xFF;
  U8    ucLen              = 8;
  U8    ucIdx              = 0;
  U8    ucData      [  8 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  UL    ulPGNs      [ 22 ] = { 65226, 65227, 65231, 65236, 64949, 64898, 64896, 40704, 64863, 64862, 64861,
                               64860, 64859, 64858, 64857, 64856, 64855, 64854, 64853, 64852, 64721, 64722 };
  
  //----------------------------------------------------------------------------------------------------
  // The following 22 DTCs are of the same form and can be sent quickly in one sweep.
  //
  // 65226: DM1    64898: DM27  64861: DM43  64856: DM48  64721: DM53
  // 65227: DM2    64896: DM28  64860: DM44  64855: DM49  64722: DM54
  // 65231: DM6    40704: DM35  64859: DM45  64854: DM50
  // 65236: DM12   64863: DM41  64858: DM46  64853: DM51
  // 64949: DM23   64862: DM42  64857: DM47  64852: DM52
  //----------------------------------------------------------------------------------------------------
  
  //
  // Send DTCs.
  //
  
  for( ucIdx = 0; ucIdx < 22; ucIdx++ )
    {
      //
      // Get the PGN number for the DTC.
      //
      
      ulPGN = ulPGNs[ucIdx];
      
      //
      // Set SPNs of 1000..1021, OC of 0..21, and FMI of 22..1
      //
      
      switch( ulPGN )
        {
        case 65226: ulSPN = 1000; ucOC =  0; ucFMI = 22; break;
        case 65227: ulSPN = 1001; ucOC =  1; ucFMI = 21; break;
        case 65231: ulSPN = 1002; ucOC =  2; ucFMI = 20; break;
        case 65236: ulSPN = 1003; ucOC =  3; ucFMI = 19; break;
        case 64949: ulSPN = 1004; ucOC =  4; ucFMI = 18; break;
        case 64898: ulSPN = 1005; ucOC =  5; ucFMI = 17; break;
        case 64896: ulSPN = 1006; ucOC =  6; ucFMI = 16; break;
        case 40704: ulSPN = 1007; ucOC =  7; ucFMI = 15; break;
        case 64863: ulSPN = 1008; ucOC =  8; ucFMI = 14; break;
        case 64862: ulSPN = 1009; ucOC =  9; ucFMI = 13; break;
        case 64861: ulSPN = 1010; ucOC = 10; ucFMI = 12; break;
        case 64860: ulSPN = 1011; ucOC = 11; ucFMI = 11; break;
        case 64859: ulSPN = 1012; ucOC = 12; ucFMI = 10; break;
        case 64858: ulSPN = 1013; ucOC = 13; ucFMI =  9; break;
        case 64857: ulSPN = 1014; ucOC = 14; ucFMI =  8; break;
        case 64856: ulSPN = 1015; ucOC = 15; ucFMI =  7; break;
        case 64855: ulSPN = 1016; ucOC = 16; ucFMI =  6; break;
        case 64854: ulSPN = 1017; ucOC = 17; ucFMI =  5; break;
        case 64853: ulSPN = 1018; ucOC = 18; ucFMI =  4; break;
        case 64852: ulSPN = 1019; ucOC = 19; ucFMI =  3; break;
        case 64721: ulSPN = 1020; ucOC = 20; ucFMI =  2; break;
        case 64722: ulSPN = 1021; ucOC = 21; ucFMI =  1; break;
        }
      
      //
      // Set the CANID.
      //
      
      ulCANID = PGNToCANID( ulPGN, ucPRI, ucSRC, ucDST );
      
      //
      // Null out the data packet.
      //
      
      memset( &ucData[ 0 ], 0xFF, 8 );
      
      //
      // Create the message.
      //
      
      ucData[ 0 ] = 0x55 ;                                   // Data[ 0 ]     = Lamp Stat (all on).
      ucData[ 1 ] = 0x55 ;                                   // Data[ 1 ]     = Flash Lamp Stat (all on).
                                                           //
      CreateThreeByteIntegerLSB( (UL) ulSPN, &ucData[ 2 ] ); // Data[2,3,4] = SPN_LSB.
                                                           //
      ucData[ 4 ]  = ucFMI ;                                 // Data[ 4 ]     = CM=0=Latest | FMI.
      ucData[ 5 ]  = ucOC  ;                                 // Data[ 5 ]     = OC.
      
      //
      // Send the message.
      //
      
      CAN0.sendMsgBuf( ulCANID, 1, 8, ucData ); delay( J1939_INTERMESSAGE_DELAY_TIME );
      
      //
      // Print message to Arduino serial port.
      //
      
      PrintJ1939Message( (char *) "Tx", ulCANID, ulPGN, ucSRC, ucDST, ucData, ucLen  );
      
    }// For all PGNs
  
}// Send_J1939_DTCs

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_PGN_65259_ComponentInfo
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_PGN_65259_ComponentInfo( void )
{
  U8    ucPRI               = 6;
  UL    ulPGN               = 65259;
  U8    ucDataEng    [ 29 ] = {'C','M','M','N','S','*','I','S','X','9','4','5','*','9','8','7','6','7','8','*','D','o','o','r','#','5','4','3','*'}; // 29
  U8    ucDataTrans  [ 29 ] = {'E','A','T','O','N','*','A','S','X','2','1','0','*','8','9','7','6','7','8','*','E','T','X','A','F','5','4','3','*'}; // 29
  U8    ucDataABS    [ 29 ] = {'W','A','B','C','O','*','4','S','4','M','V','1','*','1','3','5','7','9','B','*','X','O','2','F','F','4','4','3','*'}; // 29
  
  Send_BAM_Message( ucPRI, ulPGN,  0, ucDataEng  , 29 ); // Engine
  Send_BAM_Message( ucPRI, ulPGN,  3, ucDataTrans, 29 ); // Trans
  Send_BAM_Message( ucPRI, ulPGN, 11, ucDataABS  , 29 ); // ABS
  
}// Send_PGN_65259_ComponentInfo

///----------------------------------------------------------------------------------------------------
///<summary>
///    Send_PGN_65242_SoftwareID
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void  Send_PGN_65242_SoftwareID( void )
{
  U8    ucPRI               = 6;
  UL    ulPGN               = 65242;
  U8    ucDataEng    [ 15 ] = { 1,'1','2','3','4','5','6','7','8','9','A','B','C','D','E'}; // 15
  U8    ucDataTrans  [ 15 ] = { 1,'1','2','3','4','5','6','7','8','9','A','B','C','D','E'}; // 15
  U8    ucDataABS    [ 15 ] = { 1,'1','2','3','4','5','6','7','8','9','A','B','C','D','E'}; // 15
  
  Send_BAM_Message( ucPRI, ulPGN,  0, ucDataEng  , 15 ); // Engine
  Send_BAM_Message( ucPRI, ulPGN,  3, ucDataTrans, 15 ); // Trans
  Send_BAM_Message( ucPRI, ulPGN, 11, ucDataABS  , 15 ); // ABS
  
}// Send_PGN_65242_SoftwareID

///----------------------------------------------------------------------------------------------------
///<summary>
///    Send_PGN_65260_VIN
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void  Send_PGN_65260_VIN( void )
{
  U8    ucSRC             = 0;
  U8    ucPRI             = 6;
  UL    ulPGN             = 65260;
  U8    ucDataKW   [ 17 ] = {'1','X','K','D','P','4','E','X','5','D','J','3','4','6','5','2','7'};
  
  Send_BAM_Message( ucPRI, ulPGN, ucSRC, ucDataKW, 17 ); // Engine
  
}// Send_PGN_65260_VIN

///----------------------------------------------------------------------------------------------------
///<summary>
///    Send_PGN_64965_ECUID
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void  Send_PGN_64965_ECUID( void )
{
  U8    ucPRI             = 6;
  UL    ulPGN             = 64965;
  U8    ucDataEng  [ 19 ] = { '1','2','3','*','4','5','6','*','7','A','*','C','*','C','M','M','N','S','*'}; // 19
  U8    ucDataTrans[ 19 ] = { '1','2','3','*','4','5','6','*','7','A','*','C','*','E','A','T','O','N','*'}; // 19
  U8    ucDataABS  [ 19 ] = { '1','2','3','*','4','5','6','*','7','A','*','C','*','W','A','B','C','O','*'}; // 19
  
  Send_BAM_Message( ucPRI, ulPGN,  0, ucDataEng  , 19 ); // Engine
  Send_BAM_Message( ucPRI, ulPGN,  3, ucDataTrans, 19 ); // Trans
  Send_BAM_Message( ucPRI, ulPGN, 11, ucDataABS  , 19 ); // ABS
  
}// Send_PGN_64965_ECUID

///----------------------------------------------------------------------------------------------------
///<summary>
///    Send_PGN_64906_J2012DTCs
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void  Send_PGN_64906_J2012DTCs( void )
{
  U8 ucPRI              = 6;
  UL ulPGN              = 64906;
  UL ulCANID            = 0;
  U8 ucSRC              = 0;
  U8 ucDST              = 0xFF;
  U8 ucData      [ 13 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  
  //
  // Calculate the CANID.
  //
  
  ulCANID = PGNToCANID( ulPGN, ucPRI, ucSRC, ucDST );
  
  //
  // Fill in the data.
  //
  
  ucData[  0 ] =  2 ;  // 2 fault codes
  ucData[  1 ] = 'P';
  ucData[  2 ] = '0';
  ucData[  3 ] = '1';
  ucData[  4 ] = '1';
  ucData[  5 ] = '0';
  ucData[  6 ] = 0x85; // Active, OC of 5
  ucData[  7 ] = 'B';
  ucData[  8 ] = '0';
  ucData[  9 ] = '2';
  ucData[ 10 ] = '1';
  ucData[ 11 ] = '0';
  ucData[ 12 ] = 0x08; // Inactive, OC of 8
  
  //
  // Send the message.
  //
  
  Send_BAM_Message( ucPRI, ulPGN, 0, ucData, 13 );
  
  //
  // Print message to Arduino serial port.
  //
  
  PrintJ1939Message( (char *) "Tx", ulCANID, ulPGN, ucSRC, ucDST, ucData, 13 );
  
}//  Send_PGN_64906_J2012DTCs

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_PGN_40448_DTCCounts
///
///       Parameter Group Number:  40448 (009E0016)
///       Byte:    1   Pending DTCs             See 5.7.29.1    
///       Byte:    2   All Pending DTCs         See 5.7.29.2
///       Byte:    3   MIL-On DTCs              See 5.7.29.3
///       Byte:    4   Previously MIL-On DTCs   See 5.7.29.4
///       Byte:    5   Permanent DTCs           See 5.7.29.5
///       Bytes: 6-8   Reserved for SAE assignment
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_PGN_40448_DTCCounts( void )
{
  U8 ucPRI              = 6;
  UL ulPGN              = 40448;
  UL ulCANID            = 0;
  U8 ucSRC              = 0;
  U8 ucDST              = 0xFF;
  U8 ucData       [ 8 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  
  //
  // Calculate the CANID.
  //
  
  ulCANID = PGNToCANID( ulPGN, ucPRI, ucSRC, ucDST );
  
  //
  // Fill in the data.
  //
  
  ucData[ 0 ]              = NUM_PENDING_FAULTS      ; // Pending
  ucData[ 1 ]              = NUM_ALL_PENDING_FAULTS  ; // All pending.
  ucData[ 2 ]              = NUM_MIL_ON_DTCS         ; // MIL-On DTCs
  ucData[ 3 ]              = NUM_INACTIVE_MIL_ON_DTCS; // Inactive MIL-On DTCs
  ucData[ 4 ]              = NUM_PERMANENT_DTCS      ; // Permanent
  
  //
  // Send the message.
  //
  
  CAN0.sendMsgBuf( ulCANID, 1, 8, ucData ); delay( J1939_INTERMESSAGE_DELAY_TIME );
  
  //
  // Print message to Arduino serial port.
  //
  
  PrintJ1939Message( (char *) "Tx", ulCANID, ulPGN, ucSRC, ucDST, ucData, 8 );
  
}// Send_PGN_40448_DTCCounts

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_PGN_65229_FreezeFrame
///
///         Byte:      1                Freeze Frame Length                            See 5.7.4.1
//
///         Byte:      2      bits 8-1  SPN, 8 least significant bits of SPN           See 5.7.1.9
///         Byte:      3      bits 8-1  SPN, second byte of SPN                        See 5.7.1.9
///         Byte:      4      bits 8-6  SPN, 3 most significant bits                   See 5.7.1.9
///                           bits 5-1  FMI                                            See 5.7.1.10
//
///         Byte:      5      bit 8     SPN Conversion Method                          See 5.7.1.11
///                           bits 7-1  Occurrence Count                               See 5.7.1.12
//
///         Byte:      6                Engine Torque Mode (SPN 899)                   See SAE J1939-71
///         Byte:      7                Boost (SPN 102)                                See SAE J1939-71
///         Byte:      8                Engine Speed (SPN 190) (LSB)                   See SAE J1939-71
///         Byte:      9                Engine Speed (SPN 190) (MSB)                   See SAE J1939-71
///         Byte:     10                Engine % Load (SPN 92)                         See SAE J1939-71
///         Byte:     11                Engine coolant temperature (SPN 110)           See SAE J1939-71
///         Byte:     12                Vehicle Speed (SPN 84) (LSB)                   See SAE J1939-71
///         Byte:     13                Vehicle Speed (SPN 84) (MSB)                   See SAE J1939-71
///         Byte:     14-n              Manufacturer Specific information
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_PGN_65229_FreezeFrame( void )
{
  U8  ucPRI              = 6;
  UL  ulPGN              = 65229;
  
  U8  ucData     [ 100 ] = { 0xFF };
  
  U8  ucFFLength         = 15;                                        // Length
  UL  ulSPN              = 897;                                       // SPN = 897 - Override Control Mode Priority
  U8  ucFMI              = 2;                                         // FMI = 2 - Data Erratic, Intermittent Or Incorrect
  U8  ucOC               = 10;                                        // OC = 10
  U8  ucEngTorqueMode    = 0x08;                                      // Torque Limiting
  U8  ucEngBoost         = (U8)  ( ( 172.369 + 0.00 ) / 2.00       ); // 172 kPA - 25PSI
  U16 usEngSpeed         = (U16) ( ( 900.00  + 0.00 ) / 0.125      ); // 900 rpm, 0.125rpm/0 
  U8  ucEngPctLoad       = (U8)  ( (  80.00  + 0.00 ) / 1.000      ); // 80%, 1%/0 
  U8  ucEngCoolTemp      = (U8)  ( ( 100.00  + 40.0 ) / 1.000      ); // 212F, 1 deg C/bit , -40 deg C offset
  U16 usVehicleSpeed     = (U16) ( ( 104.607 + 0.00 ) / 0.00390625 ); // 65 mph - 1/256 km/h per bit , 0 offset
  
  //
  // Fill out the message.
  //
  ucData[  0 ]                                            = ucFFLength;
  ucData[  1 ]                                            = BYTE0_OF_UL( ulSPN );
  ucData[  2 ]                                            = BYTE1_OF_UL( ulSPN );
  ucData[  3 ]                                            = BYTE2_OF_UL( ulSPN );
  ucData[  3 ]                                            = (U8) ( (U8) ucData[ 3 ] << (U8) 5 );
  ucData[  3 ]                                            = (U8) ( ucData[ 3 ] | ucFMI );
  ucData[  4 ]                                            = ucOC;
  ucData[  5 ]                                            = ucEngTorqueMode;
  ucData[  6 ]                                            = ucEngBoost;
  CreateTwoByteIntegerLSB( usEngSpeed, &ucData[ 7 ] );
  ucData[  9 ]                                            = ucEngPctLoad;
  ucData[ 10 ]                                            = ucEngCoolTemp;
  CreateTwoByteIntegerLSB( usVehicleSpeed, &ucData[ 11 ] );
  ucData[ 13 ]                                            = 0x0A;
  ucData[ 14 ]                                            = 0x0B;
  ucData[ 15 ]                                            = 0x0C;
  //
  ucData[ 16 ]                                            = 0x00;
  ucData[ 17 ]                                            = 0x00; // 1
  ucData[ 18 ]                                            = 0x00; // 2
  ucData[ 19 ]                                            = 0x00; // 3
  ucData[ 19 ]                                            = 0x00; // 3
  ucData[ 19 ]                                            = 0x00; // 3
  ucData[ 20 ]                                            = 0x00; // 4
  ucData[ 21 ]                                            = 0x00; // 5
  ucData[ 22 ]                                            = 0x00; // 6
  CreateTwoByteIntegerLSB( 0x0000, &ucData[ 23 ] );               // 7-8
  ucData[ 25 ]                                            = 0x00; // 9
  ucData[ 26 ]                                            = 0x00; // 10
  CreateTwoByteIntegerLSB( 0x0000, &ucData[ 27 ] );               // 11-12

  ucData[ 29 ]                                            = ucFFLength;
  ucData[ 30 ]                                            = BYTE0_OF_UL( ulSPN + 1 );
  ucData[ 31 ]                                            = BYTE1_OF_UL( ulSPN + 1 );
  ucData[ 32 ]                                            = BYTE2_OF_UL( ulSPN + 1 );
  ucData[ 32 ]                                            = (U8) ( (U8) ucData[ 3 ] << (U8) 5 );
  ucData[ 32 ]                                            = (U8) ( ucData[ 3 ] | ucFMI );
  ucData[ 33 ]                                            = ucOC;
  ucData[ 34 ]                                            = ucEngTorqueMode;
  ucData[ 35 ]                                            = ucEngBoost;
  CreateTwoByteIntegerLSB( usEngSpeed, &ucData[ 36 ] );
  ucData[ 38 ]                                            = ucEngPctLoad;
  ucData[ 39 ]                                            = ucEngCoolTemp;
  CreateTwoByteIntegerLSB( usVehicleSpeed, &ucData[ 40 ] );
  ucData[ 42 ]                                            = 0x0A;
  ucData[ 43 ]                                            = 0x0B;
  ucData[ 44 ]                                            = 0x0C;
  
  Send_BAM_Message( ucPRI, ulPGN, 0, ucData, 45 );
  
}//Send_PGN_65229_FreezeFrame

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// PROPRIETARY - Send_PGN_DM25_ExpandedFreezeFrame
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
/*PROCEDURE*/ void Send_PGN_DM25_ExpandedFreezeFrame( void )
{
  U8 ucPRI              = 6;
  UL ulPGN              = 64951;
  UL ulCANID            = 0;
  U8 ucSRC              = 0;
  U8 ucDST              = 0xFF;
  U8 ucData      [ 20 ] = {0xFF};
  
  //
  // Calculate the CANID.
  //
  
  ulCANID = PGNToCANID( ulPGN, ucPRI, ucSRC, ucDST );

  //
  // FF Length, SPN, FMI, OC
  //
  
  ucData[  0 ] =    6; // FF Length
  ucData[  1 ] = 0xBE; // SPN 190 LSB 0x00005B
  ucData[  2 ] = 0x00; // SPN 190 MID 0x00005B
  ucData[  3 ] = 0x03; // SPN 190 Top3Bits | FMI = 3
  ucData[  4 ] = 0x05; // CM/OC = 5
  ucData[  5 ] = 0x0A; // SPN Data                     
  ucData[  6 ] = 0x0B; // SPN Data                     
  
  //
  // Send the message.
  //
  
  Send_BAM_Message( ucPRI, ulPGN, ucSRC, ucData, 11 ); // Engine
  
  //
  // Print message to Arduino serial port.
  //
  
  PrintJ1939Message( (char *) "Tx", ulCANID, ulPGN, ucSRC, ucDST, ucData, 6  );
  
}//Send_PGN_DM25_ExpandedFreezeFrame


//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// PROPRIETARY - Send_PGN_DM24_SPNSupport
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
/*PROCEDURE*/ void Send_PGN_DM24_SPNSupport( void )
{
  U8 ucPRI              = 6;
  UL ulPGN              = 64950;
  UL ulCANID            = 0;
  U8 ucSRC              = 0;
  U8 ucDST              = 0xFF;
  U8 ucData        [20] = {0xFF};
  
  //
  // Calculate the CANID.
  //
  
  ulCANID = PGNToCANID( ulPGN, ucPRI, ucSRC, ucDST );

  //
  // FF Length, SPN, FMI, OC
  //
  
  ucData[ 0 ]  = 0xBE; // SPN 190 Engine Speed LSB
  ucData[ 1 ]  = 0x00; // SPN 190 Engine Speed MID
  ucData[ 2 ]  = 0x00; // SPN 190 Engine Speed TOP3
  ucData[ 2 ] |= 0x01; // Bit 5 = Reserved                                  <NO>
                       // Bit 4 = Supported in Data Value DM58              <NO>
                       // Bit 3 = Supported in Scaled Test Results          <NO>
                       // Bit 2 = Supported in Data Stream                  <NO>
                       // Bit 1 = Supported in DM25 (Expanded Freeze Frame) <YES>
  ucData[ 3 ] = 0x02;  // SPN Data Length

  //
  // Send the message.
  //
  
  CAN0.sendMsgBuf( ulCANID, 1, 4, ucData ); delay( J1939_INTERMESSAGE_DELAY_TIME );

  //
  // Print message to Arduino serial port.
  //
  
  PrintJ1939Message( (char *) "Tx", ulCANID, ulPGN, ucSRC, ucDST, ucData, 4  );

  //
  // Transmission - SPN 190 Input Shaft Speed
  //
  
  ucPRI              = 6;
  ulPGN              = 64950;
  ulCANID            = 0;
  ucSRC              = 3;
  ucDST              = 0xFF;
  
  //
  // Calculate the CANID.
  //
  
  ulCANID = PGNToCANID( ulPGN, ucPRI, ucSRC, ucDST );

  //
  // FF Length, SPN, FMI, OC
  //
  
  ucData[ 0 ]  = 0xA1; // SPN 161 Engine Speed LSB
  ucData[ 1 ]  = 0x00; // SPN 161 Engine Speed MID
  ucData[ 2 ]  = 0x00; // SPN 161 Engine Speed TOP3
  ucData[ 2 ] |= 0x03; // Bit 5 = Reserved                                  <NO>
                       // Bit 4 = Supported in Data Value DM58              <NO>
                       // Bit 3 = Supported in Scaled Test Results          <NO>
                       // Bit 2 = Supported in Data Stream                  <YES>
                       // Bit 1 = Supported in DM25 (Expanded Freeze Frame) <YES>
  ucData[ 3 ] = 0x02;  // SPN Data Length

  //
  // Send the message.
  //
  
  CAN0.sendMsgBuf( ulCANID, 1, 4, ucData ); delay( J1939_INTERMESSAGE_DELAY_TIME );

  //
  // Print message to Arduino serial port.
  //
  
  PrintJ1939Message( (char *) "Tx", ulCANID, ulPGN, ucSRC, ucDST, ucData, 4  );
  
}//Send_PGN_DM24_SPNSupport

///----------------------------------------------------------------------------------------------------
///<summary>
///    Send_PGN_65217_DistancesHR
///
///       Bit   Length    SPN Description             SPN
///       1-4   4 bytes   Total Vehicle Distance HR   918  5 meters/bit
///       5-8   4 bytes   Trip Distance HR            917  5 meters/bit
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void  Send_PGN_65217_DistancesHR( void )
{
  U8    ucPRI              = 6;
  UL    ulPGN              = 65217;
  UL    ulCANID            = 0;
  U8    ucSRC              = 0;
  U8    ucDST              = 0xFF;
  U8    ucLen              = 8;
  U8    ucData       [ 8 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  UL    ulTripDist         = TOTAL_TRIP_DISTANCE;
  UL    ulTotDist          = TOTAL_VEHICLE_DISTANCE;
  
  //
  // Calculate the CANID.
  //
  
  ulCANID = PGNToCANID( ulPGN, ucPRI, ucSRC, ucDST );
  
  CreateFourByteIntegerLSB( (UL) ulTotDist  / 5.0, &ucData[ 0 ] ); // Total Vehicle Distance.
  CreateFourByteIntegerLSB( (UL) ulTripDist / 5.0, &ucData[ 4 ] ); // Trip Dist.
  
  //
  // Send the message.
  //
  
  CAN0.sendMsgBuf( ulCANID, 1, 8, ucData ); delay( J1939_INTERMESSAGE_DELAY_TIME );
  
  //
  // Print message to Arduino serial port.
  //
  
  PrintJ1939Message( (char *) "Tx", ulCANID, ulPGN, ucSRC, ucDST, ucData, ucLen  );
  
}// Send_PGN_65217_DistancesHR

///----------------------------------------------------------------------------------------------------
///<summary>
///    Send_PGN_65248_Distances
///
///       Bit   Length    SPN Description          SPN
///       1-4   4 bytes   Trip Distance            244
///       5-8   4 bytes   Total Vehicle Distance   245
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void  Send_PGN_65248_Distances( void )
{
  U8    ucPRI              = 6;
  UL    ulPGN              = 65248;
  UL    ulCANID            = 0;
  U8    ucSRC              = 0;
  U8    ucDST              = 0xFF;
  U8    ucLen              = 8;
  U8    ucData       [ 8 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  UL    ulTripDist         = TOTAL_TRIP_DISTANCE;
  UL    ulTotDist          = TOTAL_VEHICLE_DISTANCE;
  
  //
  // Calculate the CANID.
  //
  
  ulCANID = PGNToCANID( ulPGN, ucPRI, ucSRC, ucDST );
  
  CreateFourByteIntegerLSB( (UL) ulTripDist / 0.125, &ucData[ 0 ] ); // Trip Dist.
  CreateFourByteIntegerLSB( (UL) ulTotDist  / 0.125, &ucData[ 4 ] ); // Total Vehicle Distance.
  
  //
  // Send the message.
  //
  
  CAN0.sendMsgBuf( ulCANID, 1, 8, ucData ); delay( J1939_INTERMESSAGE_DELAY_TIME );
  
  //
  // Print message to Arduino serial port.
  //
  
  PrintJ1939Message( (char *) "Tx", ulCANID, ulPGN, ucSRC, ucDST, ucData, ucLen  );
  
}// Send_PGN_65248_Distances

///----------------------------------------------------------------------------------------------------
///<summary>
///    Send_PGN_65255_TotalHours
///
///       Bit Length  SPN Description           SPN
///       1-4 4 bytes Total Vehicle Hours       246
///       5-8 4 bytes Total Power Takeoff Hours 248
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void  Send_PGN_65255_TotalHours( void )
{
  U8    ucPRI                = 6;
  UL    ulPGN                = 65255;
  UL    ulCANID              = 0;
  U8    ucSRC                = 0;
  U8    ucDST                = 0xFF;
  U8    ucLen                = 8;
  U8    ucData         [ 8 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  UL    ulTotVehicleH        = TOTAL_VEHICLE_HOURS;
  UL    ulTotPTOH            = TOTAL_PTO_HOURS;
  
  //
  // Calculate the CANID.
  //
  
  ulCANID = PGNToCANID( ulPGN, ucPRI, ucSRC, ucDST );
  
  //
  // 0.05 / 0 offset. 
  //

  CreateFourByteIntegerLSB( (UL) ulTotVehicleH / 0.05, &ucData[ 0 ] );
  CreateFourByteIntegerLSB( (UL) ulTotPTOH     / 0.05, &ucData[ 4 ] ); 
  
  //
  // Send the message.
  //

  CAN0.sendMsgBuf( ulCANID, 1, 8, ucData ); delay( J1939_INTERMESSAGE_DELAY_TIME );
  
  //
  // Print message to Arduino serial port.
  //

  PrintJ1939Message( (char *) "Tx", ulCANID, ulPGN, ucSRC, ucDST, ucData, ucLen  );
  
}// Send_PGN_65255_TotalHours

///----------------------------------------------------------------------------------------------------
///<summary>
///    Send_PGN_65267_VehiclePosition
///
///       Bit Length  SPN Description           SPN
///       1-4 4 bytes Latitude                  584
///       5-8 4 bytes Longitude                 585
///
///    GPS = Camp LeJeune, NC - Main Gate - Visitor Center
///    Lat =  34.720769
///    Lon = -77.344933
///
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void  Send_PGN_65267_VehiclePosition( void )
{
  U8    ucPRI              = 6;
  UL    ulPGN              = 65267;
  UL    ulCANID            = 0;
  U8    ucSRC              = 74; // Comm Unit Cellular
  U8    ucDST              = 0xFF;
  U8    ucLen              = 8;
  U8    ucData       [ 8 ] = { 0xE3, 0x6C, 0xDD, 0x91, 0x82, 0x8E, 0x11, 0x4F };
  
  //
  // Calculate the CANID.
  //
  
  ulCANID = PGNToCANID( ulPGN, ucPRI, ucSRC, ucDST );
  
  //
  // Send the message.
  //
  
  CAN0.sendMsgBuf( ulCANID, 1, 8, ucData ); delay( J1939_INTERMESSAGE_DELAY_TIME );
  
  //
  // Print message to Arduino serial port.
  //
  
  PrintJ1939Message( (char *) "Tx", ulCANID, ulPGN, ucSRC, ucDST, ucData, ucLen  );
  
}// Send_PGN_65267_VehiclePosition

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_PGN_65253_EngineHoursRevs
///
///       Bit  Length   SPN Description           SPN
///       1-4  4 bytes  Total Engine Hours        247
///       5-8  4 bytes  Total Engine Revolutions  249
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_PGN_65253_EngineHoursRevs( void )
{
  U8    ucPRI              = 6;
  UL    ulPGN              = 65253;
  UL    ulCANID            = 0;
  U8    ucSRC              = 0;
  U8    ucDST              = 0xFF;
  U8    ucLen              = 8;
  U8    ucData       [ 8 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  UL    ulTotEngH          = TOTAL_ENGINE_HOURS;
  UL    ulTotRev           = TOTAL_ENGINE_REVOLUTIONS;
  
  //
  // Calculate the CANID.
  //
  
  ulCANID = PGNToCANID( ulPGN, ucPRI, ucSRC, ucDST );
  
  CreateFourByteIntegerLSB( (UL) ulTotEngH / 0.05, &ucData[ 0 ] );
  CreateFourByteIntegerLSB( (UL) ulTotRev  / 1000, &ucData[ 4 ] ); 
  
  //
  // Send the message.
  //
  
  CAN0.sendMsgBuf( ulCANID, 1, 8, ucData ); delay( J1939_INTERMESSAGE_DELAY_TIME );
  
  //
  // Print message to Arduino serial port.
  //
  
  PrintJ1939Message( (char *) "Tx", ulCANID, ulPGN, ucSRC, ucDST, ucData, ucLen  );
  
}// Send_PGN_65253_EngineHoursRevs

//------------------------------------------------------------------------------------------------------------------------
// UnPackThreeByteIntegerLSB
//------------------------------------------------------------------------------------------------------------------------
unsigned long UnPackThreeByteIntegerLSB( U8 *ucIntString )
{
  UL ulValue = 0;

  UL ulB2 = ( ucIntString[ 2 ] & 0xFF );  ulB2 <<= 16;
  UL ulB1 = ( ucIntString[ 1 ] & 0xFF );  ulB1 <<=  8;
  UL ulB0 = ( ucIntString[ 0 ] & 0xFF );  ulB0 <<=  0;
  
  ulValue = (UL) ( ulB2 ) | ( ulB1 ) | ( ulB0 );
  
  return ulValue;
}

///----------------------------------------------------------------------------------------------------
///<summary>
///  ReadAnalogDigitalInputs - Arduino provided function that is called continuously.
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void ReadAnalogDigitalInputs( void )
{
  int i = 0;

  if( ucOverrideActive )
  {
    fA0_EngineSpeedPercent    = ( fOverrideEngPct / 100.0 );
    fA2_VehicleSpeedPercent   = ( fOverrideVehPct / 100.0 );
    fA4_OilPressurePercent    = ( fOverrideOilPct / 100.0 );

    usA0_EngineSpeedPosition  = (U16) ( ( fOverrideEngPct / 100.0 ) * 1023 );
    usA2_VehicleSpeedPosition = (U16) ( ( fOverrideVehPct / 100.0 ) * 1023 );
    usA4_OilPressurePosition  = (U16) ( ( fOverrideOilPct / 100.0 ) * 1023 );

    // Serial override simulates a button press (park -> A1, brake -> A3, clutch -> A5).
    ucBtnA1Pressed = ucOverridePark;
    ucBtnA3Pressed = ucOverrideBrake;
    ucBtnA5Pressed = ucOverrideClutch;

    return;
  }

  //----------------------------------------------------------------------------------------------------
  // Sometimes the analog inputs fluctuate a bit.  Get a good average.
  //----------------------------------------------------------------------------------------------------

  UL ulESPTotal = 0;   UL ulVSPTotal = 0;   UL ulOPPTotal = 0;

  for( i = 0; i < 5; i++ )
    {
      ulESPTotal += analogRead( A0_ENGINE_SPEED_POT  );
      ulVSPTotal += analogRead( A2_VEHICLE_SPEED_POT );
      ulOPPTotal += analogRead( A4_OIL_PRESSURE_POT  );
    }
  
  //----------------------------------------------------------------------------------------------------
  // Read the POTs on A0, A2, A4.  Convert from type int to type unsigned short (U16) from 0..1023.
  //----------------------------------------------------------------------------------------------------

  usA0_EngineSpeedPosition  = (U16) ( (float) ulESPTotal / (float) 5.0 );
  usA2_VehicleSpeedPosition = (U16) ( (float) ulVSPTotal / (float) 5.0 );
  usA4_OilPressurePosition  = (U16) ( (float) ulOPPTotal / (float) 5.0 );

  //----------------------------------------------------------------------------------------------------
  // Convert POT readings to a percentage, noting the Mega 2560 max value of 1023.
  //----------------------------------------------------------------------------------------------------

  fA0_EngineSpeedPercent  = (float) ( (float) usA0_EngineSpeedPosition  / (float) 1023.000 );
  fA2_VehicleSpeedPercent = (float) ( (float) usA2_VehicleSpeedPosition / (float) 1023.000 );
  fA4_OilPressurePercent  = (float) ( (float) usA4_OilPressurePosition  / (float) 1023.000 );
  
  //----------------------------------------------------------------------------------------------------
  // If a POT read was weird (less than zero, greater than 1), fix it.  
  //----------------------------------------------------------------------------------------------------

  if( fA0_EngineSpeedPercent  < 0.0 ) fA0_EngineSpeedPercent  = 0.0;  if( fA0_EngineSpeedPercent  > 1.0 ) fA0_EngineSpeedPercent  = 1.0;
  if( fA2_VehicleSpeedPercent < 0.0 ) fA2_VehicleSpeedPercent = 0.0;  if( fA2_VehicleSpeedPercent > 1.0 ) fA2_VehicleSpeedPercent = 1.0;
  if( fA4_OilPressurePercent  < 0.0 ) fA4_OilPressurePercent  = 0.0;  if( fA4_OilPressurePercent  > 1.0 ) fA4_OilPressurePercent  = 1.0;

  //----------------------------------------------------------------------------------------------------
  // Get a real percent number 0..100.
  //----------------------------------------------------------------------------------------------------

  //----------------------------------------------------------------------------------------------------
  // QA FIX: the three POT percents are deliberately left as a 0.00..1.00 FRACTION.
  //
  // Every consumer in this file uses the form "MAX_VALUE * fPercent", the same convention the
  // tradeshow fPercent (0.00..1.00) uses.  The original code multiplied these by 100 first, which
  // made every POT-driven parameter 100x too large.  It then wrapped when packed into two bytes,
  // so engine speed was NON-MONOTONIC: 75% of POT travel decoded LOWER than 25%.  Leaving these
  // as a fraction is what the send functions expect.  Display code multiplies by 100 itself.
  //----------------------------------------------------------------------------------------------------
  
  //----------------------------------------------------------------------------------------------------
  // Read the Switches on A1, A3, A5.  They are held up internally, so LOW means the button was pressed.
  //----------------------------------------------------------------------------------------------------

  if( LOW == digitalRead( A1_PARK_BRAKE_SWITCH   ) ) ucBtnA1Pressed = ON; else ucBtnA1Pressed = OFF;
  if( LOW == digitalRead( A3_BRAKE_PEDAL_SWITCH  ) ) ucBtnA3Pressed = ON; else ucBtnA3Pressed = OFF;
  if( LOW == digitalRead( A5_CLUTCH_PEDAL_SWITCH ) ) ucBtnA5Pressed = ON; else ucBtnA5Pressed = OFF;

}//ReadAnalogDigitalInputs

///----------------------------------------------------------------------------------------------------
///<summary>
///   Send_Button_DTC - Send ONE J1939 DM1 (PGN 65226) single frame carrying one DTC.
///
///   Byte 0 lamp status, byte 1 flash lamp (0xFF = not flashing), bytes 2-3 SPN LSB/middle,
///   byte 4 = SPN top 3 bits (7-5) | FMI (4-0), byte 5 = conversion method 0 | occurrence count,
///   bytes 6-7 = 0xFF.
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_Button_DTC( UL ulSPN, U8 ucFMI, U8 ucOC )
{
  U8 ucPRI          = 6;
  UL ulPGN          = 65226;
  U8 ucSRC          = 0;
  U8 ucDST          = 0xFF;
  UL ulCANID        = PGNToCANID( ulPGN, ucPRI, ucSRC, ucDST );
  U8 ucData   [ 8 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

  ucData[ 0 ] = ucBtnDTCLamp;
  ucData[ 1 ] = 0xFF;
  ucData[ 2 ] = (U8) (   ulSPN         & 0xFF );
  ucData[ 3 ] = (U8) ( ( ulSPN >>  8 ) & 0xFF );
  ucData[ 4 ] = (U8) ( ( ( ( ulSPN >> 16 ) & 0x07 ) << 5 ) | ( ucFMI & 0x1F ) );
  ucData[ 5 ] = (U8) ( ucOC & 0x7F );

  CAN0.sendMsgBuf( ulCANID, 1, 8, ucData ); delay( J1939_INTERMESSAGE_DELAY_TIME );

  PrintJ1939Message( (char *) "Tx", ulCANID, ulPGN, ucSRC, ucDST, ucData, 8 );

}//Send_Button_DTC

///----------------------------------------------------------------------------------------------------
///<summary>
///   CheckButtonDTCs - On each OFF -> ON edge of a button send one DTC.  Holding a button down or
///   releasing it sends nothing.  The occurrence count (1..127) increments per press of that button.
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void CheckButtonDTCs( void )
{
  if( ON == ucBtnA1Pressed && OFF == ucBtnA1Prev )
    {
      if( ucBtnA1Count < 127 ) ucBtnA1Count++;
      Send_Button_DTC( BTN_A1_SPN, BTN_A1_FMI, ucBtnA1Count );
    }
  if( ON == ucBtnA3Pressed && OFF == ucBtnA3Prev )
    {
      if( ucBtnA3Count < 127 ) ucBtnA3Count++;
      Send_Button_DTC( BTN_A3_SPN, BTN_A3_FMI, ucBtnA3Count );
    }
  if( ON == ucBtnA5Pressed && OFF == ucBtnA5Prev )
    {
      if( ucBtnA5Count < 127 ) ucBtnA5Count++;
      Send_Button_DTC( BTN_A5_SPN, BTN_A5_FMI, ucBtnA5Count );
    }

  ucBtnA1Prev = ucBtnA1Pressed;
  ucBtnA3Prev = ucBtnA3Pressed;
  ucBtnA5Prev = ucBtnA5Pressed;

}//CheckButtonDTCs

///----------------------------------------------------------------------------------------------------
///<summary>
///
///   Send_Proprietary_PGN_65280_PID_254_ECMSimulatorSwitchStatus
///
///   Byte  Bits  Length    Parameter                         BitRes  Range      Offset
///   0-1         2 Bytes   A0 - Engine Speed Potentiometer   1.0     0 - 1023   0.0
///   2-3         2 Bytes   A2 - Vehicle Speed Potentiometer  1.0     0 - 1023   0.0
///   4-5         2 Bytes   A4 - Oil Pressure Potentiometer   1.0     0 - 1023   0.0
///   6     0     1 Bit     A1 - Park Brake Switch            1.0     0 - 1      0.0           
///   6     1     1 Bit     A3 - Brake Pedal Switch           1.0     0 - 1      0.0           
///   6     2     1 Bit     A5 - Clutch Pedal Switch          1.0     0 - 1      0.0           
///   7           1 Byte    Reserved, set to FF
///
///</summary>
///----------------------------------------------------------------------------------------------------
/*PROCEDURE*/ void Send_Proprietary_PGN_65280_PID_254_ECMSimulatorSwitchStatus( void )
{
  U8 ucPRI                 = 6;
  UL ulPGN                 = 65280;
  UL ulCANID               = 0;
  U8 ucSRC                 = 0;
  U8 ucDST                 = 0xFF;
  U8 ucLen                 = 8;
  U8 ucBytesWritten        = 0;
  UL ulEngineSpeed         = 0;

  //
  // The J1939 message.
  //
  
  U8 ucJ1939Msg     [  8 ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

  //
  // The J1587 message.
  //                            MID   PID    A0          A2          A4          SW    N/A  CSUM
  //                              0     1     2     3     4     5     6     7     8      9    10
  U8 ucJ1587Msg     [ 11 ] = {  128,  254, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  0xFF, 0xFF };
  
  //
  // Calculate the CANID.
  //
  
  ulCANID = PGNToCANID( ulPGN, ucPRI, ucSRC, ucDST );
  
  //
  // Put the POTs where they are supposed to go, J1939.
  //
  
  CreateTwoByteIntegerLSB( usA0_EngineSpeedPosition  , &ucJ1939Msg[ 0 ] );
  CreateTwoByteIntegerLSB( usA2_VehicleSpeedPosition , &ucJ1939Msg[ 2 ] );
  CreateTwoByteIntegerLSB( usA4_OilPressurePosition  , &ucJ1939Msg[ 4 ] );
  
  //
  // Put the POTs where they are supposed to go, J1587.
  //
  
  CreateTwoByteIntegerLSB( usA0_EngineSpeedPosition  , &ucJ1587Msg[ 2 ] );
  CreateTwoByteIntegerLSB( usA2_VehicleSpeedPosition , &ucJ1587Msg[ 4 ] );
  CreateTwoByteIntegerLSB( usA4_OilPressurePosition  , &ucJ1587Msg[ 6 ] );
  
  //
  // Start the switches off at 0xFF.  J1939 is index 6, J1587 is index 8.
  //
  //              1111 1000
  ucJ1939Msg[ 6 ] = 0xF8;
  ucJ1587Msg[ 8 ] = 0xF8;

  if( ON == ucBtnA1Pressed ) { ucJ1939Msg[ 6 ] |= 0x01; ucJ1587Msg[ 8 ] |= 0x01; }
  if( ON == ucBtnA3Pressed ) { ucJ1939Msg[ 6 ] |= 0x02; ucJ1587Msg[ 8 ] |= 0x02; }
  if( ON == ucBtnA5Pressed ) { ucJ1939Msg[ 6 ] |= 0x04; ucJ1587Msg[ 8 ] |= 0x04; }
  
  //
  // Default the last byte to 0xFF.  J1939 byte 7, J1587 byte 9.
  //
  
  ucJ1939Msg[ 7 ] = 0xFF;
  ucJ1587Msg[ 9 ] = 0xFF;
  
  //
  // Send the J1939 message.
  //
  
  CAN0.sendMsgBuf( ulCANID, 1, 8, ucJ1939Msg ); delay( J1939_INTERMESSAGE_DELAY_TIME );
  
  //
  // Print message to Arduino serial port.
  //
  
  PrintJ1939Message( (char *) "Tx", ulCANID, ulPGN, ucSRC, ucDST, ucJ1939Msg, ucLen  );

  //****************************************************************************************************
  // J1587 Message
  //
  // Calculate the checksum for the message.
  //
  
  ucJ1587Msg[ 10 ] = CalculateJ1587Checksum( ucJ1587Msg, 10 );
  
  //
  // Turn the RS485 pin to LOW to enable transmission.
  //
  
  digitalWrite( RS485_RXTXPIN, LOW ); 
  
  //
  // Send the J1708/J1587 message.
  //
  
  ucBytesWritten = Serial1.write( ucJ1587Msg, 11 );
  
  if( 11 != ucBytesWritten )
    {
      Serial.print ( "Error writing to the serial port.\r\n" ); 
    }
  else
    {
      PrintJ1587Message( (char *) "Tx", ucJ1587Msg, 11 );
    }
  
  //
  // Turn the RS485 pin to HIGH to disable transmission (enable reception).
  //
  
  digitalWrite( RS485_RXTXPIN, HIGH );
  
  //
  // Delay to keep the UART from getting backed up.
  //
  
  delay( 15 * BYTE_DELAY_J1587 );
  
}//Send_Proprietary_PGN_65280_PID_254_ECMSimulatorSwitchStatus

//====================================================================================================
// Copyright (C) 2018-2021, Diesel Laptops, LLC -  All Rights Reserved.
//====================================================================================================
