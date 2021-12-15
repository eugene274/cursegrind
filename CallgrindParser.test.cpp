/*
 *     calcurse - lightweight viewer of the callgrind tool of Valgrind
 *     Copyright (C) 2021  Evgeny Kashirin
 *     Contact: kashirin.e(a)list.ru
 *
 *     This program is free software: you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation, either version 3 of the License, or
 *     (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <gtest/gtest.h>
#include "CallgrindParser.hpp"

TEST(CallgrindParser, Basics) {
  CallgrindParser parser("callgrind.out.18859");
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