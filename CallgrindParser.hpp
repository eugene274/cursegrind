//
// Created by eugene on 13/09/2021.
//

#ifndef CALLGRIND_VIEWER__CALLGRINDPARSER_HPP_
#define CALLGRIND_VIEWER__CALLGRINDPARSER_HPP_

#include <fstream>
#include <memory>
#include <utility>
#include <iostream>
#include <regex>
#include <boost/lexical_cast.hpp>

class CallgrindParser {
 public:
  using SubPosition = uint64_t;
  using Cost = uint64_t;

  enum class PositionType {
    Cost, Call
  };

  enum class State {
    Initialization,
    PositionDef,
    CostsDef,
    CallPositionDef,
  };

  struct PositionSpec {
    PositionSpec(std::string name, std::string value) : name(std::move(name)), value(std::move(value)) {}
    std::string name;
    std::string value;
  };

  struct CostSpec {
    CostSpec(std::vector<SubPosition> sub_positions, std::vector<Cost> costs) : sub_positions(std::move(
        sub_positions)), costs(std::move(costs)) {}
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
    Call(unsigned int ncalls, std::vector<SubPosition> sub_positions, std::shared_ptr<Entry> entry)
        : ncalls(ncalls), sub_positions(std::move(sub_positions)), entry(std::move(entry)) {}

    unsigned int ncalls;
    std::vector<SubPosition> sub_positions;
    std::vector<CostSpec> costs;
    std::shared_ptr<Entry> entry;
  };

  struct Position {
    /* object */
    std::string ob;
    /* filename */
    std::string fl;
    /* function name */
    std::string fn;

    std::string fi;

    std::string fe;

    Position &operator = (const Position& other) = delete;
    void setPosition(const PositionSpec &spec) {
      if (spec.name == "ob") {
        ob = spec.value;
      } else if (spec.name == "fl") {
        fl = spec.value;
      } else if (spec.name == "fn") {
        fn = spec.value;
      } else if (spec.name == "fi") {
        fi = spec.value;
      } else if (spec.name == "fe") {
        fe = spec.value;
      } else {
        throw std::runtime_error("Unknown spec: " + spec.name);
      }
    }
    friend std::ostream &operator<<(std::ostream &os, const Position &position) {
      os << "fl: " << position.fl << " fn: " << position.fn;
      return os;
    }

    bool operator==(const Position &rhs) const {
      return ob == rhs.ob &&
          fl == rhs.fl &&
          fn == rhs.fn;
    }
    bool operator!=(const Position &rhs) const {
      return !(rhs == *this);
    }
  };

  struct Entry {
    std::shared_ptr<Position> position{nullptr};
    std::vector<CostSpec> costs;
    std::vector<Call> calls;

    void addCost(const CostSpec &spec) { costs.emplace_back(spec); }
    void addCall(Call &&call) { calls.emplace_back(call); }

    std::vector<Cost> totalCost() const {
      std::vector<Cost> sum;
      sum.reserve(costs.size());
      for (int ic = 0; ic < sum.size(); ic++) {
        for (const auto& c : costs) {
          sum[ic] += c.costs[ic];
        }


      }
      return sum;
    }

  };

  explicit CallgrindParser(std::string filename) : filename(std::move(filename)) {}

  void parse() {
    /* positions: [instr] [line]
    For cost lines, this defines the semantic of the first numbers. Any combination of "instr", "bb" and "line" is allowed,
    but has to be in this order which corresponds to position numbers at the start of the cost lines later in the file. */
    const std::regex re_positions_def("^positions:\\s+(instr|line\\s*)+$");

    const std::regex re_events_def(R"(^events:\s+(\w+\s*)+$)");

    std::ifstream ifs(filename);
    std::string &line = current_line;
    unsigned int line_number = 1;
    while (std::getline(ifs, line)) {
      std::smatch match_results;
      /* POSITION */
      if (std::optional<PositionSpec> cost_position;
          bool(cost_position = parsePositionLine(line, PositionType::Cost))) {
        current_state = State::PositionDef;

        if (!current_entry) {
          current_entry = newEntry();
        }

        costs_target = current_entry;

        current_position.setPosition(*cost_position);

        if (verbose_) std::cout << line << std::endl;
      }
        /* COSTS LINE */
      else if (std::optional<CostSpec> cost;
          costs_target && bool(cost = parseCostLine(line))) {
        if (current_state == State::PositionDef || current_state == State::CallPositionDef) {
          auto tmp = current_position;
          auto cached_position = std::find_if(begin(positions_cache_), end(positions_cache_), [&tmp](const auto &p) {
            return *p == tmp;
          });

          auto entry_ptr = current_state == State::PositionDef ? current_entry : current_call_entry;
          if (cached_position == end(positions_cache_)) {
            entry_ptr->position = std::make_shared<Position>(current_position);
            positions_cache_.emplace_back(entry_ptr->position);
          } else {
            entry_ptr->position = *cached_position;
          }


        }
        else if (current_state == State::CostsDef) {
          /* ignore */
        }
        else {
          throw std::runtime_error("Unexpected state");
        }
        current_state = State::CostsDef;
        costs_target->addCost(*cost);
        if (verbose_) std::cout << line << std::endl;
      }
        /* CALL POSITION */
      else if (std::optional<PositionSpec> call_position;
          bool(call_position = parsePositionLine(line, PositionType::Call))) {
        if (current_state == State::CostsDef) {
          current_call_entry.reset();
        }

        current_state = State::CallPositionDef;
        current_position.setPosition(*call_position);
        if (!current_call_entry) {
          current_call_entry = newEntry();
        }
        if (verbose_) std::cout << line << std::endl;
      }
        /* CALL LINE */
      else if (std::optional<CallSpec> call_spec;
          current_call_entry && bool(call_spec = parseCallLine(line))) {
        current_entry->addCall(Call(call_spec->ncalls, call_spec->sub_positions, current_call_entry));
        costs_target = current_call_entry;
        if (verbose_) std::cout << line << std::endl;
      }
        /* header */
      else if (std::regex_search(line, match_results, re_positions_def)) {
        for (size_t im = 1; im < match_results.size(); ++im) {
          positions_def.emplace_back(match_results.str(im)); // TODO trim
        }
        current_subposition.resize(positions_def.size());
        if (verbose_) std::cout << line << std::endl;
      } else if (std::regex_search(line, match_results, re_events_def)) {
        for (size_t im = 1; im < match_results.size(); ++im) {
          events_def.emplace_back(match_results.str(im)); // TODO trim
        }
        if (verbose_) std::cout << line << std::endl;
      } else if (parseEmptyLine(line)) {
        if (current_entry) {
          for (const auto &entry : entries) {
            for (auto& call : entry->calls) {
              if (*call.entry->position == *current_entry->position) {
                call.entry = current_entry;
                std::cout << "Found nested entry" << std::endl;
              }
            }
          }

          entries.emplace_back(current_entry);
        }

        current_entry.reset();
        current_call_entry.reset();
        costs_target.reset();
      } else {
        std::cerr << line_number << ": " << line << std::endl;
      }

      line_number++;
    }
  }
 private:

  template<typename NumberType, typename Iterator>
  NumberType parseNumber(const Iterator &begin, const Iterator &end) {
    auto length = std::distance(begin, end);
    assert(length > 0);
    try {
      return boost::lexical_cast<NumberType>(begin.operator->(), length);
    } catch (boost::bad_lexical_cast &e) {
      throw std::runtime_error("Line: " + current_line + ": Unable to cast " + std::string(begin, end));
    }

  }

  template<class Iterator>
  SubPosition parseSubPosition(
      const Iterator &begin,
      const Iterator &end,
      size_t subposition_index) {
    /* SubPosition := Number | "+" Number | "-" Number | "*" */
    if (std::distance(begin, end) == 1 && *begin == '*') {
      return current_subposition[subposition_index];
    } else if (std::distance(begin, end) > 1 && *begin == '+') {
      return current_subposition[subposition_index] + parseNumber<SubPosition>(begin + 1, end);
    } else if (std::distance(begin, end) > 1 && *begin == '-') {
      return current_subposition[subposition_index] - parseNumber<SubPosition>(begin + 1, end);
    }
    current_subposition[subposition_index] = parseNumber<SubPosition>(begin, end);
    return current_subposition[subposition_index];
  }

  bool parseEmptyLine(const std::string &empty_line) {
    const std::regex re_empty("^\\s*$");
    return std::regex_search(empty_line, re_empty);
  }

  std::optional<PositionSpec> parsePositionLine(const std::string &line, PositionType position_type) {
    /* CostPosition := "ob" | "fl" | "fi" | "fe" | "fn" */
    /* CalledPosition := " "cob" | "cfi" | "cfl" | "cfn" */
    const std::regex re_position_spec(
        position_type == PositionType::Cost ?
        R"(^(ob|fl|fi|fe|fn)=[ \t]*(\(\d+\))?[ \t]*(.*)$)" :
        R"(^c(ob|fl|fi|fn)=[ \t]*(\(\d+\))?[ \t]*(.*)$)");

    std::smatch match_results;

    if (std::regex_search(line, match_results, re_position_spec)) {
      auto position = match_results[1].str();
      auto compression_index = match_results[2].matched ? parseNumber<unsigned int>(
          match_results[2].first+1,
          match_results[2].second-1) : -1;
      bool has_specified_name = match_results[3].matched && !match_results[3].str().empty();

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

    const std::regex re_space("\\s+");
    const std::regex re_number(R"(^(\*|[+-]\d+|0[xX][0-9a-fA-F]+|\d+)+$)");
    for (
        auto it = std::sregex_token_iterator(begin(costs_line), end(costs_line), re_space, -1);
        it != std::sregex_token_iterator();
        ++it) {
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
      auto sub_position = parseSubPosition(begin(tmp), end(tmp), subposition_index);
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
    const std::regex re_call_line(R"(^calls=\s*(.+)\s+(.+))");

    std::smatch match_results;
    if (std::regex_search(line, match_results, re_call_line)) {
      auto n_calls = parseNumber<unsigned int>(match_results[1].first, match_results[1].second);

      std::vector<SubPosition> sub_positions;
      std::stringstream ss(match_results[2].str());
      int subposition_index = 0;
      for (auto &&_: positions_def) {
        std::string tmp;
        ss >> tmp;
        sub_positions.emplace_back(parseSubPosition(begin(tmp), end(tmp), subposition_index));
        subposition_index++;
      }

      return {{n_calls, sub_positions}};
    }

    return {};
  }

  std::shared_ptr<Entry> newEntry() {
    auto result = std::make_shared<Entry>();
    return result;
  }

 public:
  void SetVerbose(bool verbose) {
    CallgrindParser::verbose_ = verbose;
  }

  void Summary() {
    using std::cout;
    using std::endl;

    cout << "Entries: " << entries.size() << "; " << endl;
    cout << "Unique positions: " << positions_cache_.size() << "; " << endl;
  }

 private:

  std::vector<std::string> events_def;
  std::vector<std::string> positions_def;
  std::vector<SubPosition> current_subposition;

  std::map<unsigned int, std::string> file_compression_cache_;
  std::map<unsigned int, std::string> symbol_compression_cache_;
  std::map<unsigned int, std::string> object_compression_cache_;

  std::string current_line;



  Position current_position;







  std::map<std::pair<std::string, unsigned int>, std::string> position_compression_cache_;


  std::string filename;

  State current_state{State::Initialization};
  EntryPtr current_entry{nullptr};
  EntryPtr current_call_entry{nullptr};
  EntryPtr costs_target{nullptr};



  std::vector<std::shared_ptr<Entry>> entries;
  std::vector<std::shared_ptr<Position>> positions_cache_;

  bool verbose_{true};

};

#endif //CALLGRIND_VIEWER__CALLGRINDPARSER_HPP_
