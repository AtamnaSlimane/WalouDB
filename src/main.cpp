#include "waloudb/core/Log.h"

#include <iostream>
#include <string>

int main() {
  WalouDB::Log::Init();
  std::string input;
  while (true) {
    std::cout << "WalouDB>> ";
    WALOU_INFO("hello");
    WALOU_CRITICAL("noooooooooo");
    std::getline(std::cin, input);
    if (input == ".help") {
      std::cout << "Available commands: \nhelp\nexit\n";
    } else if (input == ".exit") {
      break;
    } else {
      std::cout << "unrecognizable command\n";
    }
  }
  return 0;
}
