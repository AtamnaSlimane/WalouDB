#include "waloudb/core/Database.h"
#include <iostream>
#include <string>

int main() {

  std::string input;

  while (true) {
    std::cout << "WalouDB>> ";
    std::getline(std::cin, input);
    if (input == ".help") {
      std::cout << "Available commands: \nhelp\nexit\n";
    } else if (input == ".exit") {
      break;
    } else {
      std::cout << "unrecognizable command\n";
    }
  }
  std::cout << "bye;\n";

  return 0;
}
