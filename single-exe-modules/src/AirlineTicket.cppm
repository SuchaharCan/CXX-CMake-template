/**
 * @file AirlineTicket.cppm
 * @module airline_ticket
 * @brief 航空机票领域模型：乘客、里程与精英会员定价（C++23 模块单元）。
 *
 * 实现与接口合并于同一 .cppm，便于 CMake FILE_SET CXX_MODULES 扫描。
 */

export module airline_ticket;

import std;

/// 机票实体：里程计价，精英会员免费
export class AirlineTicket {
 public:
  AirlineTicket();
  ~AirlineTicket();

  /// 按里程计算美元票价（精英会员返回 0）
  double calculatePriceInDollars();

  std::string getPassengerName();
  void setPassengerName(std::string name);

  int getNumberOfMiles();
  void setNumberOfMiles(int miles);

  bool hasEliteSuperRewardsStatus();
  void setHasEliteSuperRewardsStatus(bool status);

 private:
  std::string m_passengerName{"Unknown Passenger"};
  int m_numberOfMiles{0};
  bool m_hasEliteSuperRewardsStatus{false};
};

// 实现（与原 AirlineTicket.cpp / .cxx 相同；合并进接口单元以便 CMake FILE_SET 扫描）
AirlineTicket::AirlineTicket()
    : m_passengerName{"Unknown Passenger"},
      m_numberOfMiles{0},
      m_hasEliteSuperRewardsStatus{false} {}

AirlineTicket::~AirlineTicket() {}

double AirlineTicket::calculatePriceInDollars() {
  if (hasEliteSuperRewardsStatus()) {
    return 0;
  }
  return getNumberOfMiles() * 0.1;  // 每英里 0.1 美元
}

std::string AirlineTicket::getPassengerName() {
  return m_passengerName;
}

void AirlineTicket::setPassengerName(std::string name) {
  m_passengerName = name;
}

int AirlineTicket::getNumberOfMiles() {
  return m_numberOfMiles;
}

void AirlineTicket::setNumberOfMiles(int miles) {
  m_numberOfMiles = miles;
}

bool AirlineTicket::hasEliteSuperRewardsStatus() {
  return m_hasEliteSuperRewardsStatus;
}

void AirlineTicket::setHasEliteSuperRewardsStatus(bool status) {
  m_hasEliteSuperRewardsStatus = status;
}
