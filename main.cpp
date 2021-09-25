#include <iostream>
#include <cassert>
#include <ncurses.h>
#include <thread>
#include <mutex>
#include <filesystem>
#include <utility>

#include "CallgrindParser.hpp"

std::string short_path(const std::string &f) {
  namespace fs = std::filesystem;
  fs::path p(f);
  return p.filename();
}

struct ViewEntries {
  explicit ViewEntries(std::shared_ptr<CallgrindParser> parser) : parser(std::move(parser)) {}
  ~ViewEntries() {
    delwin(window);
  }

 public:
  enum NameViewMode {
    kObject, kFilename
  };
  enum CostViewMode {
    kRelative, kAbsolute
  };

  void destroy() {
    delwin(window);
  }

  void render() {
    render_lock.lock();
    auto height = LINES - 1;
    auto width = COLS - 1;
    if (!window) {
      window = newwin(height, width, 1, 1);
    } else {
      height = getmaxy(window);
      width = getmaxx(window);
    }

    init_pair(1, COLOR_WHITE, COLOR_BLACK);
    init_pair(2, COLOR_BLACK, COLOR_WHITE);

    wclear(window);
    box(window, 0, 0);

    auto actual_width = width - 2;
    const auto nlines = getNumberOfLines();

    if (!parser || parser->getEntries().empty()) {
      wrefresh(window);
      render_lock.unlock();
      return;
    }

    auto &entries = parser->getEntries();

    int ientry = entry_offset;
    auto max_cost = entries[0]->totalCost()[0];
    for (int iline = 1; iline <= nlines; ++iline) {
      if (ientry < entries.size()) {
        const auto &entry = entries.at(ientry);
        auto entry_cost = entry->totalCost()[0];
        if (iline == selected_line) {
          wattron(window, COLOR_PAIR(2));
        } else {
          wattron(window, COLOR_PAIR(1));
        }

        std::string position = name_repr == kObject ? entry->position->ob : entry->position->fl;
        std::string function = entry->position->fn;
        std::string delim = " : ";

        std::stringstream costs_stream;
        if (costs_repr == kRelative) {
          costs_stream << std::setw(10) << std::setprecision(2) << 100 * double(entry_cost) / max_cost << "%";
        } else {
          costs_stream << std::setw(10) << std::setprecision(4) << double(entry_cost);
        }
        std::string costs = costs_stream.str();

        auto name = short_path(position) + "/" + function;
        auto max_name_width = actual_width - delim.size() - costs.size();
        if (name.size() > max_name_width) {
          if (iline == selected_line) {
            auto max_line_offset = name.size() - max_name_width;
            if (selected_line_offset > max_line_offset) {
              selected_line_offset = max_line_offset;
            }
            name = name.substr(selected_line_offset, max_name_width);
            if (selected_line_offset < max_line_offset) {
              name[name.length() - 1] = '>';
            }
          } else {
            name = name.substr(0, actual_width - 3 - costs.size());
            name[name.length() - 1] = '>';
          }
          if (iline == selected_line && selected_line_offset > 0) {
            name[0] = '<';
          }
        }

        mvwprintw(
            window,
            iline, 1,
            "%s%s%s",
            costs_stream.str().c_str(),
            delim.c_str(),
            name.c_str());
        lastline = iline;
      } else {
        break;
      }
      ientry++;
    }
    wattron(window, COLOR_PAIR(1));
    wrefresh(window);
    render_lock.unlock();
  }

  void shift_selection(int pos) {
    selected_line += pos;
    if (entry_offset + selected_line >= parser->getEntries().size()) {
      selected_line = parser->getEntries().size() - entry_offset;
    } else if (selected_line >= lastline) {
      selected_line = lastline - 1;
      if (entry_offset < parser->getEntries().size() - 1) entry_offset++;
    } else if (selected_line < 1) {
      selected_line = 1;
      if (entry_offset > 0) entry_offset--;
    }
    selected_line_offset = 0;
    render();
  }

  void shift_page(int d) {
    if (d > 0) {
      if (entry_offset + getNumberOfLines() >= parser->getEntries().size()) {
        /* do nothing */
      } else {
        entry_offset += getNumberOfLines();
        if (entry_offset + selected_line >= parser->getEntries().size()) {
          selected_line = parser->getEntries().size() - entry_offset;
        }
      }
    } else if (d < 0) {
      if (entry_offset - getNumberOfLines() < 0) {
        entry_offset = 0;
      } else {
        entry_offset -= getNumberOfLines();
      }
    }
    selected_line_offset = 0;
    render();
  }

  void shift_selected_line_offset(int pos) {
    if (pos < 0 && selected_line_offset < -pos) {
      selected_line_offset = 0;
    } else {
      selected_line_offset += pos;
    }
    render();
  }

  void reset_selected_line_offset() {
    selected_line_offset = 0;
    render();
  }

  void toggle_name_repr() {
    if (name_repr == kObject)
      name_repr = kFilename;
    else
      name_repr = kObject;
    render();
  }

  void toggle_costs_repr() {
    if (costs_repr == kRelative)
      costs_repr = kAbsolute;
    else
      costs_repr = kRelative;
    render();
  }

  int dispatch(int ch) {
    switch (ch) {
      case 'j':
      case KEY_DOWN:shift_selection(1);
        break;
      case 'k':
      case KEY_UP:shift_selection(-1);
        break;
      case 'l':
      case KEY_RIGHT: shift_selected_line_offset(1);
        break;
      case 'h':
      case KEY_LEFT: shift_selected_line_offset(-1);
        break;
      case '^':
      case KEY_HOME: reset_selected_line_offset();
        break;
      case KEY_NPAGE:
      case 'f':shift_page(1);
        break;
      case KEY_PPAGE:
      case 'b':shift_page(-1);
        break;
      case 'F':toggle_name_repr();
        break;
      case 'C':toggle_costs_repr();
        break;
      default:;
    }
    return 0;
  }

 private:
  inline int getNumberOfLines() const {
    return getmaxy(window) - 2;
  }

  std::mutex render_lock;
  WINDOW *window{nullptr};
  std::shared_ptr<CallgrindParser> parser{};

  int name_repr{0};
  int costs_repr{kRelative};
  int entry_offset{0};
  size_t selected_line_offset{0};
  size_t selected_line{1};
  int lastline{1};
};

int main(int argc, char *argv[]) {
  if (argc == 1)
    return 1;

  std::string file_to_process{argv[1]};

  initscr();            /* Start curses mode 		*/

  if (!has_colors()) {
    std::cerr << "Colors are not supported by your terminal" << std::endl;
    exit(1);
  }
  start_color();

  cbreak();            /* Line buffering disabled, Pass on
					 * everty thing to me 		*/
  keypad(stdscr, TRUE);        /* I need that nifty F1 	*/
  /* param 1     : color name
  * param 2, 3, 4 : rgb content min = 0, max = 1000 */
//  assert(init_color(color_test, 500, 500, 500) != ERR);

  noecho();

  printw("Press F10 to exit");
  refresh();

  auto parser = std::make_shared<CallgrindParser>(file_to_process);
  auto entries_view = std::make_shared<ViewEntries>(parser);
  parser->SetVerbose(false);


  std::thread parse_thread([](
      std::shared_ptr<CallgrindParser> parser,
      std::shared_ptr<ViewEntries> entries_view
      ) {
    parser->parse();
    entries_view->render();
  }, parser, entries_view);
  parse_thread.detach();

  entries_view->render();
  int ch;
  while ((ch = getch()) != KEY_F(10)) {
    entries_view->dispatch(ch);
  }

  entries_view->destroy();
  endwin();            /* End curses mode		  */
  return 0;
}

