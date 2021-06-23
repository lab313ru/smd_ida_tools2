#include "resource.h"
#include <Windows.h>
#include <CommCtrl.h>
#include <map>
#include <sstream>
#include <iomanip>

#include "g_main.h"
#include "ym2612.h"
#include "ym2612_debug.h"
#include "psg.h"

enum class YM2612Channels :unsigned int
{
  CHANNEL1 = 0,
  CHANNEL2 = 1,
  CHANNEL3 = 2,
  CHANNEL4 = 3,
  CHANNEL5 = 4,
  CHANNEL6 = 5
};

enum class YM2612Operators :unsigned int
{
  OPERATOR1 = 0,
  OPERATOR2 = 1,
  OPERATOR3 = 2,
  OPERATOR4 = 3
};

static YM2612Channels selectedChannel;

static std::string previousText;
static unsigned int currentControlFocus;

static const unsigned int channelCount = 6;
static const unsigned int operatorCount = 4;
static const unsigned int partCount = 2;
static const unsigned int registerCountPerPart = 0x100;
static const unsigned int registerCountTotal = registerCountPerPart * partCount;

int enabled_channels[channelCount] = { 1, 1, 1, 1, 1, 1 };

//----------------------------------------------------------------------------------------
const unsigned int channelAddressOffsets[channelCount] = {
  0,                         //Channel 1
  1,                         //Channel 2
  2,                         //Channel 3
  registerCountPerPart + 0,  //Channel 4
  registerCountPerPart + 1,  //Channel 5
  registerCountPerPart + 2 }; //Channel 6

//----------------------------------------------------------------------------------------
const unsigned int operatorAddressOffsets[channelCount][operatorCount] = {
  {channelAddressOffsets[0] + 0x0, channelAddressOffsets[0] + 0x8, channelAddressOffsets[0] + 0x4, channelAddressOffsets[0] + 0xC},  //Channel 1
  {channelAddressOffsets[1] + 0x0, channelAddressOffsets[1] + 0x8, channelAddressOffsets[1] + 0x4, channelAddressOffsets[1] + 0xC},  //Channel 2
  {channelAddressOffsets[2] + 0x0, channelAddressOffsets[2] + 0x8, channelAddressOffsets[2] + 0x4, channelAddressOffsets[2] + 0xC},  //Channel 3
  {channelAddressOffsets[3] + 0x0, channelAddressOffsets[3] + 0x8, channelAddressOffsets[3] + 0x4, channelAddressOffsets[3] + 0xC},  //Channel 4
  {channelAddressOffsets[4] + 0x0, channelAddressOffsets[4] + 0x8, channelAddressOffsets[4] + 0x4, channelAddressOffsets[4] + 0xC},  //Channel 5
  {channelAddressOffsets[5] + 0x0, channelAddressOffsets[5] + 0x8, channelAddressOffsets[5] + 0x4, channelAddressOffsets[5] + 0xC},  //Channel 6
};

//----------------------------------------------------------------------------------------
const unsigned int channel3OperatorFrequencyAddressOffsets[2][operatorCount] = {
  {0xA9, 0xAA, 0xA8, 0xA2},
  {0xAD, 0xAE, 0xAC, 0xA6} };

//----------------------------------------------------------------------------------------
static inline unsigned int GetChannelBlockAddressOffset(YM2612Channels channelNo) {
  return channelAddressOffsets[static_cast<int>(channelNo)];
}

//----------------------------------------------------------------------------------------
static inline unsigned int GetOperatorBlockAddressOffset(YM2612Channels channelNo, YM2612Operators operatorNo) {
  return operatorAddressOffsets[static_cast<int>(channelNo)][static_cast<int>(operatorNo)];
}

//----------------------------------------------------------------------------------------
static inline unsigned int GetAddressChannel3FrequencyBlock1(YM2612Operators operatorNo) {
  return channel3OperatorFrequencyAddressOffsets[0][static_cast<int>(operatorNo)];
}

//----------------------------------------------------------------------------------------
static inline unsigned int GetAddressChannel3FrequencyBlock2(YM2612Operators operatorNo) {
  return channel3OperatorFrequencyAddressOffsets[1][static_cast<int>(operatorNo)];
}

static inline int GetRegisterData(unsigned int location) {
   return ((location < 0x100) ? YM2612.REG[0][location] : YM2612.REG[1][location - 0x100]) & 0xFF;
}

//----------------------------------------------------------------------------------------
static inline void SetRegisterData(unsigned int location, const unsigned char data) {
  bool firstPart = location < 0x100;
  YM2612_Write(firstPart ? 0 : 2, firstPart ? location : (location - 0x100));
  YM2612_Write(firstPart ? 1 : 3, data);
}

//----------------------------------------------------------------------------------------
static inline bool GetBit(unsigned int data, unsigned int bitNumber)
{
  return (data & (1 << bitNumber)) != 0;
}

//----------------------------------------------------------------------------------------
static inline unsigned int SetBit(unsigned int data, unsigned int bitNumber, bool state) {
  return ((data & ~(1 << bitNumber)) | ((unsigned int)state << bitNumber));
}

//----------------------------------------------------------------------------------------
static inline unsigned int GetDataSegment(unsigned int data, unsigned int bitStart, unsigned int abitCount) {
  return (data >> bitStart) & ((((1 << (abitCount - 1)) - 1) << 1) | 0x01);
}

//----------------------------------------------------------------------------------------
static inline unsigned int SetDataSegment(unsigned int data, unsigned int bitStart, unsigned int abitCount, unsigned int adata)
{
  unsigned int tempMask = (((1 << (abitCount - 1)) - 1) << 1) | 0x01;
  data &= ~(tempMask << bitStart);
  data |= ((adata & tempMask) << bitStart);
  return data;
}

//----------------------------------------------------------------------------------------
static inline unsigned int MaskData(unsigned int data, unsigned char bitsCount) {
  return data & ((((1 << (bitsCount - 1)) - 1) << 1) | 0x01);
}

//----------------------------------------------------------------------------------------
static inline unsigned int SetUpperBits(unsigned int data, unsigned char bitsCount, unsigned int abitCount, unsigned int adata)
{
  unsigned int targetMask = (((1 << (abitCount - 1)) - 1) << 1) | 0x01;
  data &= ~(targetMask << (bitsCount - abitCount));
  data |= ((adata & targetMask) << (bitsCount - abitCount));
  return MaskData(data, bitsCount);
}

//----------------------------------------------------------------------------------------
static inline unsigned int SetLowerBits(unsigned int data, unsigned char bitsCount, unsigned int abitCount, unsigned int adata) {
  unsigned int targetMask = (((1 << (abitCount - 1)) - 1) << 1) | 0x01;
  data &= ~targetMask;
  data |= (adata & targetMask);
  return MaskData(data, bitsCount);
}

//----------------------------------------------------------------------------------------
static inline unsigned int SetData(unsigned int data, unsigned char bitsCount, unsigned int adata)
{
  data = adata;
  return MaskData(data, bitsCount);
}

//----------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------
static inline void SetAmplitudeModulationEnabled(YM2612Channels chn, YM2612Operators op, bool enabled) {
  unsigned int operatorAddressOffset = GetOperatorBlockAddressOffset(chn, op);
  
  unsigned int registerNo = operatorAddressOffset + 0x60;
  unsigned char val = GetRegisterData(registerNo);
  SetRegisterData(registerNo, SetBit(val, 7, enabled));
}

static inline bool GetAmplitudeModulationEnabled(YM2612Channels chn, YM2612Operators op) {
  unsigned int operatorAddressOffset = GetOperatorBlockAddressOffset(chn, op);

  unsigned int registerNo = operatorAddressOffset + 0x60;
  unsigned char val = GetRegisterData(registerNo);
  return GetBit(val, 7);
}

static inline void SetOutputLeft(YM2612Channels chn, bool enabled) {
  unsigned int channelAddressOffset = GetChannelBlockAddressOffset(chn);

  unsigned int registerNo = channelAddressOffset + 0xB4;
  unsigned char val = GetRegisterData(registerNo);
  SetRegisterData(registerNo, SetBit(val, 7, enabled));
}

static inline bool GetOutputLeft(YM2612Channels chn) {
  unsigned int channelAddressOffset = GetChannelBlockAddressOffset(chn);

  unsigned int registerNo = channelAddressOffset + 0xB4;
  unsigned char val = GetRegisterData(registerNo);
  return GetBit(val, 7);
}

static inline void SetOutputRight(YM2612Channels chn, bool enabled) {
  unsigned int channelAddressOffset = GetChannelBlockAddressOffset(chn);

  unsigned int registerNo = channelAddressOffset + 0xB4;
  unsigned char val = GetRegisterData(registerNo);
  SetRegisterData(registerNo, SetBit(val, 6, enabled));
}

static inline bool GetOutputRight(YM2612Channels chn) {
  unsigned int channelAddressOffset = GetChannelBlockAddressOffset(chn);

  unsigned int registerNo = channelAddressOffset + 0xB4;
  unsigned char val = GetRegisterData(registerNo);
  return GetBit(val, 6);
}

static inline void SetTimerAEnable(bool enabled) {
  unsigned char val = GetRegisterData(0x27);
  SetRegisterData(0x27, SetBit(val, 2, enabled));
}

static inline bool GetTimerAEnable() {
  unsigned char val = GetRegisterData(0x27);
  return GetBit(val, 2);
}

static inline void SetTimerBEnable(bool enabled) {
  unsigned char val = GetRegisterData(0x27);
  SetRegisterData(0x27, SetBit(val, 3, enabled));
}

static inline bool GetTimerBEnable() {
  unsigned char val = GetRegisterData(0x27);
  return GetBit(val, 3);
}

static inline void SetTimerALoad(bool enabled) {
  unsigned char val = GetRegisterData(0x27);
  SetRegisterData(0x27, SetBit(val, 0, enabled));
}

static inline bool GetTimerALoad() {
  unsigned char val = GetRegisterData(0x27);
  return GetBit(val, 0);
}

static inline void SetTimerBLoad(bool enabled) {
  unsigned char val = GetRegisterData(0x27);
  SetRegisterData(0x27, SetBit(val, 1, enabled));
}

static inline bool GetTimerBLoad() {
  unsigned char val = GetRegisterData(0x27);
  return GetBit(val, 1);
}

static inline void SetTimerAOverflow(bool enabled) {
  YM2612.Status = SetBit(YM2612.Status, 0, enabled);
}

static inline bool GetTimerAOverflow() {
  return GetBit(YM2612.Status, 0);
}

static inline void SetTimerBOverflow(bool enabled) {
  YM2612.Status = SetBit(YM2612.Status, 1, enabled);
}

static inline bool GetTimerBOverflow() {
  return GetBit(YM2612.Status, 1);
}

static inline void SetLFOEnabled(bool enabled) {
  unsigned char val = GetRegisterData(0x22);
  SetRegisterData(0x22, SetBit(val, 3, enabled));
}

static inline bool GetLFOEnabled() {
  unsigned char val = GetRegisterData(0x22);
  return GetBit(val, 3);
}

static inline void SetDACEnabled(bool enabled) {
  unsigned char val = GetRegisterData(0x2B);
  SetRegisterData(0x2B, SetBit(val, 7, enabled));
}

static inline bool GetDACEnabled() {
  unsigned char val = GetRegisterData(0x2B);
  return GetBit(val, 7);
}

extern "C" INLINE void KEY_ON(channel_ *CH, int nsl);
extern "C" INLINE void KEY_OFF(channel_ *CH, int nsl);

#define S0             0    // Stupid typo of the YM2612
#define S1             2
#define S2             1
#define S3             3
#define RELEASE   3

static inline void SetKeyState(YM2612Channels chn, YM2612Operators op, bool enabled) {
  auto ch = &YM2612.CHANNEL[static_cast<int>(chn)];
  
  switch (op) {
  case YM2612Operators::OPERATOR1: {
    if (enabled) KEY_ON(ch, S0); else KEY_OFF(ch, S0);
  } break;
  case YM2612Operators::OPERATOR2: {
    if (enabled) KEY_ON(ch, S1); else KEY_OFF(ch, S1);
  } break;
  case YM2612Operators::OPERATOR3: {
    if (enabled) KEY_ON(ch, S2); else KEY_OFF(ch, S2);
  } break;
  case YM2612Operators::OPERATOR4: {
    if (enabled) KEY_ON(ch, S3); else KEY_OFF(ch, S3);
  } break;
  }
}

static inline bool GetKeyState(YM2612Channels chn, YM2612Operators op) {
  auto ch = YM2612.CHANNEL[static_cast<int>(chn)];

  switch (op) {
  case YM2612Operators::OPERATOR1: {
    return ch.SLOT[S0].Ecurp == RELEASE;
  } break;
  case YM2612Operators::OPERATOR2: {
    return ch.SLOT[S1].Ecurp == RELEASE;
  } break;
  case YM2612Operators::OPERATOR3: {
    return ch.SLOT[S2].Ecurp == RELEASE;
  } break;
  case YM2612Operators::OPERATOR4: {
    return ch.SLOT[S3].Ecurp == RELEASE;
  } break;
  default:
    return false;
  }
}

#undef S0
#undef S1
#undef S2
#undef S3
#undef RELEASE

static inline void SetTotalLevelData(YM2612Channels chn, YM2612Operators op, unsigned int data) {
  unsigned int operatorAddressOffset = GetOperatorBlockAddressOffset(chn, op);

  unsigned int registerNo = operatorAddressOffset + 0x40;
  unsigned char val = GetRegisterData(registerNo);
  SetRegisterData(registerNo, SetDataSegment(val, 0, 7, data));
}

static inline unsigned int GetTotalLevelData(YM2612Channels chn, YM2612Operators op) {
  unsigned int operatorAddressOffset = GetOperatorBlockAddressOffset(chn, op);

  unsigned int registerNo = operatorAddressOffset + 0x40;
  unsigned char val = GetRegisterData(registerNo);
  return GetDataSegment(val, 0, 7);
}

static inline void SetSustainLevelData(YM2612Channels chn, YM2612Operators op, unsigned int data) {
  unsigned int operatorAddressOffset = GetOperatorBlockAddressOffset(chn, op);

  unsigned int registerNo = operatorAddressOffset + 0x80;
  unsigned char val = GetRegisterData(registerNo);
  SetRegisterData(registerNo, SetDataSegment(val, 4, 4, data));
}

static inline unsigned int GetSustainLevelData(YM2612Channels chn, YM2612Operators op) {
  unsigned int operatorAddressOffset = GetOperatorBlockAddressOffset(chn, op);

  unsigned int registerNo = operatorAddressOffset + 0x80;
  unsigned char val = GetRegisterData(registerNo);
  return GetDataSegment(val, 4, 4);
}

static inline void SetAttackRateData(YM2612Channels chn, YM2612Operators op, unsigned int data) {
  unsigned int operatorAddressOffset = GetOperatorBlockAddressOffset(chn, op);

  unsigned int registerNo = operatorAddressOffset + 0x50;
  unsigned char val = GetRegisterData(registerNo);
  SetRegisterData(registerNo, SetDataSegment(val, 0, 5, data));
}

static inline unsigned int GetAttackRateData(YM2612Channels chn, YM2612Operators op) {
  unsigned int operatorAddressOffset = GetOperatorBlockAddressOffset(chn, op);

  unsigned int registerNo = operatorAddressOffset + 0x50;
  unsigned char val = GetRegisterData(registerNo);
  return GetDataSegment(val, 0, 5);
}

static inline void SetDecayRateData(YM2612Channels chn, YM2612Operators op, unsigned int data) {
  unsigned int operatorAddressOffset = GetOperatorBlockAddressOffset(chn, op);

  unsigned int registerNo = operatorAddressOffset + 0x60;
  unsigned char val = GetRegisterData(registerNo);
  SetRegisterData(registerNo, SetDataSegment(val, 0, 5, data));
}

static inline unsigned int GetDecayRateData(YM2612Channels chn, YM2612Operators op) {
  unsigned int operatorAddressOffset = GetOperatorBlockAddressOffset(chn, op);
  
  unsigned int registerNo = operatorAddressOffset + 0x60;
  unsigned char val = GetRegisterData(registerNo);
  return GetDataSegment(val, 0, 5);
}

static inline void SetSustainRateData(YM2612Channels chn, YM2612Operators op, unsigned int data) {
  unsigned int operatorAddressOffset = GetOperatorBlockAddressOffset(chn, op);

  unsigned int registerNo = operatorAddressOffset + 0x70;
  unsigned char val = GetRegisterData(registerNo);
  SetRegisterData(registerNo, SetDataSegment(val, 0, 5, data));
}

static inline unsigned int GetSustainRateData(YM2612Channels chn, YM2612Operators op) {
  unsigned int operatorAddressOffset = GetOperatorBlockAddressOffset(chn, op);

  unsigned int registerNo = operatorAddressOffset + 0x70;
  unsigned char val = GetRegisterData(registerNo);
  return GetDataSegment(val, 0, 5);
}

static inline void SetReleaseRateData(YM2612Channels chn, YM2612Operators op, unsigned int data) {
  unsigned int operatorAddressOffset = GetOperatorBlockAddressOffset(chn, op);

  unsigned int registerNo = operatorAddressOffset + 0x80;
  unsigned char val = GetRegisterData(registerNo);
  SetRegisterData(registerNo, SetDataSegment(val, 0, 4, data));
}

static inline unsigned int GetReleaseRateData(YM2612Channels chn, YM2612Operators op) {
  unsigned int operatorAddressOffset = GetOperatorBlockAddressOffset(chn, op);

  unsigned int registerNo = operatorAddressOffset + 0x80;
  unsigned char val = GetRegisterData(registerNo);
  return GetDataSegment(val, 0, 4);
}

static inline void SetSSGData(YM2612Channels chn, YM2612Operators op, unsigned int data) {
  unsigned int operatorAddressOffset = GetOperatorBlockAddressOffset(chn, op);

  unsigned int registerNo = operatorAddressOffset + 0x90;
  unsigned char val = GetRegisterData(registerNo);
  SetRegisterData(registerNo, SetDataSegment(val, 0, 4, data));
}

static inline unsigned int GetSSGData(YM2612Channels chn, YM2612Operators op) {
  unsigned int operatorAddressOffset = GetOperatorBlockAddressOffset(chn, op);

  unsigned int registerNo = operatorAddressOffset + 0x90;
  unsigned char val = GetRegisterData(registerNo);
  return GetDataSegment(val, 0, 4);
}

static inline void SetDetuneData(YM2612Channels chn, YM2612Operators op, unsigned int data) {
  unsigned int operatorAddressOffset = GetOperatorBlockAddressOffset(chn, op);

  unsigned int registerNo = operatorAddressOffset + 0x30;
  unsigned char val = GetRegisterData(registerNo);
  SetRegisterData(registerNo, SetDataSegment(val, 4, 3, data));
}

static inline unsigned int GetDetuneData(YM2612Channels chn, YM2612Operators op) {
  unsigned int operatorAddressOffset = GetOperatorBlockAddressOffset(chn, op);

  unsigned int registerNo = operatorAddressOffset + 0x30;
  unsigned char val = GetRegisterData(registerNo);
  return GetDataSegment(val, 4, 3);
}

static inline void SetMultipleData(YM2612Channels chn, YM2612Operators op, unsigned int data) {
  unsigned int operatorAddressOffset = GetOperatorBlockAddressOffset(chn, op);

  unsigned int registerNo = operatorAddressOffset + 0x30;
  unsigned char val = GetRegisterData(registerNo);
  SetRegisterData(registerNo, SetDataSegment(val, 0, 4, data));
}

static inline unsigned int GetMultipleData(YM2612Channels chn, YM2612Operators op) {
  unsigned int operatorAddressOffset = GetOperatorBlockAddressOffset(chn, op);

  unsigned int registerNo = operatorAddressOffset + 0x30;
  unsigned char val = GetRegisterData(registerNo);
  return GetDataSegment(val, 0, 4);
}

static inline void SetKeyScaleData(YM2612Channels chn, YM2612Operators op, unsigned int data) {
  unsigned int operatorAddressOffset = GetOperatorBlockAddressOffset(chn, op);

  unsigned int registerNo = operatorAddressOffset + 0x50;
  unsigned char val = GetRegisterData(registerNo);
  SetRegisterData(registerNo, SetDataSegment(val, 6, 2, data));
}

static inline unsigned int GetKeyScaleData(YM2612Channels chn, YM2612Operators op) {
  unsigned int operatorAddressOffset = GetOperatorBlockAddressOffset(chn, op);

  unsigned int registerNo = operatorAddressOffset + 0x50;
  unsigned char val = GetRegisterData(registerNo);
  return GetDataSegment(val, 6, 2);
}

static inline void SetAlgorithmData(YM2612Channels chn, unsigned int data) {
  unsigned int channelAddressOffset = GetChannelBlockAddressOffset(chn);

  unsigned int registerNo = channelAddressOffset + 0xB0;
  unsigned char val = GetRegisterData(registerNo);
  SetRegisterData(registerNo, SetDataSegment(val, 0, 3, data));
}

static inline unsigned int GetAlgorithmData(YM2612Channels chn) {
  unsigned int channelAddressOffset = GetChannelBlockAddressOffset(chn);

  unsigned int registerNo = channelAddressOffset + 0xB0;
  unsigned char val = GetRegisterData(registerNo);
  return GetDataSegment(val, 0, 3);
}

static inline void SetFeedbackData(YM2612Channels chn, unsigned int data) {
  unsigned int channelAddressOffset = GetChannelBlockAddressOffset(chn);

  unsigned int registerNo = channelAddressOffset + 0xB0;
  unsigned char val = GetRegisterData(registerNo);
  SetRegisterData(registerNo, SetDataSegment(val, 3, 3, data));
}

static inline unsigned int GetFeedbackData(YM2612Channels chn) {
  unsigned int channelAddressOffset = GetChannelBlockAddressOffset(chn);

  unsigned int registerNo = channelAddressOffset + 0xB0;
  unsigned char val = GetRegisterData(registerNo);
  return GetDataSegment(val, 3, 3);
}

static inline void SetFrequencyData(YM2612Channels chn, unsigned int data) {
  unsigned int channelAddressOffset = GetChannelBlockAddressOffset(chn);

  unsigned int register1 = channelAddressOffset + 0xA0;
  unsigned int register2 = channelAddressOffset + 0xA4;

  unsigned char val1 = GetRegisterData(register1);
  SetRegisterData(register1, SetData(val1, 8, GetDataSegment(data, 0, 8)));
  unsigned char val2 = GetRegisterData(register2);
  SetRegisterData(register2, SetDataSegment(val2, 0, 3, GetDataSegment(data, 8, 3)));
}

static inline unsigned int GetFrequencyData(YM2612Channels chn) {
  unsigned int channelAddressOffset = GetChannelBlockAddressOffset(chn);

  unsigned int register1 = channelAddressOffset + 0xA0;
  unsigned int register2 = channelAddressOffset + 0xA4;

  unsigned char val1 = GetRegisterData(register1);
  unsigned int data = SetLowerBits(0, 11, 8, val1);
  unsigned char val2 = GetRegisterData(register2);
  return SetUpperBits(data, 11, 3, GetDataSegment(val2, 0, 3));
}

static inline void SetBlockData(YM2612Channels chn, unsigned int data) {
  unsigned int channelAddressOffset = GetChannelBlockAddressOffset(chn);

  unsigned int registerNo = channelAddressOffset + 0xA4;
  unsigned char val = GetRegisterData(registerNo);
  SetRegisterData(registerNo, SetDataSegment(val, 3, 3, data));
}

static inline unsigned int GetBlockData(YM2612Channels chn) {
  unsigned int channelAddressOffset = GetChannelBlockAddressOffset(chn);

  unsigned int registerNo = channelAddressOffset + 0xA4;
  unsigned char val = GetRegisterData(registerNo);
  return GetDataSegment(val, 3, 3);
}

static inline void SetAMSData(YM2612Channels chn, unsigned int data) {
  unsigned int channelAddressOffset = GetChannelBlockAddressOffset(chn);

  unsigned int registerNo = channelAddressOffset + 0xB4;
  unsigned char val = GetRegisterData(registerNo);
  SetRegisterData(registerNo, SetDataSegment(val, 4, 2, data));
}

static inline unsigned int GetAMSData(YM2612Channels chn) {
  unsigned int channelAddressOffset = GetChannelBlockAddressOffset(chn);

  unsigned int registerNo = channelAddressOffset + 0xB4;
  unsigned char val = GetRegisterData(registerNo);
  return GetDataSegment(val, 4, 2);
}

static inline void SetPMSData(YM2612Channels chn, unsigned int data) {
  unsigned int channelAddressOffset = GetChannelBlockAddressOffset(chn);

  unsigned int registerNo = channelAddressOffset + 0xB4;
  unsigned char val = GetRegisterData(registerNo);
  SetRegisterData(registerNo, SetDataSegment(val, 0, 3, data));
}

static inline unsigned int GetPMSData(YM2612Channels chn) {
  unsigned int channelAddressOffset = GetChannelBlockAddressOffset(chn);

  unsigned int registerNo = channelAddressOffset + 0xB4;
  unsigned char val = GetRegisterData(registerNo);
  return GetDataSegment(val, 0, 3);
}

static inline void SetCH3Mode(unsigned int mode) {
  unsigned char val = GetRegisterData(0x27);
  SetRegisterData(0x27, SetDataSegment(val, 6, 2, mode));
}

static inline unsigned int GetCH3Mode() {
  unsigned char val = GetRegisterData(0x27);
  return GetDataSegment(val, 6, 2);
}

static inline void SetFrequencyDataChannel3(YM2612Operators op, unsigned int data) {
  unsigned int register1 = GetAddressChannel3FrequencyBlock1(op);
  unsigned int register2 = GetAddressChannel3FrequencyBlock2(op);

  unsigned char val1 = GetRegisterData(register1);
  SetRegisterData(register1, SetData(val1, 8, GetDataSegment(data, 0, 8)));
  unsigned char val2 = GetRegisterData(register2);
  SetRegisterData(register2, SetDataSegment(val2, 0, 3, GetDataSegment(data, 8, 3)));
}

static inline unsigned int GetFrequencyDataChannel3(YM2612Operators op) {
  unsigned int register1 = GetAddressChannel3FrequencyBlock1(op);
  unsigned int register2 = GetAddressChannel3FrequencyBlock2(op);

  unsigned char val1 = GetRegisterData(register1);
  unsigned int data = SetLowerBits(0, 11, 8, val1);
  unsigned char val2 = GetRegisterData(register2);
  return SetUpperBits(data, 11, 3, GetDataSegment(val2, 0, 3));
}

static inline void SetBlockDataChannel3(YM2612Operators op, unsigned int data) {
  unsigned int registerNo = GetAddressChannel3FrequencyBlock2(op);
  unsigned char val = GetRegisterData(registerNo);
  SetRegisterData(registerNo, SetDataSegment(val, 3, 3, data));
}

static inline unsigned int GetBlockDataChannel3(YM2612Operators op) {
  unsigned int registerNo = GetAddressChannel3FrequencyBlock2(op);
  unsigned char val = GetRegisterData(registerNo);
  return GetDataSegment(val, 3, 3);
}

static inline void SetLFOData(unsigned int data) {
  unsigned char val = GetRegisterData(0x22);
  SetRegisterData(0x22, SetDataSegment(val, 0, 3, data));
}

static inline unsigned int GetLFOData() {
  unsigned char val = GetRegisterData(0x22);
  return GetDataSegment(val, 0, 3);
}

static inline void SetDACData(unsigned int data) {
  unsigned char val = GetRegisterData(0x2A);
  SetRegisterData(0x2A, SetData(val, 8, data));
}

static inline unsigned int GetDACData() {
  return GetRegisterData(0x2A);
}


static unsigned int GetDlgItemHex(HWND hwnd, int controlID)
{
  unsigned int value = 0;

  const unsigned int maxTextLength = 1024;
  char currentTextTemp[maxTextLength];
  if (GetDlgItemText(hwnd, controlID, currentTextTemp, maxTextLength) == 0)
  {
    currentTextTemp[0] = '\0';
  }
  std::stringstream buffer;
  buffer << std::hex << currentTextTemp;
  buffer >> value;

  return value;
}

static std::string GetDlgItemString(HWND hwnd, int controlID)
{
  std::string result;

  const unsigned int maxTextLength = 1024;
  char currentTextTemp[maxTextLength];
  if (GetDlgItemText(hwnd, controlID, currentTextTemp, maxTextLength) == 0)
  {
    currentTextTemp[0] = '\0';
  }
  result = currentTextTemp;

  return result;
}

static unsigned int GetDlgItemBin(HWND hwnd, int controlID)
{
  unsigned int value = 0;

  const unsigned int maxTextLength = 1024;
  char currentTextTemp[maxTextLength];
  if (GetDlgItemText(hwnd, controlID, currentTextTemp, maxTextLength) == 0)
  {
    currentTextTemp[0] = L'\0';
  }
  std::stringstream buffer;
  buffer << currentTextTemp;
  buffer >> value;

  return value;
}

static double GetDlgItemDouble(HWND hwnd, int controlID)
{
  double value = 0;

  const unsigned int maxTextLength = 1024;
  char currentTextTemp[maxTextLength];
  if (GetDlgItemText(hwnd, controlID, currentTextTemp, maxTextLength) == 0)
  {
    currentTextTemp[0] = L'\0';
  }
  std::stringstream buffer;
  buffer << currentTextTemp;
  buffer >> value;

  return value;
}

static void UpdateDlgItemHex(HWND hwnd, int controlID, unsigned int width, unsigned int data)
{
  const unsigned int maxTextLength = 1024;
  char currentTextTemp[maxTextLength];
  if (GetDlgItemText(hwnd, controlID, currentTextTemp, maxTextLength) == 0)
  {
    currentTextTemp[0] = '\0';
  }
  std::string currentText = currentTextTemp;
  std::stringstream text;
  text << std::setw(width) << std::setfill('0') << std::hex << std::uppercase;
  text << data;
  if (text.str() != currentText)
  {
    SetDlgItemText(hwnd, controlID, text.str().c_str());
  }
}

static void UpdateDlgItemFloat(HWND hwnd, int controlID, float data)
{
  const unsigned int maxTextLength = 1024;
  char currentTextTemp[maxTextLength];
  if (GetDlgItemText(hwnd, controlID, currentTextTemp, maxTextLength) == 0)
  {
    currentTextTemp[0] = L'\0';
  }
  std::string currentText = currentTextTemp;
  std::stringstream text;
  //	text << std::fixed << std::setprecision(10);
  text << std::setprecision(10);
  text << data;
  if (text.str() != currentText)
  {
    SetDlgItemText(hwnd, controlID, text.str().c_str());
  }
}

static void UpdateDlgItemBin(HWND hwnd, int controlID, unsigned int data)
{
  const unsigned int maxTextLength = 1024;
  char currentTextTemp[maxTextLength];
  if (GetDlgItemText(hwnd, controlID, currentTextTemp, maxTextLength) == 0)
  {
    currentTextTemp[0] = L'\0';
  }
  std::string currentText = currentTextTemp;
  std::stringstream text;
  text << data;
  if (text.str() != currentText)
  {
    SetDlgItemText(hwnd, controlID, text.str().c_str());
  }
}

static void UpdateDlgItemString(HWND hwnd, int controlID, const std::string& data)
{
  SetDlgItemText(hwnd, controlID, data.c_str());
}

static void WndProcDialogImplementSaveFieldWhenLostFocus(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
  switch (msg)
  {
    //Make sure no textbox is selected on startup, and remove focus from textboxes when
    //the user clicks an unused area of the window.
  case WM_LBUTTONDOWN:
  case WM_SHOWWINDOW:
    SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(0, EN_SETFOCUS), NULL);
    SetFocus(NULL);
    break;
  }
}

//----------------------------------------------------------------------------------------
//Event handlers
//----------------------------------------------------------------------------------------
static INT_PTR YM2612_msgWM_INITDIALOG(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
  //Set the channel select radio buttons to their default state
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_CS1, BST_CHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_CS2, BST_CHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_CS3, BST_CHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_CS4, BST_CHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_CS5, BST_CHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_CS6, BST_CHECKED);

  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_CS1_CHK, (selectedChannel == YM2612Channels::CHANNEL1) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_CS2_CHK, (selectedChannel == YM2612Channels::CHANNEL2) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_CS3_CHK, (selectedChannel == YM2612Channels::CHANNEL3) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_CS4_CHK, (selectedChannel == YM2612Channels::CHANNEL4) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_CS5_CHK, (selectedChannel == YM2612Channels::CHANNEL5) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_CS6_CHK, (selectedChannel == YM2612Channels::CHANNEL6) ? BST_CHECKED : BST_UNCHECKED);

  for (auto i = 0; i < channelCount; ++i) {
    enabled_channels[i] = 1;
  }

  HWND hVol1_PSG = GetDlgItem(hwnd, IDC_PSG_SLIDER_1);
  HWND hVol2_PSG = GetDlgItem(hwnd, IDC_PSG_SLIDER_2);
  HWND hVol3_PSG = GetDlgItem(hwnd, IDC_PSG_SLIDER_3);
  HWND hVol4_PSG = GetDlgItem(hwnd, IDC_PSG_SLIDER_4);

  SendMessage(hVol1_PSG, PBM_SETPOS, (WPARAM)0, (LPARAM)0);
  SendMessage(hVol2_PSG, PBM_SETPOS, (WPARAM)0, (LPARAM)0);
  SendMessage(hVol3_PSG, PBM_SETPOS, (WPARAM)0, (LPARAM)0);
  SendMessage(hVol4_PSG, PBM_SETPOS, (WPARAM)0, (LPARAM)0);

  return TRUE;
}

static void update_channels() {
  for (auto chn = 0; chn < channelCount; ++chn) {
    if (!enabled_channels[chn]) {
      continue;
    }

    auto ch = static_cast<YM2612Channels>(chn);

    for (auto opr = 0; opr < operatorCount; ++opr) {
      auto op = static_cast<YM2612Operators>(opr);

      SetReleaseRateData(ch, op, 0x0F);

      SetKeyState(ch, op, false);

      SetTotalLevelData(ch, op, 127);
    }
  }
}

static INT_PTR YM2612_msgWM_COMMAND(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
  YM2612Channels channelNo = selectedChannel;

  if (HIWORD(wparam) == BN_CLICKED)
  {
    unsigned int controlID = LOWORD(wparam);
    switch (controlID)
    {
    //Channel Select
    case IDC_YM2612_DEBUGGER_CS1_CHK: {
      selectedChannel = YM2612Channels::CHANNEL1;
      InvalidateRect(hwnd, NULL, FALSE);
      break; }
    case IDC_YM2612_DEBUGGER_CS2_CHK: {
      selectedChannel = YM2612Channels::CHANNEL2;
      InvalidateRect(hwnd, NULL, FALSE);
      break; }
    case IDC_YM2612_DEBUGGER_CS3_CHK: {
      selectedChannel = YM2612Channels::CHANNEL3;
      InvalidateRect(hwnd, NULL, FALSE);
      break; }
    case IDC_YM2612_DEBUGGER_CS4_CHK: {
      selectedChannel = YM2612Channels::CHANNEL4;
      InvalidateRect(hwnd, NULL, FALSE);
      break; }
    case IDC_YM2612_DEBUGGER_CS5_CHK: {
      selectedChannel = YM2612Channels::CHANNEL5;
      InvalidateRect(hwnd, NULL, FALSE);
      break; }
    case IDC_YM2612_DEBUGGER_CS6_CHK: {
      selectedChannel = YM2612Channels::CHANNEL6;
      InvalidateRect(hwnd, NULL, FALSE);
      break; }

    //Disable/enable channels
    case IDC_YM2612_DEBUGGER_CS1: {
      selectedChannel = YM2612Channels::CHANNEL1;
      enabled_channels[0] = IsDlgButtonChecked(hwnd, controlID) == BST_CHECKED;

      update_channels();
      
      InvalidateRect(hwnd, NULL, FALSE);
      break; }
    case IDC_YM2612_DEBUGGER_CS2: {
      selectedChannel = YM2612Channels::CHANNEL2;
      enabled_channels[1] = IsDlgButtonChecked(hwnd, controlID) == BST_CHECKED;

      update_channels();

      InvalidateRect(hwnd, NULL, FALSE);
      break; }
    case IDC_YM2612_DEBUGGER_CS3: {
      selectedChannel = YM2612Channels::CHANNEL3;
      enabled_channels[2] = IsDlgButtonChecked(hwnd, controlID) == BST_CHECKED;

      update_channels();

      InvalidateRect(hwnd, NULL, FALSE);
      break; }
    case IDC_YM2612_DEBUGGER_CS4: {
      selectedChannel = YM2612Channels::CHANNEL4;
      enabled_channels[3] = IsDlgButtonChecked(hwnd, controlID) == BST_CHECKED;

      update_channels();

      InvalidateRect(hwnd, NULL, FALSE);
      break; }
    case IDC_YM2612_DEBUGGER_CS5: {
      selectedChannel = YM2612Channels::CHANNEL5;
      enabled_channels[4] = IsDlgButtonChecked(hwnd, controlID) == BST_CHECKED;

      update_channels();

      InvalidateRect(hwnd, NULL, FALSE);
      break; }
    case IDC_YM2612_DEBUGGER_CS6: {
      selectedChannel = YM2612Channels::CHANNEL6;
      enabled_channels[5] = IsDlgButtonChecked(hwnd, controlID) == BST_CHECKED;

      update_channels();

      InvalidateRect(hwnd, NULL, FALSE);
      break; }

                                //AM Enable
    case IDC_YM2612_DEBUGGER_AM_OP1: {
      SetAmplitudeModulationEnabled(channelNo, YM2612Operators::OPERATOR1, IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED);
      break; }
    case IDC_YM2612_DEBUGGER_AM_OP2: {
      SetAmplitudeModulationEnabled(channelNo, YM2612Operators::OPERATOR2, IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED);
      break; }
    case IDC_YM2612_DEBUGGER_AM_OP3: {
      SetAmplitudeModulationEnabled(channelNo, YM2612Operators::OPERATOR3, IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED);
      break; }
    case IDC_YM2612_DEBUGGER_AM_OP4: {
      SetAmplitudeModulationEnabled(channelNo, YM2612Operators::OPERATOR4, IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED);
      break; }

                                   //Left/Right
    case IDC_YM2612_DEBUGGER_LEFT: {
      SetOutputLeft(channelNo, IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED);
      break; }
    case IDC_YM2612_DEBUGGER_RIGHT: {
      SetOutputRight(channelNo, IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED);
      break; }

                                  //Timers
    case IDC_YM2612_DEBUGGER_TIMERA_ENABLED: {
      SetTimerAEnable(IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED);
      break; }
    case IDC_YM2612_DEBUGGER_TIMERB_ENABLED: {
      SetTimerBEnable(IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED);
      break; }
    case IDC_YM2612_DEBUGGER_TIMERA_LOADED: {
      SetTimerALoad(IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED);
      break; }
    case IDC_YM2612_DEBUGGER_TIMERB_LOADED: {
      SetTimerBLoad(IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED);
      break; }
    case IDC_YM2612_DEBUGGER_TIMERA_OVERFLOW: {
      SetTimerAOverflow(IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED);
      break; }
    case IDC_YM2612_DEBUGGER_TIMERB_OVERFLOW: {
      SetTimerBOverflow(IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED);
      break; }

                                            //LFO
    case IDC_YM2612_DEBUGGER_LFOENABLED: {
      SetLFOEnabled(IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED);
      break; }

                                       //DAC
    case IDC_YM2612_DEBUGGER_DACENABLED: {
      SetDACEnabled(IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED);
      break; }

                                       //Key On/Off
    case IDC_YM2612_DEBUGGER_KEY_11: {
      SetKeyState(YM2612Channels::CHANNEL1, YM2612Operators::OPERATOR1, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    case IDC_YM2612_DEBUGGER_KEY_12: {
      SetKeyState(YM2612Channels::CHANNEL1, YM2612Operators::OPERATOR2, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    case IDC_YM2612_DEBUGGER_KEY_13: {
      SetKeyState(YM2612Channels::CHANNEL1, YM2612Operators::OPERATOR3, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    case IDC_YM2612_DEBUGGER_KEY_14: {
      SetKeyState(YM2612Channels::CHANNEL1, YM2612Operators::OPERATOR4, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    case IDC_YM2612_DEBUGGER_KEY_21: {
      SetKeyState(YM2612Channels::CHANNEL2, YM2612Operators::OPERATOR1, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    case IDC_YM2612_DEBUGGER_KEY_22: {
      SetKeyState(YM2612Channels::CHANNEL2, YM2612Operators::OPERATOR2, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    case IDC_YM2612_DEBUGGER_KEY_23: {
      SetKeyState(YM2612Channels::CHANNEL2, YM2612Operators::OPERATOR3, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    case IDC_YM2612_DEBUGGER_KEY_24: {
      SetKeyState(YM2612Channels::CHANNEL2, YM2612Operators::OPERATOR4, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    case IDC_YM2612_DEBUGGER_KEY_31: {
      SetKeyState(YM2612Channels::CHANNEL3, YM2612Operators::OPERATOR1, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    case IDC_YM2612_DEBUGGER_KEY_32: {
      SetKeyState(YM2612Channels::CHANNEL3, YM2612Operators::OPERATOR2, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    case IDC_YM2612_DEBUGGER_KEY_33: {
      SetKeyState(YM2612Channels::CHANNEL3, YM2612Operators::OPERATOR3, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    case IDC_YM2612_DEBUGGER_KEY_34: {
      SetKeyState(YM2612Channels::CHANNEL3, YM2612Operators::OPERATOR4, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    case IDC_YM2612_DEBUGGER_KEY_41: {
      SetKeyState(YM2612Channels::CHANNEL4, YM2612Operators::OPERATOR1, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    case IDC_YM2612_DEBUGGER_KEY_42: {
      SetKeyState(YM2612Channels::CHANNEL4, YM2612Operators::OPERATOR2, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    case IDC_YM2612_DEBUGGER_KEY_43: {
      SetKeyState(YM2612Channels::CHANNEL4, YM2612Operators::OPERATOR3, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    case IDC_YM2612_DEBUGGER_KEY_44: {
      SetKeyState(YM2612Channels::CHANNEL4, YM2612Operators::OPERATOR4, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    case IDC_YM2612_DEBUGGER_KEY_51: {
      SetKeyState(YM2612Channels::CHANNEL5, YM2612Operators::OPERATOR1, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    case IDC_YM2612_DEBUGGER_KEY_52: {
      SetKeyState(YM2612Channels::CHANNEL5, YM2612Operators::OPERATOR2, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    case IDC_YM2612_DEBUGGER_KEY_53: {
      SetKeyState(YM2612Channels::CHANNEL5, YM2612Operators::OPERATOR3, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    case IDC_YM2612_DEBUGGER_KEY_54: {
      SetKeyState(YM2612Channels::CHANNEL5, YM2612Operators::OPERATOR4, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    case IDC_YM2612_DEBUGGER_KEY_61: {
      SetKeyState(YM2612Channels::CHANNEL6, YM2612Operators::OPERATOR1, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    case IDC_YM2612_DEBUGGER_KEY_62: {
      SetKeyState(YM2612Channels::CHANNEL6, YM2612Operators::OPERATOR2, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    case IDC_YM2612_DEBUGGER_KEY_63: {
      SetKeyState(YM2612Channels::CHANNEL6, YM2612Operators::OPERATOR3, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    case IDC_YM2612_DEBUGGER_KEY_64: {
      SetKeyState(YM2612Channels::CHANNEL6, YM2612Operators::OPERATOR4, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }

    case IDC_YM2612_DEBUGGER_KEY_1: {
      SetKeyState(YM2612Channels::CHANNEL1, YM2612Operators::OPERATOR1, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      SetKeyState(YM2612Channels::CHANNEL1, YM2612Operators::OPERATOR2, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      SetKeyState(YM2612Channels::CHANNEL1, YM2612Operators::OPERATOR3, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      SetKeyState(YM2612Channels::CHANNEL1, YM2612Operators::OPERATOR4, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    case IDC_YM2612_DEBUGGER_KEY_2: {
      SetKeyState(YM2612Channels::CHANNEL2, YM2612Operators::OPERATOR1, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      SetKeyState(YM2612Channels::CHANNEL2, YM2612Operators::OPERATOR2, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      SetKeyState(YM2612Channels::CHANNEL2, YM2612Operators::OPERATOR3, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      SetKeyState(YM2612Channels::CHANNEL2, YM2612Operators::OPERATOR4, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    case IDC_YM2612_DEBUGGER_KEY_3: {
      SetKeyState(YM2612Channels::CHANNEL3, YM2612Operators::OPERATOR1, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      SetKeyState(YM2612Channels::CHANNEL3, YM2612Operators::OPERATOR2, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      SetKeyState(YM2612Channels::CHANNEL3, YM2612Operators::OPERATOR3, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      SetKeyState(YM2612Channels::CHANNEL3, YM2612Operators::OPERATOR4, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    case IDC_YM2612_DEBUGGER_KEY_4: {
      SetKeyState(YM2612Channels::CHANNEL4, YM2612Operators::OPERATOR1, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      SetKeyState(YM2612Channels::CHANNEL4, YM2612Operators::OPERATOR2, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      SetKeyState(YM2612Channels::CHANNEL4, YM2612Operators::OPERATOR3, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      SetKeyState(YM2612Channels::CHANNEL4, YM2612Operators::OPERATOR4, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    case IDC_YM2612_DEBUGGER_KEY_5: {
      SetKeyState(YM2612Channels::CHANNEL5, YM2612Operators::OPERATOR1, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      SetKeyState(YM2612Channels::CHANNEL5, YM2612Operators::OPERATOR2, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      SetKeyState(YM2612Channels::CHANNEL5, YM2612Operators::OPERATOR3, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      SetKeyState(YM2612Channels::CHANNEL5, YM2612Operators::OPERATOR4, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    case IDC_YM2612_DEBUGGER_KEY_6: {
      SetKeyState(YM2612Channels::CHANNEL6, YM2612Operators::OPERATOR1, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      SetKeyState(YM2612Channels::CHANNEL6, YM2612Operators::OPERATOR2, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      SetKeyState(YM2612Channels::CHANNEL6, YM2612Operators::OPERATOR3, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      SetKeyState(YM2612Channels::CHANNEL6, YM2612Operators::OPERATOR4, (IsDlgButtonChecked(hwnd, LOWORD(wparam)) == BST_CHECKED));
      break; }
    }
  }
  else if (HIWORD(wparam) == EN_SETFOCUS)
  {
    previousText = GetDlgItemString(hwnd, LOWORD(wparam));
    currentControlFocus = LOWORD(wparam);
    return FALSE;
  }
  else if (HIWORD(wparam) == EN_KILLFOCUS)
  {
    std::string newText = GetDlgItemString(hwnd, LOWORD(wparam));
    if (currentControlFocus == LOWORD(wparam))
    {
      currentControlFocus = 0;
    }
    if (newText == previousText)
    {
      return FALSE;
    }

    unsigned int controlID = LOWORD(wparam);
    switch (controlID)
    {
      //Total Level
    case IDC_YM2612_DEBUGGER_TL_OP1: {
      SetTotalLevelData(channelNo, YM2612Operators::OPERATOR1, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_TL_OP2: {
      SetTotalLevelData(channelNo, YM2612Operators::OPERATOR2, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_TL_OP3: {
      SetTotalLevelData(channelNo, YM2612Operators::OPERATOR3, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_TL_OP4: {
      SetTotalLevelData(channelNo, YM2612Operators::OPERATOR4, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }

                                   //Sustain Level
    case IDC_YM2612_DEBUGGER_SL_OP1: {
      SetSustainLevelData(channelNo, YM2612Operators::OPERATOR1, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_SL_OP2: {
      SetSustainLevelData(channelNo, YM2612Operators::OPERATOR2, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_SL_OP3: {
      SetSustainLevelData(channelNo, YM2612Operators::OPERATOR3, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_SL_OP4: {
      SetSustainLevelData(channelNo, YM2612Operators::OPERATOR4, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }

                                   //Attack Rate
    case IDC_YM2612_DEBUGGER_AR_OP1: {
      SetAttackRateData(channelNo, YM2612Operators::OPERATOR1, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_AR_OP2: {
      SetAttackRateData(channelNo, YM2612Operators::OPERATOR2, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_AR_OP3: {
      SetAttackRateData(channelNo, YM2612Operators::OPERATOR3, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_AR_OP4: {
      SetAttackRateData(channelNo, YM2612Operators::OPERATOR4, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }

                                   //Decay Rate
    case IDC_YM2612_DEBUGGER_DR_OP1: {
      SetDecayRateData(channelNo, YM2612Operators::OPERATOR1, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_DR_OP2: {
      SetDecayRateData(channelNo, YM2612Operators::OPERATOR2, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_DR_OP3: {
      SetDecayRateData(channelNo, YM2612Operators::OPERATOR3, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_DR_OP4: {
      SetDecayRateData(channelNo, YM2612Operators::OPERATOR4, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }

                                   //Sustain Rate
    case IDC_YM2612_DEBUGGER_SR_OP1: {
      SetSustainRateData(channelNo, YM2612Operators::OPERATOR1, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_SR_OP2: {
      SetSustainRateData(channelNo, YM2612Operators::OPERATOR2, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_SR_OP3: {
      SetSustainRateData(channelNo, YM2612Operators::OPERATOR3, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_SR_OP4: {
      SetSustainRateData(channelNo, YM2612Operators::OPERATOR4, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }

                                   //Release Rate
    case IDC_YM2612_DEBUGGER_RR_OP1: {
      SetReleaseRateData(channelNo, YM2612Operators::OPERATOR1, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_RR_OP2: {
      SetReleaseRateData(channelNo, YM2612Operators::OPERATOR2, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_RR_OP3: {
      SetReleaseRateData(channelNo, YM2612Operators::OPERATOR3, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_RR_OP4: {
      SetReleaseRateData(channelNo, YM2612Operators::OPERATOR4, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }

                                   //SSG-EG Mode
    case IDC_YM2612_DEBUGGER_SSGEG_OP1: {
      SetSSGData(channelNo, YM2612Operators::OPERATOR1, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_SSGEG_OP2: {
      SetSSGData(channelNo, YM2612Operators::OPERATOR2, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_SSGEG_OP3: {
      SetSSGData(channelNo, YM2612Operators::OPERATOR3, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_SSGEG_OP4: {
      SetSSGData(channelNo, YM2612Operators::OPERATOR4, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }

                                      //Detune
    case IDC_YM2612_DEBUGGER_DT_OP1: {
      SetDetuneData(channelNo, YM2612Operators::OPERATOR1, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_DT_OP2: {
      SetDetuneData(channelNo, YM2612Operators::OPERATOR2, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_DT_OP3: {
      SetDetuneData(channelNo, YM2612Operators::OPERATOR3, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_DT_OP4: {
      SetDetuneData(channelNo, YM2612Operators::OPERATOR4, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }

                                   //Multiple
    case IDC_YM2612_DEBUGGER_MUL_OP1: {
      SetMultipleData(channelNo, YM2612Operators::OPERATOR1, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_MUL_OP2: {
      SetMultipleData(channelNo, YM2612Operators::OPERATOR2, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_MUL_OP3: {
      SetMultipleData(channelNo, YM2612Operators::OPERATOR3, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_MUL_OP4: {
      SetMultipleData(channelNo, YM2612Operators::OPERATOR4, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }

                                    //Key Scale
    case IDC_YM2612_DEBUGGER_KS_OP1: {
      SetKeyScaleData(channelNo, YM2612Operators::OPERATOR1, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_KS_OP2: {
      SetKeyScaleData(channelNo, YM2612Operators::OPERATOR2, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_KS_OP3: {
      SetKeyScaleData(channelNo, YM2612Operators::OPERATOR3, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_KS_OP4: {
      SetKeyScaleData(channelNo, YM2612Operators::OPERATOR4, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }

                                   //Channel Registers
    case IDC_YM2612_DEBUGGER_ALGORITHM: {
      SetAlgorithmData(channelNo, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_FEEDBACK: {
      SetFeedbackData(channelNo, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_FNUM: {
      SetFrequencyData(channelNo, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_BLOCK: {
      SetBlockData(channelNo, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_AMS: {
      SetAMSData(channelNo, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_PMS: {
      SetPMSData(channelNo, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }

                                //Channel 3 Frequency
    case IDC_YM2612_DEBUGGER_CH3MODE: {
      SetCH3Mode(GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_CH3FNUM_OP1: {
      SetFrequencyDataChannel3(YM2612Operators::OPERATOR1, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_CH3FNUM_OP2: {
      SetFrequencyDataChannel3(YM2612Operators::OPERATOR2, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_CH3FNUM_OP3: {
      SetFrequencyDataChannel3(YM2612Operators::OPERATOR3, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_CH3FNUM_OP4: {
      SetFrequencyDataChannel3(YM2612Operators::OPERATOR4, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_CH3BLOCK_OP1: {
      SetBlockDataChannel3(YM2612Operators::OPERATOR1, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_CH3BLOCK_OP2: {
      SetBlockDataChannel3(YM2612Operators::OPERATOR2, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_CH3BLOCK_OP3: {
      SetBlockDataChannel3(YM2612Operators::OPERATOR3, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_CH3BLOCK_OP4: {
      SetBlockDataChannel3(YM2612Operators::OPERATOR4, GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }

                                        //LFO
    case IDC_YM2612_DEBUGGER_LFOFREQ: {
      SetLFOData(GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }

                                    //DAC
    case IDC_YM2612_DEBUGGER_DACDATA: {
      SetDACData(GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    }

    return FALSE;
  }

  return TRUE;
}

static INT_PTR YM2612_Debugger_Update(HWND hwnd)
{
  YM2612Channels channelNo = selectedChannel;

  //Total Level
  if (currentControlFocus != IDC_YM2612_DEBUGGER_TL_OP1)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_TL_OP1, 2, GetTotalLevelData(channelNo, YM2612Operators::OPERATOR1));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_TL_OP2)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_TL_OP2, 2, GetTotalLevelData(channelNo, YM2612Operators::OPERATOR2));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_TL_OP3)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_TL_OP3, 2, GetTotalLevelData(channelNo, YM2612Operators::OPERATOR3));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_TL_OP4)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_TL_OP4, 2, GetTotalLevelData(channelNo, YM2612Operators::OPERATOR4));
  }

  //Sustain Level
  if (currentControlFocus != IDC_YM2612_DEBUGGER_SL_OP1)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_SL_OP1, 1, GetSustainLevelData(channelNo, YM2612Operators::OPERATOR1));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_SL_OP2)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_SL_OP2, 1, GetSustainLevelData(channelNo, YM2612Operators::OPERATOR2));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_SL_OP3)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_SL_OP3, 1, GetSustainLevelData(channelNo, YM2612Operators::OPERATOR3));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_SL_OP4)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_SL_OP4, 1, GetSustainLevelData(channelNo, YM2612Operators::OPERATOR4));
  }

  //Attack Rate
  if (currentControlFocus != IDC_YM2612_DEBUGGER_AR_OP1)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_AR_OP1, 2, GetAttackRateData(channelNo, YM2612Operators::OPERATOR1));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_AR_OP2)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_AR_OP2, 2, GetAttackRateData(channelNo, YM2612Operators::OPERATOR2));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_AR_OP3)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_AR_OP3, 2, GetAttackRateData(channelNo, YM2612Operators::OPERATOR3));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_AR_OP4)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_AR_OP4, 2, GetAttackRateData(channelNo, YM2612Operators::OPERATOR4));
  }

  //Decay Rate
  if (currentControlFocus != IDC_YM2612_DEBUGGER_DR_OP1)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_DR_OP1, 2, GetDecayRateData(channelNo, YM2612Operators::OPERATOR1));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_DR_OP2)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_DR_OP2, 2, GetDecayRateData(channelNo, YM2612Operators::OPERATOR2));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_DR_OP3)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_DR_OP3, 2, GetDecayRateData(channelNo, YM2612Operators::OPERATOR3));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_DR_OP4)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_DR_OP4, 2, GetDecayRateData(channelNo, YM2612Operators::OPERATOR4));
  }

  //Sustain Rate
  if (currentControlFocus != IDC_YM2612_DEBUGGER_SR_OP1)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_SR_OP1, 2, GetSustainRateData(channelNo, YM2612Operators::OPERATOR1));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_SR_OP2)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_SR_OP2, 2, GetSustainRateData(channelNo, YM2612Operators::OPERATOR2));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_SR_OP3)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_SR_OP3, 2, GetSustainRateData(channelNo, YM2612Operators::OPERATOR3));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_SR_OP4)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_SR_OP4, 2, GetSustainRateData(channelNo, YM2612Operators::OPERATOR4));
  }

  //Release Rate
  if (currentControlFocus != IDC_YM2612_DEBUGGER_RR_OP1)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_RR_OP1, 1, GetReleaseRateData(channelNo, YM2612Operators::OPERATOR1));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_RR_OP2)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_RR_OP2, 1, GetReleaseRateData(channelNo, YM2612Operators::OPERATOR2));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_RR_OP3)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_RR_OP3, 1, GetReleaseRateData(channelNo, YM2612Operators::OPERATOR3));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_RR_OP4)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_RR_OP4, 1, GetReleaseRateData(channelNo, YM2612Operators::OPERATOR4));
  }

  //SSG-EG Mode
  if (currentControlFocus != IDC_YM2612_DEBUGGER_SSGEG_OP1)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_SSGEG_OP1, 1, GetSSGData(channelNo, YM2612Operators::OPERATOR1));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_SSGEG_OP2)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_SSGEG_OP2, 1, GetSSGData(channelNo, YM2612Operators::OPERATOR2));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_SSGEG_OP3)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_SSGEG_OP3, 1, GetSSGData(channelNo, YM2612Operators::OPERATOR3));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_SSGEG_OP4)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_SSGEG_OP4, 1, GetSSGData(channelNo, YM2612Operators::OPERATOR4));
  }

  //Detune
  if (currentControlFocus != IDC_YM2612_DEBUGGER_DT_OP1)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_DT_OP1, 1, GetDetuneData(channelNo, YM2612Operators::OPERATOR1));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_DT_OP2)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_DT_OP2, 1, GetDetuneData(channelNo, YM2612Operators::OPERATOR2));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_DT_OP3)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_DT_OP3, 1, GetDetuneData(channelNo, YM2612Operators::OPERATOR3));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_DT_OP4)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_DT_OP4, 1, GetDetuneData(channelNo, YM2612Operators::OPERATOR4));
  }

  //Multiple
  if (currentControlFocus != IDC_YM2612_DEBUGGER_MUL_OP1)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_MUL_OP1, 1, GetMultipleData(channelNo, YM2612Operators::OPERATOR1));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_MUL_OP2)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_MUL_OP2, 1, GetMultipleData(channelNo, YM2612Operators::OPERATOR2));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_MUL_OP3)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_MUL_OP3, 1, GetMultipleData(channelNo, YM2612Operators::OPERATOR3));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_MUL_OP4)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_MUL_OP4, 1, GetMultipleData(channelNo, YM2612Operators::OPERATOR4));
  }

  //Key Scale
  if (currentControlFocus != IDC_YM2612_DEBUGGER_KS_OP1)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_KS_OP1, 1, GetKeyScaleData(channelNo, YM2612Operators::OPERATOR1));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_KS_OP2)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_KS_OP2, 1, GetKeyScaleData(channelNo, YM2612Operators::OPERATOR2));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_KS_OP3)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_KS_OP3, 1, GetKeyScaleData(channelNo, YM2612Operators::OPERATOR3));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_KS_OP4)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_KS_OP4, 1, GetKeyScaleData(channelNo, YM2612Operators::OPERATOR4));
  }

  //AM Enable
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_AM_OP1, (GetAmplitudeModulationEnabled(channelNo, YM2612Operators::OPERATOR1)) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_AM_OP2, (GetAmplitudeModulationEnabled(channelNo, YM2612Operators::OPERATOR2)) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_AM_OP3, (GetAmplitudeModulationEnabled(channelNo, YM2612Operators::OPERATOR3)) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_AM_OP4, (GetAmplitudeModulationEnabled(channelNo, YM2612Operators::OPERATOR4)) ? BST_CHECKED : BST_UNCHECKED);

  //Channel Registers
  if (currentControlFocus != IDC_YM2612_DEBUGGER_ALGORITHM)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_ALGORITHM, 1, GetAlgorithmData(channelNo));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_FEEDBACK)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_FEEDBACK, 1, GetFeedbackData(channelNo));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_FNUM)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_FNUM, 3, GetFrequencyData(channelNo));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_BLOCK)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_BLOCK, 1, GetBlockData(channelNo));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_AMS)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_AMS, 1, GetAMSData(channelNo));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_PMS)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_PMS, 1, GetPMSData(channelNo));
  }
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_LEFT, (GetOutputLeft(channelNo)) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_RIGHT, (GetOutputRight(channelNo)) ? BST_CHECKED : BST_UNCHECKED);

  //Channel 3 Frequency
  if (currentControlFocus != IDC_YM2612_DEBUGGER_CH3MODE)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_CH3MODE, 1, GetCH3Mode());
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_CH3FNUM_OP1)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_CH3FNUM_OP1, 3, GetFrequencyDataChannel3(YM2612Operators::OPERATOR1));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_CH3FNUM_OP2)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_CH3FNUM_OP2, 3, GetFrequencyDataChannel3(YM2612Operators::OPERATOR2));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_CH3FNUM_OP3)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_CH3FNUM_OP3, 3, GetFrequencyDataChannel3(YM2612Operators::OPERATOR3));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_CH3FNUM_OP4)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_CH3FNUM_OP4, 3, GetFrequencyDataChannel3(YM2612Operators::OPERATOR4));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_CH3BLOCK_OP1)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_CH3BLOCK_OP1, 1, GetBlockDataChannel3(YM2612Operators::OPERATOR1));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_CH3BLOCK_OP2)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_CH3BLOCK_OP2, 1, GetBlockDataChannel3(YM2612Operators::OPERATOR2));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_CH3BLOCK_OP3)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_CH3BLOCK_OP3, 1, GetBlockDataChannel3(YM2612Operators::OPERATOR3));
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_CH3BLOCK_OP4)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_CH3BLOCK_OP4, 1, GetBlockDataChannel3(YM2612Operators::OPERATOR4));
  }

  //Timers
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_TIMERA_ENABLED, (GetTimerAEnable()) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_TIMERB_ENABLED, (GetTimerBEnable()) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_TIMERA_LOADED, (GetTimerALoad()) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_TIMERB_LOADED, (GetTimerBLoad()) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_TIMERA_OVERFLOW, (GetTimerAOverflow()) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_TIMERB_OVERFLOW, (GetTimerBOverflow()) ? BST_CHECKED : BST_UNCHECKED);

  //LFO
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_LFOENABLED, (GetLFOEnabled()) ? BST_CHECKED : BST_UNCHECKED);
  if (currentControlFocus != IDC_YM2612_DEBUGGER_LFOFREQ)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_LFOFREQ, 2, GetLFOData());
  }
  std::string lfoFrequencyText;
  //##TODO## Update these to clock-independent calculations
  switch (GetLFOData())
  {
  case 0:
    lfoFrequencyText = "3.98";
    break;
  case 1:
    lfoFrequencyText = "5.56";
    break;
  case 2:
    lfoFrequencyText = "6.02";
    break;
  case 3:
    lfoFrequencyText = "6.37";
    break;
  case 4:
    lfoFrequencyText = "6.88";
    break;
  case 5:
    lfoFrequencyText = "9.63";
    break;
  case 6:
    lfoFrequencyText = "48.1";
    break;
  case 7:
    lfoFrequencyText = "72.2";
    break;
  }
  UpdateDlgItemString(hwnd, IDC_YM2612_DEBUGGER_LFOFREQ2, lfoFrequencyText);

  //DAC
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_DACENABLED, (GetDACEnabled()) ? BST_CHECKED : BST_UNCHECKED);
  if (currentControlFocus != IDC_YM2612_DEBUGGER_DACDATA)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_DACDATA, 2, GetDACData());
  }

  //Key On/Off
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_11, GetKeyState(YM2612Channels::CHANNEL1, YM2612Operators::OPERATOR1) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_12, GetKeyState(YM2612Channels::CHANNEL1, YM2612Operators::OPERATOR2) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_13, GetKeyState(YM2612Channels::CHANNEL1, YM2612Operators::OPERATOR3) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_14, GetKeyState(YM2612Channels::CHANNEL1, YM2612Operators::OPERATOR4) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_21, GetKeyState(YM2612Channels::CHANNEL2, YM2612Operators::OPERATOR1) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_22, GetKeyState(YM2612Channels::CHANNEL2, YM2612Operators::OPERATOR2) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_23, GetKeyState(YM2612Channels::CHANNEL2, YM2612Operators::OPERATOR3) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_24, GetKeyState(YM2612Channels::CHANNEL2, YM2612Operators::OPERATOR4) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_31, GetKeyState(YM2612Channels::CHANNEL3, YM2612Operators::OPERATOR1) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_32, GetKeyState(YM2612Channels::CHANNEL3, YM2612Operators::OPERATOR2) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_33, GetKeyState(YM2612Channels::CHANNEL3, YM2612Operators::OPERATOR3) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_34, GetKeyState(YM2612Channels::CHANNEL3, YM2612Operators::OPERATOR4) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_41, GetKeyState(YM2612Channels::CHANNEL4, YM2612Operators::OPERATOR1) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_42, GetKeyState(YM2612Channels::CHANNEL4, YM2612Operators::OPERATOR2) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_43, GetKeyState(YM2612Channels::CHANNEL4, YM2612Operators::OPERATOR3) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_44, GetKeyState(YM2612Channels::CHANNEL4, YM2612Operators::OPERATOR4) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_51, GetKeyState(YM2612Channels::CHANNEL5, YM2612Operators::OPERATOR1) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_52, GetKeyState(YM2612Channels::CHANNEL5, YM2612Operators::OPERATOR2) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_53, GetKeyState(YM2612Channels::CHANNEL5, YM2612Operators::OPERATOR3) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_54, GetKeyState(YM2612Channels::CHANNEL5, YM2612Operators::OPERATOR4) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_61, GetKeyState(YM2612Channels::CHANNEL6, YM2612Operators::OPERATOR1) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_62, GetKeyState(YM2612Channels::CHANNEL6, YM2612Operators::OPERATOR2) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_63, GetKeyState(YM2612Channels::CHANNEL6, YM2612Operators::OPERATOR3) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_64, GetKeyState(YM2612Channels::CHANNEL6, YM2612Operators::OPERATOR4) ? BST_CHECKED : BST_UNCHECKED);

  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_1, (GetKeyState(YM2612Channels::CHANNEL1, YM2612Operators::OPERATOR1) && GetKeyState(YM2612Channels::CHANNEL1, YM2612Operators::OPERATOR2) && GetKeyState(YM2612Channels::CHANNEL1, YM2612Operators::OPERATOR3) && GetKeyState(YM2612Channels::CHANNEL1, YM2612Operators::OPERATOR4)) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_2, (GetKeyState(YM2612Channels::CHANNEL2, YM2612Operators::OPERATOR1) && GetKeyState(YM2612Channels::CHANNEL2, YM2612Operators::OPERATOR2) && GetKeyState(YM2612Channels::CHANNEL2, YM2612Operators::OPERATOR3) && GetKeyState(YM2612Channels::CHANNEL2, YM2612Operators::OPERATOR4)) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_3, (GetKeyState(YM2612Channels::CHANNEL3, YM2612Operators::OPERATOR1) && GetKeyState(YM2612Channels::CHANNEL3, YM2612Operators::OPERATOR2) && GetKeyState(YM2612Channels::CHANNEL3, YM2612Operators::OPERATOR3) && GetKeyState(YM2612Channels::CHANNEL3, YM2612Operators::OPERATOR4)) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_4, (GetKeyState(YM2612Channels::CHANNEL4, YM2612Operators::OPERATOR1) && GetKeyState(YM2612Channels::CHANNEL4, YM2612Operators::OPERATOR2) && GetKeyState(YM2612Channels::CHANNEL4, YM2612Operators::OPERATOR3) && GetKeyState(YM2612Channels::CHANNEL4, YM2612Operators::OPERATOR4)) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_5, (GetKeyState(YM2612Channels::CHANNEL5, YM2612Operators::OPERATOR1) && GetKeyState(YM2612Channels::CHANNEL5, YM2612Operators::OPERATOR2) && GetKeyState(YM2612Channels::CHANNEL5, YM2612Operators::OPERATOR3) && GetKeyState(YM2612Channels::CHANNEL5, YM2612Operators::OPERATOR4)) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_KEY_6, (GetKeyState(YM2612Channels::CHANNEL6, YM2612Operators::OPERATOR1) && GetKeyState(YM2612Channels::CHANNEL6, YM2612Operators::OPERATOR2) && GetKeyState(YM2612Channels::CHANNEL6, YM2612Operators::OPERATOR3) && GetKeyState(YM2612Channels::CHANNEL6, YM2612Operators::OPERATOR4)) ? BST_CHECKED : BST_UNCHECKED);

  char tmp[10];

  int val = PSG.Register[0]==0 ? 0 : (int)(3579545/(PSG.Register[0]*32));
  _itoa(val, tmp, 10);
  SetDlgItemText(hwnd, IDC_PSG_FREQ_1, tmp);

  val = PSG.Register[0];
  _itoa(val, tmp, 10);
  SetDlgItemText(hwnd, IDC_PSG_DATA_1, tmp);

  val = PSG.Register[2] == 0 ? 0 : (int)(3579545 / (PSG.Register[2] * 32));
  _itoa(val, tmp, 10);
  SetDlgItemText(hwnd, IDC_PSG_FREQ_2, tmp);

  val = PSG.Register[2];
  _itoa(val, tmp, 10);
  SetDlgItemText(hwnd, IDC_PSG_DATA_2, tmp);

  val = PSG.Register[4] == 0 ? 0 : (int)(3579545 / (PSG.Register[4] * 32));
  _itoa(val, tmp, 10);
  SetDlgItemText(hwnd, IDC_PSG_FREQ_3, tmp);

  val = PSG.Register[4];
  _itoa(val, tmp, 10);
  SetDlgItemText(hwnd, IDC_PSG_DATA_3, tmp);

  UpdateDlgItemString(hwnd, IDC_PSG_FEEDBACK, (PSG.Register[6]>>2)==1 ? "White": "Periodic");

  if ((PSG.Register[6] & 0x03) == 0){
    UpdateDlgItemString(hwnd, IDC_PSG_CLOCK, "Clock/2");
  }
  else if ((PSG.Register[6] & 0x03) == 0) {
    UpdateDlgItemString(hwnd, IDC_PSG_CLOCK, "Clock/2");
  }
  else if ((PSG.Register[6] & 0x03) == 1) {
    UpdateDlgItemString(hwnd, IDC_PSG_CLOCK, "Clock/4");
  }
  else if ((PSG.Register[6] & 0x03) == 2) {
    UpdateDlgItemString(hwnd, IDC_PSG_CLOCK, "Clock/8");
  }
  else if ((PSG.Register[6] & 0x03) == 3) {
    UpdateDlgItemString(hwnd, IDC_PSG_CLOCK, "Tone 3");
  }

  int pval = (100 * PSG.Volume[0]) / PSG_MaxVolume;
  SendMessage(GetDlgItem(hwnd, IDC_PSG_SLIDER_1), PBM_SETPOS, (WPARAM)pval, (LPARAM)0);

  pval = (100 * PSG.Volume[1]) / PSG_MaxVolume;
  SendMessage(GetDlgItem(hwnd, IDC_PSG_SLIDER_2), PBM_SETPOS, (WPARAM)pval, (LPARAM)0);

  pval = (100 * PSG.Volume[2]) / PSG_MaxVolume;
  SendMessage(GetDlgItem(hwnd, IDC_PSG_SLIDER_3), PBM_SETPOS, (WPARAM)pval, (LPARAM)0);

  pval = (100 * PSG.Volume[3]) / PSG_MaxVolume;
  SendMessage(GetDlgItem(hwnd, IDC_PSG_SLIDER_4), PBM_SETPOS, (WPARAM)pval, (LPARAM)0);

  return TRUE;
}

void Update_YM2612_View()
{
  if (!YM2612DbgHWnd) return;

  YM2612_Debugger_Update(YM2612DbgHWnd);
  RedrawWindow(GetDlgItem(YM2612DbgHWnd, IDC_ADSR_DRAW1), NULL, NULL, RDW_INVALIDATE);
  RedrawWindow(GetDlgItem(YM2612DbgHWnd, IDC_ADSR_DRAW2), NULL, NULL, RDW_INVALIDATE);
  RedrawWindow(GetDlgItem(YM2612DbgHWnd, IDC_ADSR_DRAW3), NULL, NULL, RDW_INVALIDATE);
  RedrawWindow(GetDlgItem(YM2612DbgHWnd, IDC_ADSR_DRAW4), NULL, NULL, RDW_INVALIDATE);
}

const int ADSR_H = 140;
const int ADSR_W = ADSR_H * 2;
const int ADSR_MAX_H = 128;
const int SUSTAIN_X = 31;

static void move_to_point(HDC hdc, int x, int y) {
  MoveToEx(hdc, x, ADSR_H - 1 - y, NULL);
}

static void line_to_point(HDC hdc, int x, int y) {
  LineTo(hdc, x, ADSR_H - 1 - y);
}

static int map_val_y(double val, double max_val) {
  return max_val ? ((val / max_val) * ADSR_H) : 0;
}

static int map_val(double val, double max_val, double out_max) {
  return out_max ? (val * out_max / max_val) : 0;
}

static HPEN t1Pen, t2Pen, rPen, bPen, oPen, gPen, pPen;
static HBRUSH adsrBrush;

LRESULT CALLBACK YM2612WndProcDialog(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
  RECT r, r2;
  int dx1, dy1, dx2, dy2;

  WndProcDialogImplementSaveFieldWhenLostFocus(hwnd, msg, wparam, lparam);
  switch (msg)
  {
  case WM_INITDIALOG: {
    YM2612DbgHWnd = hwnd;

    GetWindowRect(HWnd, &r);
    dx1 = (r.right - r.left) / 2;
    dy1 = (r.bottom - r.top) / 2;

    GetWindowRect(hwnd, &r2);
    dx2 = (r2.right - r2.left) / 2;
    dy2 = (r2.bottom - r2.top) / 2;

    // push it away from the main window if we can
    const int width = (r.right - r.left);
    const int width2 = (r2.right - r2.left);
    if (r.left + width2 + width < GetSystemMetrics(SM_CXSCREEN))
    {
      r.right += width;
      r.left += width;
    }
    else if ((int)r.left - (int)width2 > 0)
    {
      r.right -= width2;
      r.left -= width2;
    }

    SetWindowPos(hwnd, NULL, r.left, r.top, NULL, NULL, SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);

    HWND hOpRegs = GetDlgItem(hwnd, IDC_YM2612_DEBUGGER_OP_REGS_GB);
    GetWindowRect(hOpRegs, &r);
    MapWindowPoints(HWND_DESKTOP, hwnd, (LPPOINT)&r, 2);

    // ADSR views
    for (auto i = 0; i <= (IDC_ADSR_DRAW4 - IDC_ADSR_DRAW1); ++i) {
      HWND adsrDraw = GetDlgItem(hwnd, IDC_ADSR_DRAW1 + i);
      SetWindowPos(adsrDraw, NULL,
        r.right + 5,
        r.top + 5 + (ADSR_H + 5) * i,
        ADSR_W,
        ADSR_H,
        SWP_NOZORDER | SWP_NOACTIVATE);
    }
    // ADSR views

    t1Pen = CreatePen(PS_DOT, 1, RGB(47, 79, 79));
    t2Pen = CreatePen(PS_DOT, 1, RGB(220, 220, 220));
    rPen = CreatePen(PS_SOLID, 1, RGB(255, 0, 0));
    bPen = CreatePen(PS_SOLID, 1, RGB(0, 0, 255));
    oPen = CreatePen(PS_SOLID, 1, RGB(255, 165, 0));
    gPen = CreatePen(PS_SOLID, 1, RGB(0, 255, 0));
    pPen = CreatePen(PS_SOLID, 1, RGB(255, 0, 255));

    adsrBrush = CreateSolidBrush(RGB(105, 105, 105));

    return YM2612_msgWM_INITDIALOG(hwnd, wparam, lparam);
  }
  case WM_COMMAND:
    return YM2612_msgWM_COMMAND(hwnd, wparam, lparam);
  case WM_DRAWITEM:
  {
    LPDRAWITEMSTRUCT di = (LPDRAWITEMSTRUCT)lparam;

    switch ((UINT)wparam) {
    case IDC_ADSR_DRAW1:
    case IDC_ADSR_DRAW2:
    case IDC_ADSR_DRAW3:
    case IDC_ADSR_DRAW4:
    {
      FillRect(di->hDC, &di->rcItem, adsrBrush);

      YM2612Operators op = (YM2612Operators)((UINT)wparam - IDC_ADSR_DRAW1);

      int total = 0x7F - GetTotalLevelData(selectedChannel, op);
      total = map_val(total, 0x7F, ADSR_H-1);

      // draw Total Level
      HPEN hOldPen = (HPEN)SelectObject(di->hDC, t1Pen);
      move_to_point(di->hDC, 0, total);
      line_to_point(di->hDC, ADSR_W-1, total);
      SelectObject(di->hDC, hOldPen);
      // end draw Total Level

      move_to_point(di->hDC, 0, 0);

      int attack_inv = GetAttackRateData(selectedChannel, op);
      int attack = 0x1F - attack_inv;
      int decay_orig = GetDecayRateData(selectedChannel, op);
      int decay = decay_orig;
      int sustain_level = 0x0F - GetSustainLevelData(selectedChannel, op);
      int sustain_rate = GetSustainRateData(selectedChannel, op);
      int release = GetReleaseRateData(selectedChannel, op);
      int max_w = attack + decay + SUSTAIN_X + release;

      attack = map_val(attack, max_w, ADSR_W-1);
      decay = map_val(decay, max_w, ADSR_W-1);
      int sustain_x = map_val(SUSTAIN_X, max_w, ADSR_W-1);
      release = map_val(release, max_w, ADSR_W-1);

      sustain_level = map_val(sustain_level, 0x0F, total);
      sustain_rate = map_val(sustain_rate, 0x1F, sustain_level);

      // draw Attack
      hOldPen = (HPEN)SelectObject(di->hDC, rPen);
      line_to_point(di->hDC, attack, total);

      if (attack_inv < 0x1F) {
        SelectObject(di->hDC, t2Pen);
        line_to_point(di->hDC, attack, 0);
      }

      move_to_point(di->hDC, attack, total);
      SelectObject(di->hDC, hOldPen);
      // end draw Attack

      // draw Decay
      hOldPen = (HPEN)SelectObject(di->hDC, bPen);
      line_to_point(di->hDC, attack + decay, sustain_level);

      if (decay_orig > 0) {
        SelectObject(di->hDC, t2Pen);
        line_to_point(di->hDC, attack + decay, 0);
      }

      move_to_point(di->hDC, attack + decay, sustain_level);
      SelectObject(di->hDC, hOldPen);
      // end draw Decay

      sustain_rate = sustain_level - sustain_rate;

      // draw Sustain
      hOldPen = (HPEN)SelectObject(di->hDC, gPen);
      line_to_point(di->hDC, attack + decay + sustain_x, sustain_rate);

      if (sustain_rate > 0) {
        SelectObject(di->hDC, t2Pen);
        line_to_point(di->hDC, attack + decay + sustain_x, 0);
      }

      move_to_point(di->hDC, attack + decay + sustain_x, sustain_rate);
      SelectObject(di->hDC, hOldPen);
      // end draw Sustain

      // draw Release
      hOldPen = (HPEN)SelectObject(di->hDC, pPen);
      line_to_point(di->hDC, attack + decay + sustain_x + release, 0);
      SelectObject(di->hDC, hOldPen);
      // end draw Releae

      return TRUE;
    }
    }
  }
  case WM_CLOSE:
    DeleteObject(t1Pen);
    DeleteObject(t2Pen);
    DeleteObject(rPen);
    DeleteObject(bPen);
    DeleteObject(oPen);
    DeleteObject(gPen);
    DeleteObject(pPen);

    DialogsOpen--;
    YM2612DbgHWnd = NULL;
    EndDialog(hwnd, true);
    return TRUE;
  }
  return FALSE;
}