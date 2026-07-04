#include "engine.hpp"

#include <iostream>

int main()
{
    aurora::chess::Engine engine{"Aurora"};
    std::cout << engine.describe() << '\n';
    engine.run_uci_loop();
    return 0;
}
