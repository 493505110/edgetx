/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "opentx.h"
#include "flysky_ibus.h"

#if defined(PCBNV14)
extern uint32_t NV14internalModuleFwVersion;
#endif

/*
 *  TXID + RXID are already skipped in MULTI module to save memory+transmission time, format from Multi is:
 *  AA or AC | TX_RSSI | sensor ...
 */


#define FLYSKY_TELEMETRY_LENGTH (2+7*4)        // Should it be 2+7*6???
#define ALT_PRECISION 15
#define R_DIV_G_MUL_10_Q15 (uint64_t)9591506
#define INV_LOG2_E_Q1DOT31 (uint64_t)0x58b90bfc // Inverse log base 2 of e
#define PRESSURE_MASK 0x7FFFF

struct FlySkySensor
{
  const uint16_t id;
  const char * name;
  const TelemetryUnit unit;
  const uint8_t precision;
};

// telemetry sensors ID
enum
{
  AFHDS2A_ID_VOLTAGE = 0x00,    // Internal Voltage
  AFHDS2A_ID_TEMPERATURE = 0x01,    // Temperature
  AFHDS2A_ID_MOT = 0x02,    // RPM
  AFHDS2A_ID_EXTV = 0x03,    // External Voltage
  AFHDS2A_ID_CELL_VOLTAGE = 0x04,    // Avg Cell voltage
  AFHDS2A_ID_BAT_CURR = 0x05,    // battery current A * 100
  AFHDS2A_ID_FUEL = 0x06,    // remaining battery percentage / mah drawn otherwise or fuel level no unit!
  AFHDS2A_ID_RPM = 0x07,    // throttle value / battery capacity
  AFHDS2A_ID_CMP_HEAD = 0x08,    // Heading  0..360 deg, 0=north 2bytes
  AFHDS2A_ID_CLIMB_RATE = 0x09,    // 2 bytes m/s *100 signed
  AFHDS2A_ID_COG = 0x0A,    // 2 bytes  Course over ground(NOT heading, but direction of movement) in degrees * 100, 0.0..359.99 degrees. unknown max uint
  AFHDS2A_ID_GPS_STATUS = 0x0B,    // 2 bytes
  AFHDS2A_ID_ACC_X = 0x0C,    // 2 bytes m/s *100 signed
  AFHDS2A_ID_ACC_Y = 0x0D,    // 2 bytes m/s *100 signed
  AFHDS2A_ID_ACC_Z = 0x0E,    // 2 bytes m/s *100 signed
  AFHDS2A_ID_ROLL = 0x0F,    // 2 bytes deg *100 signed
  AFHDS2A_ID_PITCH = 0x10,    // 2 bytes deg *100 signed
  AFHDS2A_ID_YAW = 0x11,    // 2 bytes deg *100 signed
  AFHDS2A_ID_VERTICAL_SPEED = 0x12,    // 2 bytes m/s *100 signed
  AFHDS2A_ID_GROUND_SPEED = 0x13,    // 2 bytes m/s *100 different unit than build-in sensor
  AFHDS2A_ID_GPS_DIST = 0x14,    // 2 bytes distance from home m unsigned
  AFHDS2A_ID_ARMED = 0x15,    // 2 bytes
  AFHDS2A_ID_FLIGHT_MODE = 0x16,    // 2 bytes
  AFHDS3_FRM_TEMP           = 0x57,    //virtual
  AFHDS3_FRM_EXT_V          = 0x58,    //virtual
  AFHDS2A_ID_PRES = 0x41,    // Pressure
  AFHDS2A_ID_ODO1 = 0x7C,    // Odometer1
  AFHDS2A_ID_ODO2 = 0x7D,    // Odometer2
  AFHDS2A_ID_SPE = 0x7E,    // Speed 2 bytes km/h
  AFHDS2A_ID_TX_V = 0x7F,    // TX Voltage

  AFHDS2A_ID_GPS_LAT = 0x80,    // 4bytes signed WGS84 in degrees * 1E7
  AFHDS2A_ID_GPS_LON = 0x81,    // 4bytes signed WGS84 in degrees * 1E7
  AFHDS2A_ID_GPS_ALT = 0x82,    // 4bytes signed!!! GPS alt m*100
  AFHDS2A_ID_ALT = 0x83,    // 4bytes signed!!! Alt m*100
  AFHDS2A_ID_S84 = 0x84,
  AFHDS2A_ID_S85 = 0x85,
  AFHDS2A_ID_S86 = 0x86,
  AFHDS2A_ID_S87 = 0x87,
  AFHDS2A_ID_S88 = 0x88,
  AFHDS2A_ID_S89 = 0x89,
  AFHDS2A_ID_S8a = 0x8A,
  AFHDS2A_ID_RX_SIG_AFHDS3  = 0xF7,    // SIG
  AFHDS2A_ID_RX_SNR_AFHDS3  = 0xF8,    // SNR
  AFHDS2A_ID_ALT_FLYSKY     = 0xF9,    // Altitude 2 bytes signed in m - used in FlySky native TX
  AFHDS2A_ID_RX_SNR = 0xFA,    // SNR
  AFHDS2A_ID_RX_NOISE = 0xFB,    // Noise
  AFHDS2A_ID_RX_RSSI = 0xFC,    // RSSI
  AFHDS2A_ID_RX_ERR_RATE = 0xFE,    // Error rate
  AFHDS2A_ID_END = 0xFF,

  // AC type telemetry with multiple values in one packet
  AFHDS2A_ID_GPS_FULL = 0xFD,
  AFHDS2A_ID_VOLT_FULL = 0xF0,
  AFHDS2A_ID_ACC_FULL = 0xEF,
  AFHDS2A_ID_TX_RSSI = 0x200,     // Pseudo id outside 1 byte range of FlySky sensors
};

const FlySkySensor flySkySensors[] = {
  {AFHDS2A_ID_VOLTAGE | 0x100, STR_SENSOR_A1,          UNIT_VOLTS,             2},  // RX Voltage (remapped, really 0x0)
  {AFHDS2A_ID_TEMPERATURE,     STR_SENSOR_TEMP1,       UNIT_CELSIUS,           1},  // Temperature
  {AFHDS2A_ID_MOT,             STR_SENSOR_RPM,         UNIT_RAW,               0},  // RPM
  {AFHDS2A_ID_EXTV,            STR_SENSOR_A3,          UNIT_VOLTS,             2},  // External voltage
  {AFHDS2A_ID_CELL_VOLTAGE,    STR_SENSOR_CELLS,       UNIT_VOLTS,             2},  // Avg Cell voltage
  {AFHDS2A_ID_BAT_CURR,        STR_SENSOR_CURR,        UNIT_AMPS,              2},  // battery current A * 100
  {AFHDS2A_ID_FUEL,            STR_SENSOR_CAPACITY,    UNIT_RAW,               0},  // remaining battery percentage / mah drawn otherwise or fuel level no unit!
  {AFHDS2A_ID_RPM,             STR_SENSOR_RPM,         UNIT_RAW,               0},  // throttle value / battery capacity
  {AFHDS2A_ID_CMP_HEAD,        STR_SENSOR_HDG,         UNIT_DEGREE,            0},  // Heading  0..360 deg, 0=north 2bytes
  {AFHDS2A_ID_CLIMB_RATE,      STR_SENSOR_VSPD,        UNIT_METERS_PER_SECOND, 2},  // 2 bytes m/s *100
  {AFHDS2A_ID_COG,             STR_SENSOR_HDG,         UNIT_DEGREE,            2},  // 2 bytes  Course over ground(NOT heading, but direction of movement) in degrees * 100, 0.0..359.99 degrees. unknown max uint
  {AFHDS2A_ID_GPS_STATUS,      STR_SENSOR_SATELLITES,  UNIT_RAW,               0},  // 2 bytes
  {AFHDS2A_ID_ACC_X,           STR_SENSOR_ACCX,        UNIT_METERS_PER_SECOND, 2},  // 2 bytes m/s *100 signed
  {AFHDS2A_ID_ACC_Y,           STR_SENSOR_ACCY,        UNIT_METERS_PER_SECOND, 2},  // 2 bytes m/s *100 signed
  {AFHDS2A_ID_ACC_Z,           STR_SENSOR_ACCZ,        UNIT_METERS_PER_SECOND, 2},  // 2 bytes m/s *100 signed
  {AFHDS2A_ID_ROLL,            STR_SENSOR_ROLL,        UNIT_DEGREE,            2},  // 2 bytes deg *100 signed
  {AFHDS2A_ID_PITCH,           STR_SENSOR_PITCH,       UNIT_DEGREE,            2},  // 2 bytes deg *100 signed
  {AFHDS2A_ID_YAW,             STR_SENSOR_YAW,         UNIT_DEGREE,            2},  // 2 bytes deg *100 signed
  {AFHDS2A_ID_VERTICAL_SPEED,  STR_SENSOR_VSPD,        UNIT_METERS_PER_SECOND, 2},  // 2 bytes m/s *100
  {AFHDS2A_ID_GROUND_SPEED,    STR_SENSOR_GSPD,        UNIT_METERS_PER_SECOND, 2},  // 2 bytes m/s *100 different unit than build-in sensor
  {AFHDS2A_ID_GPS_DIST, STR_SENSOR_DIST,               UNIT_METERS,            0},  // 2 bytes dist from home m unsigned
  {AFHDS2A_ID_ARMED,    STR_SENSOR_ARM,                UNIT_RAW,               0},  // 2 bytes
  {AFHDS2A_ID_FLIGHT_MODE,     STR_SENSOR_FLIGHT_MODE, UNIT_RAW,               0},  // 2 bytes index
  {AFHDS2A_ID_PRES,     STR_SENSOR_PRES,               UNIT_RAW,               2},  // 4 bytes In fact Temperature + Pressure -> Altitude
  {AFHDS2A_ID_PRES | 0x100,    STR_SENSOR_TEMP2,       UNIT_CELSIUS,           1},  // 2 bytes Temperature
  {AFHDS3_FRM_TEMP,             STR_TEMP2,             UNIT_CELSIUS,                1},  // 2 bytes temperature
  {AFHDS3_FRM_EXT_V,            STR_TXV,               UNIT_VOLTS,                  2},  // 2 bytes voltage
  {AFHDS2A_ID_ODO1,     STR_SENSOR_ODO1,               UNIT_METERS,            2},  // 2 bytes Odometer1 -- some magic with 330 needed
  {AFHDS2A_ID_ODO2,     STR_SENSOR_ODO2,               UNIT_METERS,            2},  // 2 bytes Odometer2 -- some magic with 330 needed
  {AFHDS2A_ID_SPE,             STR_SENSOR_ASPD,        UNIT_KMH,               2},  // 2 bytes Speed km/h -- some magic with 330 needed
  {AFHDS2A_ID_TX_V,     STR_SENSOR_TXV,                UNIT_VOLTS,             2},  // TX Voltage
  {AFHDS2A_ID_GPS_LAT,         STR_SENSOR_GPS,         UNIT_RAW,               7},  // 4 bytes signed WGS84 in degrees * 1E7
  {AFHDS2A_ID_GPS_LON,         STR_SENSOR_GPS,         UNIT_RAW,               7},  // 4 bytes signed WGS84 in degrees * 1E7
  {AFHDS2A_ID_GPS_ALT,         STR_SENSOR_GPSALT,      UNIT_METERS,            2},  // 4 bytes signed GPS alt m*100
  {AFHDS2A_ID_ALT,             STR_SENSOR_ALT,         UNIT_METERS,            2},  // 4 bytes signed Alt m*100
  {AFHDS2A_ID_RX_SIG_AFHDS3,    STR_RX_QUALITY,        UNIT_RAW,                    0},  // RX error rate
  {AFHDS2A_ID_RX_SNR_AFHDS3,    STR_RX_SNR,            UNIT_DB,                     1},  // RX SNR
  {AFHDS2A_ID_RX_SNR,          STR_SENSOR_RX_SNR,      UNIT_DB,                0},  // RX SNR
  {AFHDS2A_ID_RX_NOISE,        STR_SENSOR_RX_NOISE,    UNIT_DB,                0},  // RX Noise
  {AFHDS2A_ID_RX_RSSI,         STR_SENSOR_RSSI,        UNIT_DB,                0},  // RX RSSI (0xfc)
  {AFHDS2A_ID_RX_ERR_RATE,     STR_SENSOR_RX_QUALITY,  UNIT_RAW,               0},  // RX error rate
  {AFHDS2A_ID_TX_RSSI,         STR_SENSOR_TX_RSSI,     UNIT_RAW,               0},  // Pseudo sensor for TRSSI

  {0x00,                       NULL,             UNIT_RAW,               0},  // sentinel
};

int32_t getALT(uint32_t value);

void processFlySkySensor(const uint8_t * packet, uint8_t type)
{
  uint8_t buffer[8];
  uint16_t id = packet[0];
  const uint8_t instance = packet[1];
  int32_t value;

  //Load most likely value
  if (type == 0xAA)
    value = (packet[3] << 8) | packet[2];
  else
    value = (packet[6] << 24) | (packet[5] << 16) | (packet[4] << 8) | packet[3];

  if (id == 0) id = 0x100;   // Some part of OpenTX does not like sensor with id and instance 0, remap to 0x100

  if (id == AFHDS2A_ID_RX_NOISE || id == AFHDS2A_ID_RX_RSSI) {
    value  = 135 - value;
  }
  else if (id == AFHDS2A_ID_RX_ERR_RATE) {
    value = 100 - value;
    telemetryData.rssi.set(value);
    if (value > 0) telemetryStreaming = TELEMETRY_TIMEOUT10ms;
  }
  else if(id == AFHDS2A_ID_RX_SIG_AFHDS3) {
    telemetryData.rssi.set(value);
    if(value>0) telemetryStreaming = TELEMETRY_TIMEOUT10ms;
  }
  else if (id == AFHDS2A_ID_PRES && value) {
    // Extract temperature to a new sensor
    setTelemetryValue(PROTOCOL_TELEMETRY_FLYSKY_IBUS, id | 0x100, 0, instance, ((value >> 19) - 400), UNIT_CELSIUS, 1);
    // Extract alt to a new sensor
    setTelemetryValue(PROTOCOL_TELEMETRY_FLYSKY_IBUS, AFHDS2A_ID_ALT, 0, instance, getALT(value), UNIT_METERS, 2);
    value &= PRESSURE_MASK;
  }
  else if ((id >= AFHDS2A_ID_ACC_X && id <= AFHDS2A_ID_VERTICAL_SPEED) || id == AFHDS2A_ID_CLIMB_RATE || id == AFHDS2A_ID_ALT_FLYSKY) {
    value = (int16_t) value; // Signed value
  }
  else if (id == AFHDS2A_ID_GPS_STATUS) {
    value = value >> 8;
  }
  else if (id == AFHDS2A_ID_GPS_FULL) {
    //(AC FRAME)[ID][inst][size][fix][sats][LAT]x4[LON]x4[ALT]x4
    setTelemetryValue(PROTOCOL_TELEMETRY_FLYSKY_IBUS, AFHDS2A_ID_GPS_STATUS, 0, instance, packet[4], UNIT_RAW, 0);
    for (uint8_t sensorID = AFHDS2A_ID_GPS_LAT; sensorID <= AFHDS2A_ID_GPS_ALT; sensorID++) {
      int index = 5 + (sensorID - AFHDS2A_ID_GPS_LAT) * 4;
      buffer[0] = sensorID;
      buffer[1] = instance;
      buffer[2] = 4;
      memcpy(buffer + 3, packet + index, 4);
      processFlySkySensor(buffer, 0xAC);
    }
    return;
  }
  else if (id == AFHDS2A_ID_VOLT_FULL) {
    //(AC FRAME)[ID][inst][size][ACC_X]x2[ACC_Y]x2[ACC_Z]x2[ROLL]x2[PITCH]x2[YAW]x2
    for (uint8_t sensorID = AFHDS2A_ID_EXTV; sensorID <= AFHDS2A_ID_RPM; sensorID++) {
      int index = 3 + (sensorID - AFHDS2A_ID_EXTV) * 2;
      buffer[0] = sensorID;
      buffer[1] = instance;
      buffer[2] = packet[index];
      buffer[3] = packet[index + 1];
      processFlySkySensor(buffer, 0xAA);
    }
    return;
  }
  else if (id == AFHDS2A_ID_ACC_FULL) {
    //(AC FRAME)[ID][inst][size]
    for (uint8_t sensorID = AFHDS2A_ID_ACC_X; sensorID <= AFHDS2A_ID_YAW; sensorID++) {
      int index = 3 + (sensorID - AFHDS2A_ID_ACC_X) * 2;
      buffer[0] = sensorID;
      buffer[1] = instance;
      buffer[2] = packet[index];
      buffer[3] = packet[index + 1];
      processFlySkySensor(buffer, 0xAA);
    }
    return;
  }
  for (const FlySkySensor * sensor = flySkySensors; sensor->id; sensor++) {
    if (sensor->id != id) continue;
    if (sensor->unit == UNIT_CELSIUS) value -= 400; // Temperature sensors have 40 degree offset
    else if (sensor->unit == UNIT_VOLTS) value = (int16_t) value; // Voltage types are unsigned 16bit integers
    setTelemetryValue(PROTOCOL_TELEMETRY_FLYSKY_IBUS, id, 0, instance, value, sensor->unit, sensor->precision);
    return;
  }
  //unknown
  setTelemetryValue(PROTOCOL_TELEMETRY_FLYSKY_IBUS, id, 0, instance, value, UNIT_RAW, 0);
}

void processFlySkyPacket(const uint8_t * packet)
{
  // Set TX RSSI Value, reverse MULTIs scaling
  setTelemetryValue(PROTOCOL_TELEMETRY_FLYSKY_IBUS, AFHDS2A_ID_TX_RSSI, 0, 0, packet[0], UNIT_RAW, 0);

  const uint8_t * buffer = packet + 1;
  int sesnor = 0;
  while (sesnor++ < 7) {
    if (*buffer == AFHDS2A_ID_END) break;
    processFlySkySensor(buffer, 0xAA);
    buffer += 4;
  }
}

void processFlySkyPacketAC(const uint8_t * packet)
{
  // Set TX RSSI Value, reverse MULTIs scaling
  setTelemetryValue(PROTOCOL_TELEMETRY_FLYSKY_IBUS, AFHDS2A_ID_TX_RSSI, 0, 0, packet[0], UNIT_RAW, 0);
  const uint8_t * buffer = packet + 1;
  while (buffer - packet < 26) //28 + 1(multi TX rssi) - 3(ac header)
  {
    if (*buffer == AFHDS2A_ID_END) break;
    uint8_t size = buffer[2];
    processFlySkySensor(buffer, 0xAC);
    buffer += size + 3;
  }
}

void processFlySkyTelemetryData(uint8_t data, uint8_t * rxBuffer, uint8_t &rxBufferCount)
{
  if (rxBufferCount == 0)
    return;

  if (data == 0xAA || data == 0xAC) {
    TRACE("[IBUS] Packet 0x%02X", data);
  }
  else {
    TRACE("[IBUS] invalid start byte 0x%02X", data);
    rxBufferCount = 0;
    return;
  }

  if (rxBufferCount < TELEMETRY_RX_PACKET_SIZE) {
    rxBuffer[rxBufferCount++] = data;
  }
  else {
    TRACE("[IBUS] array size %d error", rxBufferCount);
    rxBufferCount = 0;
  }

  if (rxBufferCount >= FLYSKY_TELEMETRY_LENGTH) {
    // debug print the content of the packets
#if 0
    debugPrintf(", rssi 0x%02X: ", rxBuffer[1]);
    for (int i=0; i<7; i++) {
      debugPrintf("[%02X %02X %02X%02X] ", rxBuffer[i*4+2], rxBuffer[i*4 + 3],
                  rxBuffer[i*4 + 4], rxBuffer[i*4 + 5]);
    }
    debugPrintf(CRLF);
#endif
    if (data == 0xAA) processFlySkyPacket(rxBuffer + 1);
    else if (data == 0xAC) processFlySkyPacketAC(rxBuffer + 1);
    rxBufferCount = 0;
  }
}

const FlySkySensor * getFlySkySensor(uint16_t id)
{
  for (const FlySkySensor * sensor = flySkySensors; sensor->id; sensor++) {
    if (id == sensor->id)
      return sensor;
  }
  return nullptr;
}

void flySkySetDefault(int index, uint16_t id, uint8_t subId, uint8_t instance)
{
  TelemetrySensor &telemetrySensor = g_model.telemetrySensors[index];
  telemetrySensor.id = id;
  telemetrySensor.subId = subId;
  telemetrySensor.instance = instance;

  const FlySkySensor * sensor = getFlySkySensor(id);
  if (sensor) {
    TelemetryUnit unit = sensor->unit;
    uint8_t prec = min<uint8_t>(2, sensor->precision);
    telemetrySensor.init(sensor->name, unit, prec);

    if (unit == UNIT_RPMS) {
      telemetrySensor.custom.ratio = 1;
      telemetrySensor.custom.offset = 1;
    }
  }
  else {
    telemetrySensor.init(id);
  }

  storageDirty(EE_MODEL);
}

uint16_t ibusTempToK(int16_t tempertureIbus)
{
  return (uint16_t) (tempertureIbus - 400) + 2731;
}

int32_t log2fix(uint32_t x)
{
  int32_t b = 1U << (ALT_PRECISION - 1);
  int32_t y = 0;
  while (x < 1U << ALT_PRECISION) {
    x <<= 1;
    y -= 1U << ALT_PRECISION;
  }

  while (x >= 2U << ALT_PRECISION) {
    x >>= 1;
    y += 1U << ALT_PRECISION;
  }

  uint64_t z = x;
  for (size_t i = 0; i < ALT_PRECISION; i++) {
    z = (z * z) >> ALT_PRECISION;
    if (z >= 2U << ALT_PRECISION) {
      z >>= 1;
      y += b;
    }
    b >>= 1;
  }
  return y;
}

int32_t getALT(uint32_t value)
{
  uint32_t pressurePa = value & PRESSURE_MASK;
  if (pressurePa == 0) return 0;
  uint16_t temperatureK = ibusTempToK((uint16_t) (value >> 19));
  static uint32_t initPressure = 0;
  static uint16_t initTemperature = 0;
  if (initPressure <= 0) // use current pressure for ground altitude -> 0
  {
    initPressure = pressurePa;
    initTemperature = temperatureK;
  }
  int temperature = (initTemperature + temperatureK) >> 1; //div 2
  bool tempNegative = temperature < 0;
  if (tempNegative) temperature = temperature * -1;
  uint64_t helper = R_DIV_G_MUL_10_Q15;
  helper = helper * (uint64_t) temperature;
  helper = helper >> ALT_PRECISION;

  uint32_t po_to_p = (uint32_t)(initPressure << (ALT_PRECISION - 1));
  po_to_p = po_to_p / pressurePa;
  //shift missing bit
  po_to_p = po_to_p << 1;
  if (po_to_p == 0) return 0;
  uint64_t t = log2fix(po_to_p) * INV_LOG2_E_Q1DOT31;
  int32_t ln = t >> 31;

  bool neg = ln < 0;
  if (neg) ln = ln * -1;
  helper = helper * (uint64_t) ln;
  helper = helper >> ALT_PRECISION;
  int result = (int) helper;

  if (neg ^ tempNegative) result = result * -1;
  return result;
}

#if defined(AFHDS2)

#define FLYSKY_FIXED_RX_VOLTAGE (uint8_t)(FLYSKY_SENSOR_RX_VOLTAGE + (uint8_t)0xA0)

#define MIN_SNR 8
#define MAX_SNR 45

#define FIXED_PRECISION 15
#define FIXED(val) (val << FIXED_PRECISION)
#define DECIMAL(val) (val >> FIXED_PRECISION)
#define PRESSURE_MASK 0x7FFFF
#define V_SPEED_AVERAGING_TIME_10ms  2

struct FlyskyNv14Sensor {
  const uint16_t id;
  const uint8_t subId;
  const char * name;
  const TelemetryUnit unit;
  const uint8_t precision;
  const uint8_t offset;
  const uint8_t bytes;
  const bool issigned;
};

union nv14SensorData {
  uint8_t UINT8;
  uint16_t UINT16;
  int16_t INT16;
  uint32_t UINT32;
};

FlyskyNv14Sensor defaultNv14Sensor = {0, 0, "UNKNOWN", UNIT_RAW, 0, 0, 2, false};

const FlyskyNv14Sensor Nv14Sensor[]=
{
    {FLYSKY_FIXED_RX_VOLTAGE,  0, STR_SENSOR_A1,          UNIT_VOLTS,         2, 0, 2, false},
    {FLYSKY_SENSOR_RX_SIGNAL,  0, STR_SENSOR_RX_SIGNAL,   UNIT_RAW,           0, 0, 2, false},
    {FLYSKY_SENSOR_RX_RSSI,    0, STR_SENSOR_RSSI,        UNIT_DB,            0, 0, 2, true,},
    {FLYSKY_SENSOR_RX_NOISE,   0, STR_SENSOR_RX_NOISE,    UNIT_DB,            0, 0, 2, true},
    {FLYSKY_SENSOR_RX_SNR,     0, STR_SENSOR_RX_SNR,      UNIT_DB,            0, 0, 2, false},
    {FLYSKY_SENSOR_RX_SNR,     1, STR_SENSOR_RX_QUALITY,  UNIT_PERCENT,       0, 0, 2, false},
    {FLYSKY_SENSOR_TEMP,       0, STR_SENSOR_TEMP1,       UNIT_CELSIUS,       1, 0, 2, true},
    {FLYSKY_SENSOR_EXT_VOLTAGE,0, STR_SENSOR_A3,          UNIT_VOLTS,         2, 0, 2, false},
    {FLYSKY_SENSOR_MOTO_RPM,   0, STR_SENSOR_RPM,         UNIT_RPMS,          0, 0, 2, false},
    {FLYSKY_SENSOR_PRESSURE,   0, STR_SENSOR_PRES,        UNIT_RAW,           1, 0, 2, false},
    {FLYSKY_SENSOR_PRESSURE,   1, STR_SENSOR_ALT,         UNIT_METERS,        2, 0, 2, true},
//    {FLYSKY_SENSOR_PRESSURE,   2, STR_SENSOR_TEMP2,       UNIT_CELSIUS,       1, 0, 4, true},
    {FLYSKY_SENSOR_PRESSURE,   3, STR_SENSOR_VSPD,        UNIT_METERS_PER_SECOND,  2, 0, 2, true},   
    {FLYSKY_SENSOR_GPS,        1, STR_SENSOR_SATELLITES,  UNIT_RAW,           0, 0, 1, false},
    {FLYSKY_SENSOR_GPS,        2, STR_SENSOR_GPS,         UNIT_GPS_LATITUDE,  0, 1, 4, true},
    {FLYSKY_SENSOR_GPS,        3, STR_SENSOR_GPS,         UNIT_GPS_LONGITUDE, 0, 5, 4, true},
    {FLYSKY_SENSOR_GPS,        4, STR_SENSOR_ALT,         UNIT_METERS,        0, 8, 2, true},
    {FLYSKY_SENSOR_GPS,        5, STR_SENSOR_GSPD,        UNIT_KMH,           1, 10, 2, false},
    {FLYSKY_SENSOR_GPS,        6, STR_SENSOR_HDG,         UNIT_DEGREE,        3, 12, 2, false},
//    {FLYSKY_SENSOR_SYNC,       0, "Sync",                 UNIT_RAW,           0, 0,  2, false},
    defaultNv14Sensor
};

const FlyskyNv14Sensor* getFlyskyNv14Sensor(uint16_t id, uint8_t subId)
{
  for (const FlyskyNv14Sensor* sensor = Nv14Sensor; sensor->id; sensor++) {
    if (sensor->id == id && sensor->subId == subId) {
      return sensor;
    }
  }
  return &defaultNv14Sensor;
}

void flySkyNv14SetDefault(int index, uint8_t id, uint8_t subId,
                          uint8_t instance)
{
  TelemetrySensor& telemetrySensor = g_model.telemetrySensors[index];
  telemetrySensor.id = id;
  telemetrySensor.subId = subId;
  telemetrySensor.instance = instance;
  const FlyskyNv14Sensor* sensor = getFlyskyNv14Sensor(id, subId);
  telemetrySensor.init(sensor->name, sensor->unit, sensor->precision);
  if (sensor->unit == UNIT_RPMS) {
    telemetrySensor.custom.ratio = 1;
    telemetrySensor.custom.offset = 1;
  }
  storageDirty(EE_MODEL);
}


inline tmr10ms_t getTicks() { return g_tmr10ms; }

int32_t CalculateAltitude(unsigned int pressure)
{
  int32_t alt = getALT(pressure);
  return alt;
}

int32_t GetSensorValueFlySkyNv14(const FlyskyNv14Sensor* sensor,
                                 const uint8_t* data)
{
  int32_t value = 0;
  const nv14SensorData* sensorData =
      reinterpret_cast<const nv14SensorData*>(data + sensor->offset);
  static bool pre_10_0_14_RmFw = false;

  if (sensor->bytes == 1)
    value = sensorData->UINT8;
  else if (sensor->bytes == 2)
    value = sensor->issigned ? sensorData->INT16 : sensorData->UINT16;
  else if (sensor->bytes == 4)
    value = sensorData->UINT32;

  if (NV14internalModuleFwVersion <= 0x1000E) {
    if (sensor->id == FLYSKY_SENSOR_RX_SIGNAL) {
      if (value <= 10)
        pre_10_0_14_RmFw = true;
      else
        pre_10_0_14_RmFw = false;
    }
  }

  // For older RF module FW Sgml is in [0, 10] range
  // and we need to use RSSI for alarm
  if (pre_10_0_14_RmFw) {
    if (sensor->id == FLYSKY_SENSOR_RX_RSSI) {
      if (value < -200) value = -200;
      // if g_model.rssiAlarms.flysky_telemetry == 1
      // RSSI will be kept within native FlySky range [-90, -60]
      if (!g_model.rssiAlarms.flysky_telemetry) {
        value *= 2;
        value += 220;
      }
      telemetryData.rssi.set(value);
    }
  } else if (sensor->id == FLYSKY_SENSOR_RX_SIGNAL) {
    telemetryData.rssi.set(value);
  }

  if (sensor->id == FLYSKY_SENSOR_PRESSURE) {
    static tmr10ms_t prevTimer = 0;
    static uint32_t timePassed = 0;
    static int32_t prevAlt = 0;
    static int32_t vSpeed = 100000;
    static int32_t altChange = 0;

    switch (sensor->subId)
    {
      case 0:
        value = value & PRESSURE_MASK;
        break;
      case 1:
        value = CalculateAltitude(value);
        {
          tmr10ms_t currTimer = getTicks();
          int32_t currAlt = value;
          if (currTimer > prevTimer) {
            timePassed += (currTimer - prevTimer);
            altChange += (currAlt - prevAlt);
            prevAlt = currAlt;
            prevTimer = currTimer;
          } else if(currTimer < prevTimer) {  // overflow
            timePassed = 0;
            altChange = 0;
            prevAlt = currAlt;
            prevTimer = currTimer;
          }
        }
      break;
      case 2:
      // TO DO: fix temperature calculation
        value = (int16_t)(value >> 19) + 150;// - 400;
      break; 
      case 3:
        if (timePassed > V_SPEED_AVERAGING_TIME_10ms) {  // Some averaging
          bool neg = false;
          // There are some problems with negative numbers arithmetic 
          // (division, compiler)
          if (altChange < 0) {
            altChange = -altChange;
            neg = true;
          }
          int32_t tmp = (altChange * 100) / timePassed;
          if (neg)
            vSpeed = -tmp;
          else
            vSpeed = tmp;
          altChange = 0;
          timePassed = 0;
        }
        value = vSpeed;
        break;   
    }
  } 
  return value;
}

// Module pulse synchronization
#define SAFE_SYNC_LAG 800       /* us */
#define SYNC_UPDATE_TIMEOUT 200 /* *10ms */
#define AFHDS2_SYNC_SAMPLES 8
#define AFHDS2_NEGATIVE_SYNC_LIMIT (AFHDS2_PERIOD - SAFE_SYNC_LAG)

int16_t syncAfhds2min = 0;
int16_t syncAfhds2max = 0;
unsigned currentSyncIndex;

void flySkyNv14Sync(int16_t delayValue)
{
  if (delayValue > AFHDS2_NEGATIVE_SYNC_LIMIT) {
    delayValue -= AFHDS2_PERIOD;
  }
  if (currentSyncIndex == 0) {
    syncAfhds2min = AFHDS2_PERIOD;
    syncAfhds2max = -SAFE_SYNC_LAG;
  }

  if (delayValue > syncAfhds2max) {
    syncAfhds2max = delayValue;
  }
  if (delayValue < syncAfhds2min) {
    syncAfhds2min = delayValue;
  }
  if (currentSyncIndex++ == AFHDS2_SYNC_SAMPLES) {
    currentSyncIndex = 0;
    // check against to late delivered frames up to 800us, some frames still in
    // range
    if (syncAfhds2min < 0 && syncAfhds2max < SAFE_SYNC_LAG) {
      getModuleSyncStatus(INTERNAL_MODULE)
          .update(AFHDS2_PERIOD, (syncAfhds2min - 100) + SAFE_SYNC_LAG);
    } else if (syncAfhds2max > SAFE_SYNC_LAG + 100) {  // > 900us
      if (syncAfhds2min > 100) {  // never sync if last registred value is below
                                  // 100us - we are to close to perfect time
        getModuleSyncStatus(INTERNAL_MODULE)
            .update(AFHDS2_PERIOD, (syncAfhds2min - 100) + SAFE_SYNC_LAG);
      } else if (syncAfhds2min < 0) {
        getModuleSyncStatus(INTERNAL_MODULE)
            .update(AFHDS2_PERIOD, (syncAfhds2max - 900) + SAFE_SYNC_LAG);
      }
    }
  }
}

void flySkyNv14ProcessTelemetryPacket(const uint8_t* ptr, uint8_t size)
{
  uint8_t sensorID = ptr[0];
  uint8_t instnace = ptr[1];
  int sensorCount = 0;
  if (sensorID != FLYSKY_SENSOR_SYNC) sensorCount++;

  // native telemetry for 1.1.2
  if (NV14internalModuleFwVersion >= 0x010102) {
    if (sensorID == FLYSKY_SENSOR_SYNC)
      flySkyNv14Sync((int16_t)(ptr[3] << 8 | ptr[2]));
    uint8_t frameType = 0xAA;
    if (size > 4) {
      frameType = 0xAC;
    } else if (size != 4) {
      return;
    }
    processFlySkySensor(ptr, frameType);
  } else {
    if (sensorID == FLYSKY_SENSOR_RX_VOLTAGE)
      sensorID = FLYSKY_FIXED_RX_VOLTAGE;
    for (const FlyskyNv14Sensor* sensor = Nv14Sensor; sensor->id; sensor++) {
      if (sensor->id == sensorID) {
        int32_t value = GetSensorValueFlySkyNv14(sensor, ptr + 2);
        setTelemetryValue(PROTOCOL_TELEMETRY_FLYSKY_NV14, sensor->id,
                          sensor->subId, instnace, value, sensor->unit,
                          sensor->precision);
        if (sensor->id == FLYSKY_SENSOR_SYNC) flySkyNv14Sync(value);
      }
    }
  }
  if (sensorCount) {
    telemetryStreaming = TELEMETRY_TIMEOUT10ms;
  }
}

#endif // defined(AFHDS2)
