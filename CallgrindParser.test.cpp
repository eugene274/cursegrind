//
// Created by eugene on 13/09/2021.
//

#include <gtest/gtest.h>
#include "CallgrindParser.hpp"

TEST(CallgrindParser, Basics) {
  CallgrindParser parser("callgrind.out.18859");
  parser.SetVerbose(false);
  parser.parse();
  parser.Summary();
  std::cout << "Done" << std::endl;
}