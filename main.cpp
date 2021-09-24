#include <iostream>
#include <cassert>
#include <ncurses.h>

#include "CallgrindParser.hpp"

WINDOW *create_newwin(int height, int width, int starty, int startx);
void destroy_win(WINDOW *local_win);

int main(int argc, char *argv[]) {
  WINDOW *my_win;

  if (argc == 1)
    return 1;

  std::string file_to_process{argv[1]};
  CallgrindParser parser(file_to_process);
  parser.SetVerbose(false);
  parser.parse();


  int startx, starty, width, height;
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

  short color_test = 8;
  /* param 1     : color name
  * param 2, 3, 4 : rgb content min = 0, max = 1000 */
  assert(init_color(color_test, 500, 500, 500) != ERR);


  height = LINES - 1;
  width = COLS - 1;
  starty = 1;   /* Calculating for a center placement */
  startx = 1;    /* of the window		*/
  printw("Press F1 to exit");
  refresh();

  init_pair(1, COLOR_RED, COLOR_BLUE);
  attron(COLOR_PAIR(1));
  my_win = create_newwin(height, width, starty, startx);

  /* header */
  wprintw(my_win, "\t%s", file_to_process.c_str());

  auto max_cost = parser.getEntries()[0]->totalCost()[0];

  int entry_offset = 0;
  int ientry = entry_offset;
  int top_margin = 2;
  int bottom_margin = 2;
  for (int iline = top_margin; iline < height-bottom_margin;++iline) {
    if (ientry < parser.getEntries().size()) {
      const auto& entry = parser.getEntries()[ientry];
      auto entry_cost = entry->totalCost()[0];
      mvprintw(iline, 2, "%4u\t%u:\t%s/%s",
               100 * entry_cost / max_cost,
               entry_cost,
               entry->position->ob.c_str(), entry->position->fn.c_str());
    } else {
      break;
    }
    ientry++;
  }


  wrefresh(my_win);
  while ((ch = getch()) != KEY_F(1)) {

  }

  attroff(COLOR_PAIR(1));
  endwin();            /* End curses mode		  */
  return 0;
}

WINDOW *create_newwin(int height, int width, int starty, int startx) {
  WINDOW *local_win;

  local_win = newwin(height, width, starty, startx);
  init_pair(2, COLOR_GREEN, 8);
  wattr_on(local_win, COLOR_PAIR(2), nullptr);
  box(local_win, 0, 0);        /* 0, 0 gives default characters
					 * for the vertical and horizontal
					 * lines			*/
  wrefresh(local_win);        /* Show that box 		*/

  return local_win;
}

void destroy_win(WINDOW *local_win) {
  /* box(local_win, ' ', ' '); : This won't produce the desired
   * result of erasing the window. It will leave it's four corners
   * and so an ugly remnant of window.
   */
  wborder(local_win, ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ');
  /* The parameters taken are
   * 1. win: the window on which to operate
   * 2. ls: character to be used for the left side of the window
   * 3. rs: character to be used for the right side of the window
   * 4. ts: character to be used for the top side of the window
   * 5. bs: character to be used for the bottom side of the window
   * 6. tl: character to be used for the top left corner of the window
   * 7. tr: character to be used for the top right corner of the window
   * 8. bl: character to be used for the bottom left corner of the window
   * 9. br: character to be used for the bottom right corner of the window
   */
  wattroff(local_win, COLOR_PAIR(2));
  wrefresh(local_win);
  delwin(local_win);
}