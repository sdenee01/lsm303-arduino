#include <LSM303.h>
#include <Wire.h>
#include <math.h>

// Defines ////////////////////////////////////////////////////////////////

// The Arduino two-wire interface uses a 7-bit number for the address,
// and sets the last bit correctly based on reads and writes
#define D_SA0_HIGH_ADDRESS              0b0011101 // D with SA0 high
#define D_SA0_LOW_ADDRESS               0b0011110 // D with SA0 low or non-D magnetometer
#define NON_D_MAG_ADDRESS               0b0011110 // D with SA0 low or non-D magnetometer
#define NON_D_ACC_SA0_LOW_ADDRESS       0b0011000 // non-D accelerometer with SA0 low
#define NON_D_ACC_SA0_HIGH_ADDRESS      0b0011001 // non-D accelerometer with SA0 high

#define TEST_REG_NACK -1

#define D_WHO_ID    0x49
#define DLM_WHO_ID  0x3C

// Constructors ////////////////////////////////////////////////////////////////

LSM303::LSM303(void)
{
  // These values lead to an assumed magnetometer bias of 0.
  // It is recommended that a calibration be done for your particular unit to determine its bias.
  m_max.x = +32767; m_max.y = +32767; m_max.z = +32767;
  m_min.x = -32767; m_min.y = -32767; m_min.z = -32767;

  _device = device_auto;

  io_timeout = 0;  // 0 = no timeout
  did_timeout = false;
}

// Public Methods //////////////////////////////////////////////////////////////

bool LSM303::timeoutOccurred()
{
  return did_timeout;
}

void LSM303::setTimeout(unsigned int timeout)
{
  io_timeout = timeout;
}

unsigned int LSM303::getTimeout()
{
  return io_timeout;
}

bool LSM303::init(deviceType device, sa0State sa0)
{
  // determine device type if necessary
  if (device == device_auto)
  {
    if (testReg(D_SA0_HIGH_ADDRESS, WHO_AM_I) == D_WHO_ID)
    {
      // device responds to address 0011101; it's a D with SA0 high
      device = device_D;
      sa0 = sa0_high;
    }
    else
    {
      // might be D with SA0 low, DLHC, DLM, or DLH
      switch (testReg(D_SA0_LOW_ADDRESS, WHO_AM_I))
      {
        case D_WHO_ID:
          // device responds to address 0011110 with D ID; it's a D with SA0 low
          device = device_D;
          sa0 = sa0_low;
          break;
          
        case DLM_WHO_ID:
          // device responds to address 0011110 with DLM ID; it's a DLM magnetometer (accelerometer SA0 still indeterminate)
          device = device_DLM;
          break;
          
        default: // TEST_REG_NACK
          // might be DLHC or DLH; make a guess based on accelerometer address
          // (Pololu boards pull SA0 low on DLH, and DLHC doesn't have SA0 but uses same acc address as DLH/DLM with SA0 high)
          if (testReg(NON_D_ACC_SA0_HIGH_ADDRESS, CTRL_REG1_A) != TEST_REG_NACK)
          {
            // device responds to address 0011001; guess that it's a DLHC
            device = device_DLHC;
            sa0 = sa0_high;
          }
          else if (testReg(NON_D_ACC_SA0_LOW_ADDRESS, CTRL_REG1_A) != TEST_REG_NACK)
          {
            // device responds to address 0011000; guess that it's a DLH
            device = device_DLH;
            sa0 = sa0_low;
          }
          else
            // device hasn't responded meaningfully, so give up
            return false;
      } 
    }
  }
  
  // determine SA0 if necessary
  if (sa0 == sa0_auto)
  {
    if (device == device_D)
    {
      if (testReg(D_SA0_HIGH_ADDRESS, WHO_AM_I) == D_WHO_ID)
      {
        sa0 = sa0_high;
      }
      else if (testReg(D_SA0_HIGH_ADDRESS, WHO_AM_I) == D_WHO_ID)
      {
        sa0 = sa0_low;
      }
      else
      {
        // no response on either possible address; give up
        return false;  
      }
    }
    else if (device == device_DLM || device == device_DLH)
    {
      if (testReg(NON_D_ACC_SA0_HIGH_ADDRESS, CTRL_REG1_A) != TEST_REG_NACK)
      {
        sa0 = sa0_high;
      }
      else if (testReg(NON_D_ACC_SA0_LOW_ADDRESS, CTRL_REG1_A) != TEST_REG_NACK)
      {
        sa0 = sa0_low;
      }
      else
      {
        // no response on either possible address; give up
        return false;
      }
    }
  }
  
  _device = device;
  
  // set device addresses and translated register addresses
  switch (device)
  {
    case device_D:
      acc_address = mag_address = (sa0 == sa0_high) ? D_SA0_HIGH_ADDRESS : D_SA0_LOW_ADDRESS;
      out_x_l_m = D_OUT_X_L_M;
      out_x_h_m = D_OUT_X_H_M;
      out_y_l_m = D_OUT_Y_L_M;
      out_y_h_m = D_OUT_Y_H_M;
      out_z_l_m = D_OUT_Z_L_M;
      out_z_h_m = D_OUT_Z_H_M;
      break;
      
    case device_DLHC:
      acc_address = NON_D_ACC_SA0_HIGH_ADDRESS; // DLHC doesn't have SA0 but uses same acc address as DLH/DLM with SA0 high
      mag_address = NON_D_MAG_ADDRESS;
      out_x_h_m = DLHC_OUT_X_H_M;
      out_x_l_m = DLHC_OUT_X_L_M;
      out_y_h_m = DLHC_OUT_Y_H_M;
      out_y_l_m = DLHC_OUT_Y_L_M;
      out_z_h_m = DLHC_OUT_Z_H_M;
      out_z_l_m = DLHC_OUT_Z_L_M;
      break;
      
    case device_DLM:
      acc_address = (sa0 == sa0_high) ? NON_D_ACC_SA0_HIGH_ADDRESS : NON_D_ACC_SA0_LOW_ADDRESS;
      mag_address = NON_D_MAG_ADDRESS;
      out_x_h_m = DLM_OUT_X_H_M;
      out_x_l_m = DLM_OUT_X_L_M;
      out_y_h_m = DLM_OUT_Y_H_M;
      out_y_l_m = DLM_OUT_Y_L_M;
      out_z_h_m = DLM_OUT_Z_H_M;
      out_z_l_m = DLM_OUT_Z_L_M;
      break;
      
    case device_DLH:
      acc_address = (sa0 == sa0_high) ? NON_D_ACC_SA0_HIGH_ADDRESS : NON_D_ACC_SA0_LOW_ADDRESS;
      mag_address = NON_D_MAG_ADDRESS;
      out_x_h_m = DLH_OUT_X_H_M;
      out_x_l_m = DLH_OUT_X_L_M;
      out_y_h_m = DLH_OUT_Y_H_M;
      out_y_l_m = DLH_OUT_Y_L_M;
      out_z_h_m = DLH_OUT_Z_H_M;
      out_z_l_m = DLH_OUT_Z_L_M;
      break; 
  }
}

// Turns on the LSM303's accelerometer and magnetometers and places them in normal
// mode.
void LSM303::enableDefault(void)
{

  if (_device == device_D)
  {
    // Enable Accelerometer
    // 0x57 = 0b01010111
    // 50 Hz ODR, all axes enabled
    writeAccReg(CTRL1, 0x57);
    
    // Enable Magnetometer
    // 0x00 = 0b00000000
    // Continuous conversion mode
    writeMagReg(CTRL7, 0x00);
    // 0x70 = 0b01110000
    // high resolution mode, 50 Hz ODR
    writeMagReg(CTRL5, 0x70);
  }
  else
  {
    // Enable Accelerometer
    // 0x27 = 0b00100111
    // Normal power mode (DLHC: 10 Hz), all axes enabled
    writeAccReg(CTRL_REG1_A, 0x27);

    if (_device == device_DLHC)
      writeAccReg(CTRL_REG4_A, 0x08); // DLHC: enable high resolution mode

    // Enable Magnetometer
    // 0x00 = 0b00000000
    // Continuous conversion mode
    writeMagReg(MR_REG_M, 0x00);
  }
}

// Writes an accelerometer register
void LSM303::writeAccReg(regAddr reg, byte value)
{
  Wire.beginTransmission(acc_address);
  Wire.write((byte)reg);
  Wire.write(value);
  last_status = Wire.endTransmission();
}

// Reads an accelerometer register
byte LSM303::readAccReg(regAddr reg)
{
  byte value;

  Wire.beginTransmission(acc_address);
  Wire.write((byte)reg);
  last_status = Wire.endTransmission();
  Wire.requestFrom(acc_address, (byte)1);
  value = Wire.read();
  Wire.endTransmission();

  return value;
}

// Writes a magnetometer register
void LSM303::writeMagReg(regAddr reg, byte value)
{
  Wire.beginTransmission(mag_address);
  Wire.write((byte)reg);
  Wire.write(value);
  last_status = Wire.endTransmission();
}

// Reads a magnetometer register
byte LSM303::readMagReg(regAddr reg)
{
  byte value;

  // if dummy register address (magnetometer Y/Z), use device type to determine actual address
  if (reg < 0)
  {
    switch (reg)
    {
      case OUT_X_H_M:
        reg = out_x_h_m;
        break;
      case OUT_X_L_M:
        reg = out_x_l_m;
        break;
      case OUT_Y_H_M:
        reg = out_y_h_m;
        break;
      case OUT_Y_L_M:
        reg = out_y_l_m;
        break;
      case OUT_Z_H_M:
        reg = out_z_h_m;
        break;
      case OUT_Z_L_M:
        reg = out_z_l_m;
        break;
    }
  }

  Wire.beginTransmission(mag_address);
  Wire.write((byte)reg);
  last_status = Wire.endTransmission();
  Wire.requestFrom(mag_address, (byte)1);
  value = Wire.read();
  Wire.endTransmission();

  return value;
}

void LSM303::writeReg(regAddr reg, byte value)
{
  // mag address == acc_address for LSM303D, so it doesn't really matter which one we use.
  // Use writeMagReg so it can translate OUT_[XYZ]_[HL]_M
  if (_device == device_D || reg < CTRL_REG1_A || reg == TEMP_OUT_H_M || TEMP_OUT_L_M)
  {
    writeMagReg(reg, value);
  }
  else
  {
    writeAccReg(reg, value);
  }
}

byte LSM303::readReg(regAddr reg)
{
  // mag address == acc_address for LSM303D, so it doesn't really matter which one we use.
  // Use writeMagReg so it can translate OUT_[XYZ]_[HL]_M
  if (_device == device_D || reg < CTRL_REG1_A || reg == TEMP_OUT_H_M || TEMP_OUT_L_M)
  {
    return readMagReg(reg);
  }
  else
  {
    return readAccReg(reg);
  }
}

// Reads the 3 accelerometer channels and stores them in vector a
void LSM303::readAcc(void)
{
  Wire.beginTransmission(acc_address);
  // assert the MSB of the address to get the accelerometer
  // to do slave-transmit subaddress updating.
  Wire.write(OUT_X_L_A | (1 << 7));
  last_status = Wire.endTransmission();
  Wire.requestFrom(acc_address, (byte)6);

  unsigned int millis_start = millis();
  did_timeout = false;
  while (Wire.available() < 6) {
    if (io_timeout > 0 && ((unsigned int)millis() - millis_start) > io_timeout)
    {
      did_timeout = true;
      return;
    }
  }

  byte xla = Wire.read();
  byte xha = Wire.read();
  byte yla = Wire.read();
  byte yha = Wire.read();
  byte zla = Wire.read();
  byte zha = Wire.read();

  a.x = (int16_t)(xha << 8 | xla);
  a.y = (int16_t)(yha << 8 | yla);
  a.z = (int16_t)(zha << 8 | zla);
  
  // LSM303D has 16-bit accelerometer outputs. For all others, 
  // combine high and low bytes, then shift right to discard lowest 4 bits (which are meaningless)
  // GCC performs an arithmetic right shift for signed negative numbers, but this code will not work
  // if you port it to a compiler that does a logical right shift instead.
  if (_device != device_D)
  {
    a.x >>= 4;
    a.y >>= 4;
    a.z >>= 4;
  }
}

// Reads the 3 magnetometer channels and stores them in vector m
void LSM303::readMag(void)
{
  Wire.beginTransmission(mag_address);
  // If LSM303D, assert MSB to enable subaddress updating
  // OUT_X_L_M comes first on D, OUT_X_H_M on others
  Wire.write((_device == device_D) ? out_x_l_m | (1 << 7) : out_x_h_m);
  last_status = Wire.endTransmission();
  Wire.requestFrom(mag_address, (byte)6);

  unsigned int millis_start = millis();
  did_timeout = false;
  while (Wire.available() < 6) {
    if (io_timeout > 0 && ((unsigned int)millis() - millis_start) > io_timeout)
    {
      did_timeout = true;
      return;
    }
  }
  
  byte xlm, xhm, ylm, yhm, zlm, zhm;
  
  if (_device == device_D)
  {
    /// D: X_L, X_H, Y_L, Y_H, Z_L, Z_H
    xlm = Wire.read();
    xhm = Wire.read();
    ylm = Wire.read();
    yhm = Wire.read();
    zlm = Wire.read();
    zhm = Wire.read();
  }
  else
  {
    // DLHC, DLM, DLH: X_H, X_L...
    xhm = Wire.read();
    xlm = Wire.read();

    if (_device == device_DLH)
    {
      // DLH: ...Y_H, Y_L, Z_H, Z_L
      yhm = Wire.read();
      ylm = Wire.read();
      zhm = Wire.read();
      zlm = Wire.read();
    }
    else
    {
      // DLM, DLHC: ...Z_H, Z_L, Y_H, Y_L
      zhm = Wire.read();
      zlm = Wire.read();
      yhm = Wire.read();
      ylm = Wire.read();
    }
  }

  // combine high and low bytes
  m.x = (int16_t)(xhm << 8 | xlm);
  m.y = (int16_t)(yhm << 8 | ylm);
  m.z = (int16_t)(zhm << 8 | zlm);
}

// Reads all 6 channels of the LSM303 and stores them in the object variables
void LSM303::read(void)
{
  readAcc();
  readMag();
}

// Returns the number of degrees from the -Y axis that it
// is pointing.
int LSM303::heading(void)
{
  return heading((vector<int>){0,-1,0});
}

// Returns the angular difference in the horizontal plane between the
// From vector and North, in degrees.
//
// Description of heading algorithm:
// Shift and scale the magnetic reading based on calibration data to
// to find the North vector. Use the acceleration readings to
// determine the Up vector (gravity is measured as an upward
// acceleration). The cross product of North and Up vectors is East.
// The vectors East and North form a basis for the horizontal plane.
// The From vector is projected into the horizontal plane and the
// angle between the projected vector and north is returned.
template <typename T> float LSM303::heading(vector<T> from)
{
    vector<int32_t> temp_m = {m.x, m.y, m.z};
    
    // subtract offset (average of min and max) from magnetometer readings
    temp_m.x -= ((int32_t)m_min.x + m_max.x) / 2;
    temp_m.y -= ((int32_t)m_min.y + m_max.y) / 2;
    temp_m.z -= ((int32_t)m_min.z + m_max.z) / 2;
  
    // compute E and N
    vector<float> E;
    vector<float> N;
    vector_cross(&temp_m, &a, &E);
    vector_normalize(&E);
    vector_cross(&a, &E, &N);
    vector_normalize(&N);

    // compute heading
    float heading = atan2(vector_dot(&E, &from), vector_dot(&N, &from)) * 180 / M_PI;
    if (heading < 0) heading += 360;
    return heading;
}

template <typename Ta, typename Tb, typename To> void LSM303::vector_cross(const vector<Ta> *a,const vector<Tb> *b, vector<To> *out)
{
  out->x = (a->y * b->z) - (a->z * b->y);
  out->y = (a->z * b->x) - (a->x * b->z);
  out->z = (a->x * b->y) - (a->y * b->x);
}

template <typename Ta, typename Tb> float LSM303::vector_dot(const vector<Ta> *a, const vector<Tb> *b)
{
  return (a->x * b->x) + (a->y * b->y) + (a->z * b->z);
}

void LSM303::vector_normalize(vector<float> *a)
{
  float mag = sqrt(vector_dot(a, a));
  a->x /= mag;
  a->y /= mag;
  a->z /= mag;
}

// Private Methods //////////////////////////////////////////////////////////////

int LSM303::testReg(byte address, regAddr reg)
{
  Wire.beginTransmission(address);
  Wire.write((byte)reg);
  last_status = Wire.endTransmission();
  
  Wire.requestFrom(address, (byte)1);
  if (Wire.available())
    return Wire.read();
  else
    return TEST_REG_NACK;
}