#include "engine.hpp"
#include "uci.hpp"

#include <iostream>

int main()
{
    aurora::chess::Engine engine{"Aurora"};
    std::cout << engine.describe() << '\n';
    aurora::chess::run_uci_loop(engine, std::cin, std::cout);
    return 0;
}
