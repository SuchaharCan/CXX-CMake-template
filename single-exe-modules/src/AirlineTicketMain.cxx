/**
 * @file AirlineTicketMain.cxx
 * @brief Ticket 可执行入口：import airline_ticket，演示定价与 ScenarioLog。
 */

#include "ScenarioLog.hxx"

import std;
import airline_ticket;

using scenario_log::log_info;
using scenario_log::log_sep;

int main() {
  log_sep();
  log_info("Ticket starting");

  AirlineTicket myTicket;
  myTicket.setPassengerName("Sherman T. Socketwrench");
  myTicket.setNumberOfMiles(700);
  const double cost{myTicket.calculatePriceInDollars()};

  log_info("passenger=" + myTicket.getPassengerName());
  log_info("miles=" + std::to_string(myTicket.getNumberOfMiles()));
  log_info("cost=" + std::to_string(cost));

  std::cout << "This ticket will cost $" << cost << '\n';

  log_info("Ticket finished ok");
  log_sep();
  return 0;
}
