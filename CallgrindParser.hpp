
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

#ifndef CALLGRIND_VIEWER__CALLGRINDPARSER_HPP_
#define CALLGRIND_VIEWER__CALLGRINDPARSER_HPP_

#include <boost/lexical_cast.hpp>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <regex>
#include <utility>

class CallgrindParser {
 public:
  using SubPosition = uint64_t;
  using Cost = uint64_t;

  enum class PositionType : std::size_t { Cost = 0, Call = 1, FiFe = 2 };

  struct PositionSpec {
    PositionSpec(std::string name, std::string value)
        : name(std::move(name)), value(std::move(value)) {}
    std::string name;
    std::string value;
  };

  struct CostSpec {
    CostSpec(std::vector<SubPosition> sub_positions, std::vector<Cost> costs)
        : sub_positions(std::move(sub_positions)), costs(std::move(costs)) {}
    std::vector<SubPosition> sub_positions;
    std::vector<Cost> costs;
  };

  struct CallSpec {
    CallSpec(unsigned int ncalls, const std::vector<SubPosition> &sub_positions)
        : ncalls(ncalls), sub_positions(sub_positions) {}
    unsigned int ncalls;
    std::vector<SubPosition> sub_positions;
  };

  struct Entry;
  using EntryPtr = std::shared_ptr<Entry>;

  struct Call {
    Call(unsigned int ncalls, std::vector<SubPosition> sub_positions,
         std::shared_ptr<Entry> entry)
        : ncalls(ncalls),
          sub_positions(std::move(sub_positions)),
          entry(std::move(entry)) {}

    unsigned int ncalls;
    std::vector<SubPosition> sub_positions;
    std::vector<CostSpec> costs;
    std::shared_ptr<Entry> entry;

    void addCost(const CostSpec &spec) { costs.emplace_back(spec); }
    std::vector<Cost> totalCosts() const {
      std::vector<Cost> result;
      for (auto &cost_spec : costs) {
        if (result.empty()) {
          result = cost_spec.costs;
          continue;
        }
        for (int i = 0; i < cost_spec.costs.size(); ++i) {
          result[i] += cost_spec.costs[i];
        }
      }
      return result;
    }
  };

  struct Position {
    /* object */
    std::string binary;
    /* filename */
    std::string source;
    /* function name */
    std::string symbol;

    Position &operator=(const Position &other) = delete;
    void setPosition(const PositionSpec &spec) {
      if (spec.name == "ob") {
        binary = spec.value;
      } else if (spec.name == "fl") {
        source = spec.value;
      } else if (spec.name == "fn") {
        symbol = spec.value;
      } else if (spec.name == "fi") {
        source = spec.value;
      } else if (spec.name == "fe") {
        source = spec.value;
      } else {
        throw std::runtime_error("Unknown spec: " + spec.name);
      }
    }
    friend std::ostream &operator<<(std::ostream &os,
                                    const Position &position) {
      os << "fl: " << position.source << " fn: " << position.symbol;
      return os;
    }

    bool operator==(const Position &rhs) const {
      return symbol == rhs.symbol && binary == rhs.binary &&
             source == rhs.source;
    }
    bool operator!=(const Position &rhs) const { return !(rhs == *this); }
  };

  struct Entry {
    std::shared_ptr<Position> position{nullptr};
    std::vector<CostSpec> costs;
    std::vector<Call> calls;
    std::vector<std::weak_ptr<Entry> > callers;

    void addCost(const CostSpec &spec) { costs.emplace_back(spec); }
    void addCall(Call &&call) { calls.emplace_back(call); }
    std::vector<Cost> totalCost() const {
      std::vector<Cost> result;
      for (size_t ic = 0; ic < costs[0].costs.size(); ++ic) {
        Cost sum = 0;
        for (const auto &cost : costs) {
          sum += cost.costs[ic];
        }
        for (const auto &call : calls) {
          for (const auto &call_cost : call.costs) {
            sum += call_cost.costs[ic];
          }
        }
        result.push_back(sum);
      }
      return result;
    }
  };

  explicit CallgrindParser(std::string filename)
      : filename(std::move(filename)) {}

  void parse() {
    auto get_position_from_cache =
        [this](const Position &position) -> std::shared_ptr<Position> {
      auto cached_position = std::find_if(
          begin(this->positions_cache_), end(this->positions_cache_),
          [&position](const auto &p) { return *p == position; });

      if (cached_position == end(positions_cache_)) {
        auto new_cache_entry = std::make_shared<Position>(position);
        this->positions_cache_.emplace_back(new_cache_entry);
        return new_cache_entry;
      }

      return *cached_position;
    };

    /* positions: [instr] [line]
    For cost lines, this defines the semantic of the first numbers. Any
    combination of "instr", "bb" and "line" is allowed, but has to be in this
    order which corresponds to position numbers at the start of the cost lines
    later in the file. */
    static const std::regex re_positions_def(
        "^positions:\\s+(instr|line\\s*)+$");

    static const std::regex re_events_def(R"(^events:\s+(\w+\s*)+$)");

    std::ifstream ifs(filename);
    std::string line;
    unsigned int current_line_number = 0;
    auto getline = [&ifs, &line, &current_line_number]() -> bool {
      auto is_eof = !bool(std::getline(ifs, line));
      if (!is_eof) {
        current_line_number++;
      }
      return !is_eof;
    };

    Position current_position;

    while (getline()) {
      std::smatch match_results;
      /* POSITION */
      if (std::optional<PositionSpec> cost_position;
          bool(cost_position = parsePositionLine(line, PositionType::Cost))) {
        /* setup new event */
        if (verbose_) std::cout << "Begin entry" << std::endl;
        auto new_entry = std::make_shared<Entry>();
        /* populating position */
        {
          current_position.setPosition(*cost_position);
          while (getline() && (cost_position = parsePositionLine(
                                   line, PositionType::Cost))) {
            current_position.setPosition(*cost_position);
          }
          new_entry->position = get_position_from_cache(current_position);
        }
        /* line = CostSpec */
        /* now parsing costs */
        {
          auto cost_spec = parseCostLine(line);
          if (!cost_spec) {
            throw std::runtime_error("Expected cost spec at " +
                                     std::to_string(current_line_number));
          }
          new_entry->addCost(*cost_spec);
          while (getline()) {
            if ((cost_spec = parseCostLine(line))) {
              new_entry->addCost(*cost_spec);
            } else if (parsePositionLine(line, PositionType::FiFe)) {
              if (verbose_) std::cout << "Ignore fife" << std::endl;
            } else {
              break;
            }
          }
        }
        while (true) {
          /* here line is call position spec or endline */
          Position call_position = current_position;
          if (std::optional<PositionSpec> call_position_spec;
              bool(call_position_spec =
                       parsePositionLine(line, PositionType::Call))) {
            if (verbose_) std::cout << "Begin call" << std::endl;
            auto call_entry = std::make_shared<Entry>();
            call_position.setPosition(*call_position_spec);
            while (getline() && (call_position_spec = parsePositionLine(
                                     line, PositionType::Call))) {
              call_position.setPosition(*call_position_spec);
            }
            call_entry->position = get_position_from_cache(call_position);
            /* here line is call line */
            auto call_line = parseCallLine(line);
            if (!call_line) {
              throw std::runtime_error("Expected call line at " +
                                       std::to_string(current_line_number));
            }
            Call call(call_line->ncalls, call_line->sub_positions, call_entry);
            {
              /* we expect cost line after */
              getline();
              auto cost_spec = parseCostLine(line);
              if (!cost_spec) {
                throw std::runtime_error("Expected cost line after call at " +
                                         std::to_string(current_line_number));
              }
              call.addCost(*cost_spec);
              while (getline()) {
                if ((cost_spec = parseCostLine(line))) {
                  call.addCost(*cost_spec);
                }
                /* fi/fe= */
                else if (parsePositionLine(line, PositionType::FiFe)) {
                  if (verbose_) std::cout << "Ignore fife" << std::endl;
                } else {
                  break;
                }
              }
            }
            assert(!call.costs.empty());
            /* now we ready to add new call */
            new_entry->addCall(std::move(call));
            /* now line should be empty */
          } else if (parseEmptyLine(line)) {
            break;
          } else {
            throw std::runtime_error("Unexpected not empty line");
          }

        }  // while (true)

        entries_.push_back(new_entry);

        if (verbose_) std::cout << "End entry" << std::endl;
      }
      /* header */
      else if (std::regex_search(line, match_results, re_positions_def)) {
        for (size_t im = 1; im < match_results.size(); ++im) {
          positions_def.emplace_back(match_results.str(im));  // TODO trim
        }
        current_subposition.resize(positions_def.size());
        if (verbose_) std::cout << line << std::endl;
      } else if (std::regex_search(line, match_results, re_events_def)) {
        for (size_t im = 1; im < match_results.size(); ++im) {
          events_def.emplace_back(match_results.str(im));  // TODO trim
        }
        if (verbose_) std::cout << line << std::endl;
      } else if (parseEmptyLine(line)) {
      } else {
        //        std::cerr << current_line_number << ": " << line <<
        //        std::endl;
      }
    }

    for (auto &entry1 : entries_) {
      for (auto &entry1_call : entry1->calls) {
        for (auto &entry2 : entries_) {
          if (*(entry1_call.entry->position) == *(entry2->position)) {
            entry1_call.entry = entry2;
            auto this_caller =
                std::find_if(begin(entry2->callers), end(entry2->callers),
                             [entry1](const std::weak_ptr<Entry> &e) {
                               return e.lock() == entry1;
                             });
            if (this_caller == end(entry2->callers)) {
              entry2->callers.emplace_back(entry1);
            }
            break;
          }
        }
      }
    }

    std::sort(begin(entries_), end(entries_),
              [](const EntryPtr &lhs, const EntryPtr &rhs) {
                return lhs->totalCost()[0] > rhs->totalCost()[0];
              });

    std::cout << "Parsed " << current_line_number << " lines" << std::endl;
  }

 private:
  template <typename NumberType, typename Iterator>
  NumberType parseNumber(const Iterator &begin, const Iterator &end) {
    auto length = std::distance(begin, end);
    assert(length > 0);
    return boost::lexical_cast<NumberType>(begin.operator->(), length);
  }

  template <class Iterator>
  SubPosition parseSubPosition(const Iterator &begin, const Iterator &end,
                               size_t subposition_index) {
    /* SubPosition := Number | "+" Number | "-" Number | "*" */
    if (std::distance(begin, end) == 1 && *begin == '*') {
      return current_subposition[subposition_index];
    } else if (std::distance(begin, end) > 1 && *begin == '+') {
      return current_subposition[subposition_index] +
             parseNumber<SubPosition>(begin + 1, end);
    } else if (std::distance(begin, end) > 1 && *begin == '-') {
      return current_subposition[subposition_index] -
             parseNumber<SubPosition>(begin + 1, end);
    }
    current_subposition[subposition_index] =
        parseNumber<SubPosition>(begin, end);
    return current_subposition[subposition_index];
  }

  bool parseEmptyLine(const std::string &empty_line) {
    static const std::regex re_empty("^\\s*$");
    return std::regex_search(empty_line, re_empty);
  }

  std::optional<PositionSpec> parsePositionLine(const std::string &line,
                                                PositionType position_type) {
    /* CostPosition := "ob" | "fl" | "fi" | "fe" | "fn" */
    /* CalledPosition := " "cob" | "cfi" | "cfl" | "cfn" */
    static const std::vector<std::regex> re_map{
        std::regex(R"(^(ob|fl|fi|fe|fn)=[ \t]*(\(\d+\))?[ \t]*(.*)$)"),
        std::regex(R"(^c(ob|fl|fi|fn)=[ \t]*(\(\d+\))?[ \t]*(.*)$)"),
        std::regex(R"(^(fi|fe)=[ \t]*(\(\d+\))?[ \t]*(.*)$)")};
    auto &re_position_spec = re_map[size_t(position_type)];

    std::smatch match_results;

    if (std::regex_search(line, match_results, re_position_spec)) {
      auto position = match_results[1].str();
      auto compression_index =
          match_results[2].matched
              ? parseNumber<unsigned int>(match_results[2].first + 1,
                                          match_results[2].second - 1)
              : -1;
      bool has_specified_name =
          match_results[3].matched && !match_results[3].str().empty();

      std::string value;
      if (!has_specified_name) {
        const std::map<unsigned int, std::string> *cache{nullptr};
        if (position == "fl" || position == "fe" || position == "fi") {
          cache = &file_compression_cache_;
        } else if (position == "fn") {
          cache = &symbol_compression_cache_;
        } else if (position == "ob") {
          cache = &object_compression_cache_;
        } else {
          assert(false);
        }
        auto result = cache->find(compression_index);
        if (result == end(*cache)) {
          throw std::runtime_error("Cannot find compression from the cache");
        }
        value = result->second;
      } else {
        value = match_results.str(3);
      }

      /* cache value */
      if (compression_index >= 0 && has_specified_name) {
        std::map<unsigned int, std::string> *cache = &symbol_compression_cache_;
        if (position == "fl" || position == "fe" || position == "fi") {
          cache = &file_compression_cache_;
        } else if (position == "fn") {
          cache = &symbol_compression_cache_;
        } else if (position == "ob") {
          cache = &object_compression_cache_;
        } else {
          assert(false);
        }
        auto result = cache->emplace(compression_index, value);
        assert(result.second);
      }

      assert(!value.empty());
      return {{position, value}};
    }
    return {};
  }

  std::optional<CostSpec> parseCostLine(const std::string &costs_line) {
    static const std::regex re_space("\\s+");
    static const std::regex re_number(
        R"(^(\*|[+-]\d+|0[xX][0-9a-fA-F]+|\d+)+$)");
    for (auto it = std::sregex_token_iterator(begin(costs_line),
                                              end(costs_line), re_space, -1);
         it != std::sregex_token_iterator(); ++it) {
      if (!std::regex_match(it->first, it->second, re_number)) {
        return {};
      }
    }

    std::stringstream ss(costs_line);

    std::vector<SubPosition> sub_positions;
    sub_positions.reserve(positions_def.size());

    std::vector<Cost> costs;
    costs.reserve(events_def.size());

    int subposition_index = 0;
    for (auto &&_ : positions_def) {
      std::string tmp;
      ss >> tmp;
      auto sub_position =
          parseSubPosition(begin(tmp), end(tmp), subposition_index);
      sub_positions.emplace_back(sub_position);
      subposition_index++;
    }

    for (auto &&_ : events_def) {
      std::string tmp;
      ss >> tmp;
      auto cost = parseNumber<Cost>(begin(tmp), end(tmp));
      costs.emplace_back(cost);
    }

    return {{sub_positions, costs}};
  }

  std::optional<CallSpec> parseCallLine(const std::string &line) {
    /* CallLine := "calls=" Space* Number Space+ SubPositionList */
    static const std::regex re_call_line(R"(^calls=\s*(.+)\s+(.+))");

    std::smatch match_results;
    if (std::regex_search(line, match_results, re_call_line)) {
      auto n_calls = parseNumber<unsigned int>(match_results[1].first,
                                               match_results[1].second);

      std::vector<SubPosition> sub_positions;
      std::stringstream ss(match_results[2].str());
      int subposition_index = 0;
      for (auto &&_ : positions_def) {
        std::string tmp;
        ss >> tmp;
        sub_positions.emplace_back(
            parseSubPosition(begin(tmp), end(tmp), subposition_index));
        subposition_index++;
      }

      return {{n_calls, sub_positions}};
    }

    return {};
  }

 public:
  void SetVerbose(bool verbose) { CallgrindParser::verbose_ = verbose; }

  void Summary() const {
    using std::cout;
    using std::endl;

    cout << "Entries: " << entries_.size() << "; " << endl;
    cout << "Unique positions: " << positions_cache_.size() << "; " << endl;
    cout << endl;
    printTopEntries(cout, 100);
  }

  const std::vector<std::shared_ptr<Entry> > &getEntries() const {
    return entries_;
  }

 private:
  void printTopEntries(std::ostream &os, unsigned int ne = 0) const {
    if (entries_.empty()) return;
    ne = ne == 0 ? entries_.size() : ne;

    auto max_cost = entries_[0]->totalCost();
    for (const auto &entry : entries_) {
      if (ne == 0) break;

      os << entry->totalCost()[0] * 100 / max_cost[0] << "% "
         << entry->totalCost()[0] << "\t\t" << entry->position->binary
         << "::" << entry->position->symbol << std::endl;
      --ne;
    }
  }

  std::vector<std::string> events_def;
  std::vector<std::string> positions_def;
  std::vector<SubPosition> current_subposition;

  std::map<unsigned int, std::string> file_compression_cache_;
  std::map<unsigned int, std::string> symbol_compression_cache_;
  std::map<unsigned int, std::string> object_compression_cache_;

  std::string filename;

  std::vector<std::shared_ptr<Entry> > entries_;
  std::vector<std::shared_ptr<Position> > positions_cache_;

  bool verbose_{true};
};

#endif  // CALLGRIND_VIEWER__CALLGRINDPARSER_HPP_
