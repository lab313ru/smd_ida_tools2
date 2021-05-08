#include "resource.h"
#include <Windows.h>
#include <map>
#include <sstream>
#include <iomanip>

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

typedef std::map<YM2612Operators, bool> OpEnable_t;
typedef std::map<YM2612Operators, unsigned int> OpData_t;

static std::map<YM2612Channels, OpEnable_t> am_enabled;
static std::map<YM2612Channels, bool> left_enabled;
static std::map<YM2612Channels, bool> right_enabled;
static std::map<YM2612Channels, OpEnable_t> key_enabled;
static std::map<YM2612Channels, OpData_t> total_levels_data;
static std::map<YM2612Channels, OpData_t> sustain_levels_data;
static std::map<YM2612Channels, OpData_t> attack_rate_data;
static std::map<YM2612Channels, OpData_t> decay_rate_data;
static std::map<YM2612Channels, OpData_t> sustain_rate_data;
static std::map<YM2612Channels, OpData_t> release_rate_data;
static std::map<YM2612Channels, OpData_t> ssg_data;
static std::map<YM2612Channels, OpData_t> detune_data;
static std::map<YM2612Channels, OpData_t> multiple_data;
static std::map<YM2612Channels, OpData_t> key_scale_data;
static std::map<YM2612Channels, unsigned int> algorithm_data;
static std::map<YM2612Channels, unsigned int> feedback_data;
static std::map<YM2612Channels, unsigned int> frequency_data;
static std::map<YM2612Channels, unsigned int> block_data;
static std::map<YM2612Channels, unsigned int> ams_data;
static std::map<YM2612Channels, unsigned int> pms_data;
static std::map<YM2612Channels, OpData_t> frequency_chn_data;
static std::map<YM2612Channels, OpData_t> block_chn_data;

static bool timer_a_enabled;
static bool timer_b_enabled;
static bool timer_a_load;
static bool timer_b_load;
static bool timer_a_overflow;
static bool timer_b_overflow;

static bool lfo_enabled;
static bool dac_enabled;

static unsigned int ch3_mode;
static unsigned int timer_a_data;
static unsigned int timer_b_data;
static unsigned int timer_a_current_counter;
static unsigned int timer_b_current_counter;
static unsigned int lfo_data;
static unsigned int dac_data;

static double external_clock_rate;
static unsigned int fm_clock_divider;
static unsigned int eg_clock_divider;
static unsigned int output_clock_divider;
static unsigned int timer_a_clock_divider;
static unsigned int timer_b_clock_divider;


static std::string previousText;
static unsigned int currentControlFocus;

static inline void SetAmplitudeModulationEnabled(YM2612Channels chn, YM2612Operators op, bool enabled) {
  am_enabled[chn][op] = enabled;
}

static inline bool GetAmplitudeModulationEnabled(YM2612Channels chn, YM2612Operators op) {
  return am_enabled[chn][op];
}

static inline void SetOutputLeft(YM2612Channels chn, bool enabled) {
  left_enabled[chn] = enabled;
}

static inline bool GetOutputLeft(YM2612Channels chn) {
  return left_enabled[chn];
}

static inline void SetOutputRight(YM2612Channels chn, bool enabled) {
  right_enabled[chn] = enabled;
}

static inline bool GetOutputRight(YM2612Channels chn) {
  return right_enabled[chn];
}

static inline void SetTimerAEnable(bool enabled) {
  timer_a_enabled = enabled;
}

static inline bool GetTimerAEnable() {
  return timer_a_enabled;
}

static inline void SetTimerBEnable(bool enabled) {
  timer_b_enabled = enabled;
}

static inline bool GetTimerBEnable() {
  return timer_b_enabled;
}

static inline void SetTimerALoad(bool enabled) {
  timer_a_load = enabled;
}

static inline bool GetTimerALoad() {
  return timer_a_load;
}

static inline void SetTimerBLoad(bool enabled) {
  timer_b_load = enabled;
}

static inline bool GetTimerBLoad() {
  return timer_b_load;
}

static inline void SetTimerAOverflow(bool enabled) {
  timer_a_overflow = enabled;
}

static inline bool GetTimerAOverflow() {
  return timer_a_overflow;
}

static inline void SetTimerBOverflow(bool enabled) {
  timer_b_overflow = enabled;
}

static inline bool GetTimerBOverflow() {
  return timer_b_overflow;
}

static inline void SetLFOEnabled(bool enabled) {
  lfo_enabled = enabled;
}

static inline bool GetLFOEnabled() {
  return lfo_enabled;
}

static inline void SetDACEnabled(bool enabled) {
  dac_enabled = enabled;
}

static inline bool GetDACEnabled() {
  return dac_enabled;
}

static inline void SetKeyState(YM2612Channels chn, YM2612Operators op, bool enabled) {
  key_enabled[chn][op] = enabled;
}

static inline bool GetKeyState(YM2612Channels chn, YM2612Operators op) {
  return key_enabled[chn][op];
}

static inline void SetTotalLevelData(YM2612Channels chn, YM2612Operators op, unsigned int data) {
  total_levels_data[chn][op] = data;
}

static inline unsigned int GetTotalLevelData(YM2612Channels chn, YM2612Operators op) {
  return total_levels_data[chn][op];
}

static inline void SetSustainLevelData(YM2612Channels chn, YM2612Operators op, unsigned int data) {
  sustain_levels_data[chn][op] = data;
}

static inline unsigned int GetSustainLevelData(YM2612Channels chn, YM2612Operators op) {
  return sustain_levels_data[chn][op];
}

static inline void SetAttackRateData(YM2612Channels chn, YM2612Operators op, unsigned int data) {
  attack_rate_data[chn][op] = data;
}

static inline unsigned int GetAttackRateData(YM2612Channels chn, YM2612Operators op) {
  return attack_rate_data[chn][op];
}

static inline void SetDecayRateData(YM2612Channels chn, YM2612Operators op, unsigned int data) {
  decay_rate_data[chn][op] = data;
}

static inline unsigned int GetDecayRateData(YM2612Channels chn, YM2612Operators op) {
  return decay_rate_data[chn][op];
}

static inline void SetSustainRateData(YM2612Channels chn, YM2612Operators op, unsigned int data) {
  sustain_rate_data[chn][op] = data;
}

static inline unsigned int GetSustainRateData(YM2612Channels chn, YM2612Operators op) {
  return sustain_rate_data[chn][op];
}

static inline void SetReleaseRateData(YM2612Channels chn, YM2612Operators op, unsigned int data) {
  release_rate_data[chn][op] = data;
}

static inline unsigned int GetReleaseRateData(YM2612Channels chn, YM2612Operators op) {
  return release_rate_data[chn][op];
}

static inline void SetSSGData(YM2612Channels chn, YM2612Operators op, unsigned int data) {
  ssg_data[chn][op] = data;
}

static inline unsigned int GetSSGData(YM2612Channels chn, YM2612Operators op) {
  return ssg_data[chn][op];
}

static inline void SetDetuneData(YM2612Channels chn, YM2612Operators op, unsigned int data) {
  detune_data[chn][op] = data;
}

static inline unsigned int GetDetuneData(YM2612Channels chn, YM2612Operators op) {
  return detune_data[chn][op];
}

static inline void SetMultipleData(YM2612Channels chn, YM2612Operators op, unsigned int data) {
  multiple_data[chn][op] = data;
}

static inline unsigned int GetMultipleData(YM2612Channels chn, YM2612Operators op) {
  return multiple_data[chn][op];
}

static inline void SetKeyScaleData(YM2612Channels chn, YM2612Operators op, unsigned int data) {
  key_scale_data[chn][op] = data;
}

static inline unsigned int GetKeyScaleData(YM2612Channels chn, YM2612Operators op) {
  return key_scale_data[chn][op];
}

static inline void SetAlgorithmData(YM2612Channels chn, unsigned int data) {
  algorithm_data[chn] = data;
}

static inline unsigned int GetAlgorithmData(YM2612Channels chn) {
  return algorithm_data[chn];
}

static inline void SetFeedbackData(YM2612Channels chn, unsigned int data) {
  feedback_data[chn] = data;
}

static inline unsigned int GetFeedbackData(YM2612Channels chn) {
  return feedback_data[chn];
}

static inline void SetFrequencyData(YM2612Channels chn, unsigned int data) {
  frequency_data[chn] = data;
}

static inline unsigned int GetFrequencyData(YM2612Channels chn) {
  return frequency_data[chn];
}

static inline void SetBlockData(YM2612Channels chn, unsigned int data) {
  block_data[chn] = data;
}

static inline unsigned int GetBlockData(YM2612Channels chn) {
  return block_data[chn];
}

static inline void SetAMSData(YM2612Channels chn, unsigned int data) {
  ams_data[chn] = data;
}

static inline unsigned int GetAMSData(YM2612Channels chn) {
  return ams_data[chn];
}

static inline void SetPMSData(YM2612Channels chn, unsigned int data) {
  pms_data[chn] = data;
}

static inline unsigned int GetPMSData(YM2612Channels chn) {
  return pms_data[chn];
}

static inline void SetCH3Mode(unsigned int mode) {
  ch3_mode = mode;
}

static inline unsigned int GetCH3Mode() {
  return ch3_mode;
}

static inline void SetFrequencyDataChannel3(YM2612Operators op, unsigned int data) {
  frequency_chn_data[YM2612Channels::CHANNEL3][op] = data;
}

static inline unsigned int GetFrequencyDataChannel3(YM2612Operators op) {
  return frequency_chn_data[YM2612Channels::CHANNEL3][op];
}

static inline void SetBlockDataChannel3(YM2612Operators op, unsigned int data) {
  block_chn_data[YM2612Channels::CHANNEL3][op] = data;
}

static inline unsigned int GetBlockDataChannel3(YM2612Operators op) {
  return block_chn_data[YM2612Channels::CHANNEL3][op];
}

static inline void SetTimerAData(unsigned int data) {
  timer_a_data = data;
}

static inline unsigned int GetTimerAData() {
  return timer_a_data;
}

static inline void SetTimerBData(unsigned int data) {
  timer_b_data = data;
}

static inline unsigned int GetTimerBData() {
  return timer_b_data;
}

static inline void SetTimerACurrentCounter(unsigned int data) {
  timer_a_current_counter = data;
}

static inline unsigned int GetTimerACurrentCounter() {
  return timer_a_current_counter;
}

static inline void SetTimerBCurrentCounter(unsigned int data) {
  timer_b_current_counter = data;
}

static inline unsigned int GetTimerBCurrentCounter() {
  return timer_b_current_counter;
}

static inline void SetLFOData(unsigned int data) {
  lfo_data = data;
}

static inline unsigned int GetLFOData() {
  return lfo_data;
}

static inline void SetDACData(unsigned int data) {
  dac_data = data;
}

static inline unsigned int GetDACData() {
  return dac_data;
}

static inline void SetExternalClockRate(double clockRate) {
  external_clock_rate = clockRate;
}

static inline double GetExternalClockRate() {
  return external_clock_rate;
}

static inline void SetFMClockDivider(unsigned int data) {
  fm_clock_divider = data;
}

static inline unsigned int GetFMClockDivider() {
  return fm_clock_divider;
}

static inline void SetEGClockDivider(unsigned int data) {
  eg_clock_divider = data;
}

static inline unsigned int GetEGClockDivider() {
  return eg_clock_divider;
}

static inline void SetOutputClockDivider(unsigned int data) {
  output_clock_divider = data;
}

static inline unsigned int GetOutputClockDivider() {
  return output_clock_divider;
}

static inline void SetTimerAClockDivider(unsigned int data) {
  timer_a_clock_divider = data;
}

static inline unsigned int GetTimerAClockDivider() {
  return timer_a_clock_divider;
}

static inline void SetTimerBClockDivider(unsigned int data) {
  timer_b_clock_divider = data;
}

static inline unsigned int GetTimerBClockDivider() {
  return timer_b_clock_divider;
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
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_CS1, (selectedChannel == YM2612Channels::CHANNEL1) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_CS2, (selectedChannel == YM2612Channels::CHANNEL2) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_CS3, (selectedChannel == YM2612Channels::CHANNEL3) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_CS4, (selectedChannel == YM2612Channels::CHANNEL4) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_CS5, (selectedChannel == YM2612Channels::CHANNEL5) ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, IDC_YM2612_DEBUGGER_CS6, (selectedChannel == YM2612Channels::CHANNEL6) ? BST_CHECKED : BST_UNCHECKED);

  return TRUE;
}

static INT_PTR YM2612_msgWM_COMMAND(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
  YM2612Channels channelNo = selectedChannel;

  if (HIWORD(wparam) == BN_CLICKED)
  {
    unsigned int controlID = LOWORD(wparam);
    switch (controlID)
    {
    case IDC_YM2612_DEBUGGER_INFO_OP1: {
      presenter.OpenOperatorView(channelNo, YM2612Operators::OPERATOR1);
      break; }
    case IDC_YM2612_DEBUGGER_INFO_OP2: {
      presenter.OpenOperatorView(channelNo, YM2612Operators::OPERATOR2);
      break; }
    case IDC_YM2612_DEBUGGER_INFO_OP3: {
      presenter.OpenOperatorView(channelNo, YM2612Operators::OPERATOR3);
      break; }
    case IDC_YM2612_DEBUGGER_INFO_OP4: {
      presenter.OpenOperatorView(channelNo, YM2612Operators::OPERATOR4);
      break; }

                                     //Channel Select
    case IDC_YM2612_DEBUGGER_CS1: {
      selectedChannel = YM2612Channels::CHANNEL1;
      InvalidateRect(hwnd, NULL, FALSE);
      break; }
    case IDC_YM2612_DEBUGGER_CS2: {
      selectedChannel = YM2612Channels::CHANNEL2;
      InvalidateRect(hwnd, NULL, FALSE);
      break; }
    case IDC_YM2612_DEBUGGER_CS3: {
      selectedChannel = YM2612Channels::CHANNEL3;
      InvalidateRect(hwnd, NULL, FALSE);
      break; }
    case IDC_YM2612_DEBUGGER_CS4: {
      selectedChannel = YM2612Channels::CHANNEL4;
      InvalidateRect(hwnd, NULL, FALSE);
      break; }
    case IDC_YM2612_DEBUGGER_CS5: {
      selectedChannel = YM2612Channels::CHANNEL5;
      InvalidateRect(hwnd, NULL, FALSE);
      break; }
    case IDC_YM2612_DEBUGGER_CS6: {
      selectedChannel = YM2612Channels::CHANNEL6;
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

                                         //Timers
    case IDC_YM2612_DEBUGGER_TIMERA_RATE: {
      SetTimerAData(GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_TIMERB_RATE: {
      SetTimerBData(GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_TIMERA_DATA: {
      SetTimerACurrentCounter(GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_TIMERB_DATA: {
      SetTimerBCurrentCounter(GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }

                                        //LFO
    case IDC_YM2612_DEBUGGER_LFOFREQ: {
      SetLFOData(GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }

                                    //DAC
    case IDC_YM2612_DEBUGGER_DACDATA: {
      SetDACData(GetDlgItemHex(hwnd, LOWORD(wparam)));
      break; }

                                    //Clock
    case IDC_YM2612_DEBUGGER_CLOCK: {
      SetExternalClockRate(GetDlgItemDouble(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_FMDIVIDE: {
      SetFMClockDivider(GetDlgItemBin(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_EGDIVIDE: {
      SetEGClockDivider(GetDlgItemBin(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_OUTDIVIDE: {
      SetOutputClockDivider(GetDlgItemBin(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_TADIVIDE: {
      SetTimerAClockDivider(GetDlgItemBin(hwnd, LOWORD(wparam)));
      break; }
    case IDC_YM2612_DEBUGGER_TBDIVIDE: {
      SetTimerBClockDivider(GetDlgItemBin(hwnd, LOWORD(wparam)));
      break; }
    }

    return FALSE;
  }

  return TRUE;
}

static INT_PTR YM2612_msgWM_TIMER(HWND hwnd, WPARAM wparam, LPARAM lparam)
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
  if (currentControlFocus != IDC_YM2612_DEBUGGER_TIMERA_RATE)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_TIMERA_RATE, 3, GetTimerAData());
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_TIMERB_RATE)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_TIMERB_RATE, 2, GetTimerBData());
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_TIMERA_DATA)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_TIMERA_DATA, 3, GetTimerACurrentCounter());
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_TIMERB_DATA)
  {
    UpdateDlgItemHex(hwnd, IDC_YM2612_DEBUGGER_TIMERB_DATA, 2, GetTimerBCurrentCounter());
  }

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

  //Clock
  if (currentControlFocus != IDC_YM2612_DEBUGGER_CLOCK)
  {
    UpdateDlgItemFloat(hwnd, IDC_YM2612_DEBUGGER_CLOCK, (float)GetExternalClockRate());
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_FMDIVIDE)
  {
    UpdateDlgItemBin(hwnd, IDC_YM2612_DEBUGGER_FMDIVIDE, GetFMClockDivider());
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_EGDIVIDE)
  {
    UpdateDlgItemBin(hwnd, IDC_YM2612_DEBUGGER_EGDIVIDE, GetEGClockDivider());
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_OUTDIVIDE)
  {
    UpdateDlgItemBin(hwnd, IDC_YM2612_DEBUGGER_OUTDIVIDE, GetOutputClockDivider());
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_TADIVIDE)
  {
    UpdateDlgItemBin(hwnd, IDC_YM2612_DEBUGGER_TADIVIDE, GetTimerAClockDivider());
  }
  if (currentControlFocus != IDC_YM2612_DEBUGGER_TBDIVIDE)
  {
    UpdateDlgItemBin(hwnd, IDC_YM2612_DEBUGGER_TBDIVIDE, GetTimerBClockDivider());
  }

  return TRUE;
}

static INT_PTR YM2612WndProcDialog(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
  WndProcDialogImplementSaveFieldWhenLostFocus(hwnd, msg, wparam, lparam);
  switch (msg)
  {
  case WM_INITDIALOG:
    return YM2612_msgWM_INITDIALOG(hwnd, wparam, lparam);
  case WM_COMMAND:
    return YM2612_msgWM_COMMAND(hwnd, wparam, lparam);
  }
  return FALSE;
}