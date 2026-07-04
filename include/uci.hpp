#pragma once

#include <iosfwd>

#include "engine.hpp"

namespace aurora::chess
{

    void run_uci_loop(Engine &engine, std::istream &input, std::ostream &output);

} // namespace aurora::chess
