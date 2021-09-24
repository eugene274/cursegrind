#include <iostream>
#include <cassert>
#include <ncurses.h>

#include "CallgrindParser.hpp"

int main(int argc, char *argv[]) {
  WINDOW *my_win;

  if (argc == 1)
    return 1;

  std::string file_to_process{argv[1]};
  CallgrindParser parser(file_to_process);
  parser.SetVerbose(false);
  parser.parse();


  int ch;

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


  printw("Press F1 to exit");
  refresh();


  auto draw_main = [&parser] (
      int entry_offset = 0,
      int line_selected = 1) {

    init_pair(1, COLOR_WHITE, COLOR_BLACK);
    init_pair(2, COLOR_BLACK, COLOR_WHITE);

    auto height = LINES - 1;
    auto width = COLS - 1;
    auto main_win = newwin(height, width, 1, 1);
    box(main_win, 0, 0);

    int top_margin = 2;
    int bottom_margin = 2;
    auto nlines = height - bottom_margin - top_margin;

    entry_offset = nlines >= parser.getEntries().size()? 0 : entry_offset;
    int ientry = entry_offset;

    auto max_cost = parser.getEntries()[0]->totalCost()[0];
    for (int iline = 1; iline < height-bottom_margin;++iline) {
      if (ientry < parser.getEntries().size()) {
        const auto& entry = parser.getEntries()[ientry];
        auto entry_cost = entry->totalCost()[0];
        if (iline == line_selected) {
          wattron(main_win, COLOR_PAIR(2));
        } else {
          wattron(main_win, COLOR_PAIR(1));
        }
        mvwprintw(
            main_win,
            iline, 2, "%4u\t%u:\t%s/%s",
            100 * entry_cost / max_cost,
            entry_cost,
            entry->position->ob.c_str(), entry->position->fn.c_str());
      } else {
        break;
      }
      ientry++;
    }
    wrefresh(main_win);
    return main_win;
  };


  int current_offset = 0;
  auto main_win = draw_main(current_offset);

  auto shift_win = [&main_win,&current_offset,&draw_main] (int n) {
    if (n == 0 ) {
      return;
    }
    current_offset += n;
    if (current_offset < 0) {
      current_offset = 0;
    }
    wrefresh(main_win);
    delwin(main_win);
    main_win = draw_main(current_offset);
  };


  while ((ch = getch()) != KEY_F(1)) {
    switch (ch) {
      case KEY_DOWN:
        shift_win(1);
        break;
      case KEY_UP:
        shift_win(-1);
        break;
      case 'f':
        shift_win(10);
        break;
      case 'b':
        shift_win(-10);
        break;
      default:;
    }
  }

  attroff(COLOR_PAIR(1));
  endwin();            /* End curses mode		  */
  return 0;
}

