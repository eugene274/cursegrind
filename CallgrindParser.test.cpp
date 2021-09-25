//
// Created by eugene on 13/09/2021.
//

#include <gtest/gtest.h>
#include "CallgrindParser.hpp"

TEST(CallgrindParser, Basics) {
  CallgrindParser parser("callgrind.out.64956");
  parser.SetVerbose(false);
  parser.parse();
  parser.Summary();
  std::cout << "Done" << std::endl;
}

TEST(CallgrindParser, Empty) {
  CallgrindParser parser("empty.out");
  parser.SetVerbose(false);
  parser.parse();
  parser.Summary();
}