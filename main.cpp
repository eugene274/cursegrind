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

#include <form.h>
#include <ncurses.h>

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>
#include <utility>

#include "CallgrindParser.hpp"

std::string short_path(const std::string &f) {
  namespace fs = std::filesystem;
  fs::path p(f);
  return p.filename();
}

struct ListView {
  explicit ListView(std::shared_ptr<CallgrindParser> parser)
      : parser(std::move(parser)) {}
  ~ListView() { delwin(window); }

 public:
  enum NameViewMode { kObject, kFilename };
  enum CostViewMode { kRelative, kAbsolute };

  void destroy() { delwin(window); }

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
    init_pair(3, COLOR_BLACK, COLOR_YELLOW);

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

        std::string position = name_repr == kObject ? entry->position->binary
                                                    : entry->position->source;
        std::string function = entry->position->symbol;
        std::string delim = " : ";

        std::stringstream costs_stream;
        if (costs_repr == kRelative) {
          costs_stream << std::setw(10) << std::setprecision(2)
                       << 100 * double(entry_cost) / max_cost << "%";
        } else {
          costs_stream << std::setw(10) << std::setprecision(4)
                       << double(entry_cost);
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

        mvwprintw(window, iline, 1, "%s%s%s", costs_stream.str().c_str(),
                  delim.c_str(), name.c_str());
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

  void shiftSelection(int pos) {
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

  void shiftPage(int d) {
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

  void shiftSelectedLineOffset(int pos) {
    if (pos < 0 && selected_line_offset < -pos) {
      selected_line_offset = 0;
    } else {
      selected_line_offset += pos;
    }
    render();
  }

  void resetSelectedLineOffset() {
    selected_line_offset = 0;
    render();
  }

  void toggleNameRepr() {
    if (name_repr == kObject)
      name_repr = kFilename;
    else
      name_repr = kObject;
    render();
  }

  void toggleCostsRepr() {
    if (costs_repr == kRelative)
      costs_repr = kAbsolute;
    else
      costs_repr = kRelative;
    render();
  }

  int dispatch(int ch) {
    switch (ch) {
      case 'j':
      case KEY_DOWN:
        shiftSelection(1);
        break;
      case 'k':
      case KEY_UP:
        shiftSelection(-1);
        break;
      case 'l':
      case KEY_RIGHT:
        shiftSelectedLineOffset(1);
        break;
      case 'h':
      case KEY_LEFT:
        shiftSelectedLineOffset(-1);
        break;
      case '^':
      case KEY_HOME:
        resetSelectedLineOffset();
        break;
      case KEY_NPAGE:
      case 'f':
        shiftPage(1);
        break;
      case KEY_PPAGE:
      case 'b':
        shiftPage(-1);
        break;
      case 'F':
        toggleNameRepr();
        break;
      case 'C':
        toggleCostsRepr();
        break;
      default:;
    }
    return 0;
  }

 private:
  inline int getNumberOfLines() const { return getmaxy(window) - 2; }

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

struct ItemView {
  ~ItemView() {
    if (window) {
      delwin(window);
      window = nullptr;
    }
  }
  void render() {
    auto height = 5;
    auto width = COLS - 1;

    if (!window) {
      window = newwin(height, width, LINES - 5, 1);
      keypad(window, true);
    } else {
      height = getmaxy(window);
      width = getmaxx(window);
    }

    wclear(window);
    box(window, 0, 0);
    mvwprintw(window, 1, 1, "%s", message.c_str());

    wrefresh(window);
  }

  std::string message;
  WINDOW *window{nullptr};
};

struct TreeView {
  enum CostsView { kAbsolute, kPersentage };
  enum ENameView { kSymbolOnly, kFileSymbol, kObjectSymbol };

  struct TreeNode {
    int level{0};
    bool expandable{true};
    bool selectable{true};
    std::vector<std::shared_ptr<TreeNode> > children;
    std::function<std::string(int, int)> render_string;
    std::function<void()> on_expand;

    bool is_expanded{false};
    bool is_selected{false};
    bool is_highlighted{false};
  };

  using TreeNodePtr = std::shared_ptr<TreeNode>;

  explicit TreeView(std::shared_ptr<CallgrindParser> parser)
      : parser(std::move(parser)) {}
  ~TreeView() { destroy(); }

  void render() {
    static std::string expand_symbol = "[+]";
    static std::string collapse_symbol = "[-]";
    static std::string nonexpandable_symbol = " * ";

    auto height = LINES - 6;
    auto width = COLS - 1;

    if (!window) {
      window = newwin(height, width, 1, 1);
      keypad(window, true);
    } else if (getmaxx(window) != COLS - 1 || getmaxy(window) != LINES - 1) {
      destroy();
      window = newwin(height, width, 1, 1);
      keypad(window, true);
    } else {
      height = getmaxy(window);
      width = getmaxx(window);
    }

    init_pair(1, COLOR_WHITE, COLOR_BLACK);
    init_pair(2, COLOR_BLACK, COLOR_WHITE);
    init_pair(3, COLOR_BLACK, COLOR_YELLOW);

    wclear(window);

    if (!nodes_initialized) {
      initNodes();
      auto first_selectable_it = std::find_if(
          begin(nodes), end(nodes),
          [](TreeNodePtr &nodeptr) { return nodeptr->selectable; });
      selected_inode = std::distance(begin(nodes), first_selectable_it);
      nodes[selected_inode]->is_selected = true;
      nodes_initialized = true;
    }

    renderSearchForm();
    box(window, 0, 0);

    auto actual_width = width - 2;
    auto actual_height = height - (search_activated ? 2 : 1);

    if (selected_inode - offset_inode >= actual_height - 2) {
      offset_inode = selected_inode - (actual_height - 2);
    } else if (selected_inode < offset_inode) {
      offset_inode = selected_inode;
    }
    size_t inode = offset_inode;
    for (int iline = 1; iline < actual_height && inode < nodes.size();
         ++iline, ++inode) {
      auto &node = *nodes[inode];
      std::stringstream line_text;

      std::string bullet_symbol =
          node.expandable ? node.is_expanded ? collapse_symbol : expand_symbol
                          : nonexpandable_symbol;

      line_text << node.render_string(0, 0);

      auto left_offset = 1 + 2 * node.level;
      auto text_width = actual_width - left_offset - 1 - bullet_symbol.length();
      auto text = line_text.str().substr(0, text_width);
      mvwprintw(window, iline, left_offset, "%s", bullet_symbol.c_str());
      if (node.is_selected) {
        wattron(window, COLOR_PAIR(2));
      } else if (node.is_highlighted) {
        wattron(window, COLOR_PAIR(3));
      }
      mvwprintw(window, iline, left_offset + 1 + bullet_symbol.length(), "%s",
                text.c_str());
      wattron(window, COLOR_PAIR(1));
    }

    wrefresh(window);
    setMessage(nodes[selected_inode]->render_string(0, 0));
  }

  int dispatch(int) {
    int ch = wgetch(window);
    if (search_activated) {
      switch (ch) {
        case KEY_LEFT:
          form_driver(search_form, REQ_PREV_CHAR);
          break;
        case KEY_RIGHT:
          form_driver(search_form, REQ_NEXT_CHAR);
          break;
        case 127:
        case '\b':
        case KEY_BACKSPACE:
          form_driver(search_form, REQ_DEL_PREV);
          break;
        case '\n':
        case KEY_ENTER:
          form_driver(search_form, REQ_VALIDATION);
          /* populate search results */
          doSearch();
        case 27 /*ESCAPE */:
          /* cancel search */
          search_activated = false;
          form_driver(search_form, REQ_CLR_FIELD);
          render();
          break;
        case KEY_F(10):
          return -1;
          break;
        default:
          form_driver(search_form, ch);
      }
      return 0;
    }
    switch (ch) {
      case 'e':
      case 'l':
      case KEY_RIGHT:
        expand_selected();
        break;
      case 'h':
      case KEY_LEFT:
        collapse_selected();
        break;
      case 'j':
      case KEY_DOWN:
        nextSelectable();
        break;
      case 'k':
      case KEY_UP:
      case 'p':
        prevSelectable();
        break;
      case 'v':
        toggleNameView();
        break;
      case 'c':
        toggleCostsView();
        break;
      case '/':
        search_activated = true;
        render();
        break;
      case KEY_F(10):
        return -1;
      default:;
    }
    return 0;
  }

  void destroy() {
    if (search_form) {
      unpost_form(search_form);
      free_form(search_form);
      search_form = nullptr;
      for (auto field_ptr : search_fields) {
        free_field(field_ptr);
      }
      search_fields.clear();
    }
    if (window) delwin(window);
  }

  void SetItemView(const std::shared_ptr<ItemView> &item_view) {
    TreeView::item_view = item_view;
  }

 private:
  void expand_selected() {
    auto current_node = nodes[selected_inode];
    if (!current_node->expandable) {
      return;
    }
    if (current_node->is_expanded) return;
    if (current_node->on_expand) {
      current_node->on_expand();
    }
    current_node->is_expanded = true;
    for (auto &child : current_node->children) {
      child->level = current_node->level + 1;
    }
    nodes.insert(begin(nodes) + selected_inode + 1,
                 begin(current_node->children), end(current_node->children));
    render();
  }

  void collapse_selected() {
    auto current_node = nodes[selected_inode];
    if (!current_node->is_expanded) /* already collapsed */
      return;
    current_node->is_expanded = false;
    auto current_node_it = begin(nodes) + selected_inode;
    auto collapse_begin = current_node_it + 1;
    auto collapse_end = std::find_if(collapse_begin, end(nodes),
                                     [&current_node](TreeNodePtr ptr) {
                                       return current_node->level >= ptr->level;
                                     });
    nodes.erase(collapse_begin, collapse_end);
    render();
  }

  TreeNodePtr makeCallerNode(const CallgrindParser::EntryPtr &caller) {
    auto new_node = std::make_shared<TreeNode>();
    new_node->expandable = false;
    new_node->selectable = false;
    new_node->render_string = [this, caller, new_node](int, int) {
      std::stringstream text_stream;
      text_stream << "< ";  // add n-called and stats

      if (name_view == kSymbolOnly) {
        text_stream << caller->position->symbol;
      } else if (name_view == kFileSymbol) {
        text_stream << short_path(caller->position->source)
                    << ":::" << caller->position->symbol;
      } else if (name_view == kObjectSymbol) {
        text_stream << short_path(caller->position->binary)
                    << ":::" << caller->position->symbol;
      }
      return text_stream.str();
    };

    return new_node;
  }

  TreeNodePtr makeCallNode(const CallgrindParser::EntryPtr &parent,
                           const CallgrindParser::Call &call) {
    auto new_node = std::make_shared<TreeNode>();
    new_node->expandable = true;
    new_node->selectable = true;
    new_node->render_string = [this, parent, new_node, call](int, int) {
      std::stringstream text_stream;
      text_stream << "> [calls=" << std::setprecision(2) << double(call.ncalls)
                  << "] ";
      if (costs_view == kAbsolute) {
        text_stream << "[Ir=" << std::setprecision(2)
                    << double(call.totalCosts()[0]) << "] ";
      } else {
        text_stream << "[" << std::setprecision(2)
                    << 100 * double(call.totalCosts()[0]) /
                           parent->totalCost()[0]
                    << "%] ";
      }

      if (name_view == kSymbolOnly) {
        text_stream << call.entry->position->symbol;
      } else if (name_view == kFileSymbol) {
        text_stream << short_path(call.entry->position->source)
                    << ":::" << call.entry->position->symbol;
      } else if (name_view == kObjectSymbol) {
        text_stream << short_path(call.entry->position->binary)
                    << ":::" << call.entry->position->symbol;
      }
      return text_stream.str();
    };
    auto call_entry = call.entry;
    if (call_entry->calls.empty()) {
      new_node->expandable = false;
      return new_node;
    }
    new_node->on_expand = [this, call_entry, new_node]() {
      new_node->children.clear();
      auto calls = call_entry->calls;
      std::sort(begin(calls), end(calls),
                [](const CallgrindParser::Call &lhs,
                   const CallgrindParser::Call &rhs) {
                  return lhs.totalCosts()[0] > rhs.totalCosts()[0];
                });
      for (auto &call : calls) {
        new_node->children.emplace_back(makeCallNode(call_entry, call));
      }
    };
    return new_node;
  }

  TreeNodePtr makeEntryNode(const CallgrindParser::EntryPtr &entry) {
    auto new_node = std::make_shared<TreeNode>();
    new_node->expandable = true;
    new_node->selectable = true;
    new_node->is_expanded = false;

    new_node->render_string = [this, entry, new_node](int, int) -> std::string {
      std::stringstream text_stream;
      if (costs_view == kAbsolute) {
        text_stream << "[" << std::setw(7) << std::setprecision(2)
                    << double(entry->totalCost()[0]) << "] ";
      } else if (costs_view == kPersentage) {
        text_stream << "[" << std::setw(7) << std::setprecision(2)
                    << 100 * double(entry->totalCost()[0]) /
                           parser->getEntries()[0]->totalCost()[0]
                    << "%] ";
      }

      if (name_view == kSymbolOnly) {
        text_stream << entry->position->symbol;
      } else if (name_view == kFileSymbol) {
        text_stream << short_path(entry->position->source)
                    << ":::" << entry->position->symbol;
      } else if (name_view == kObjectSymbol) {
        text_stream << short_path(entry->position->binary)
                    << ":::" << entry->position->symbol;
      }
      return text_stream.str();
    };

    if (entry->calls.empty()) {
      new_node->expandable = false;
      new_node->is_expanded = false;
      return new_node;
    }

    new_node->on_expand = [this, entry, new_node]() {
      new_node->children.clear();
      auto callers = entry->callers;
      for (const auto &caller : callers) {
        new_node->children.emplace_back(makeCallerNode(caller.lock()));
      }
      auto calls = entry->calls;
      std::sort(begin(calls), end(calls),
                [](const CallgrindParser::Call &lhs,
                   const CallgrindParser::Call &rhs) {
                  return lhs.totalCosts()[0] > rhs.totalCosts()[0];
                });
      for (auto &call : calls) {
        new_node->children.emplace_back(makeCallNode(entry, call));
      }
    };
    return new_node;
  }

  void initNodes() {
    auto &entries = parser->getEntries();

    for (auto &entry : entries) {
      nodes.emplace_back(makeEntryNode(entry));
    }
  }

  void nextSelectable() {
    auto current_node_it = begin(nodes) + selected_inode;
    auto next_selectable_node_it =
        std::find_if(current_node_it + 1, end(nodes),
                     [](const TreeNodePtr &n) { return n->selectable; });
    if (next_selectable_node_it == end(nodes)) {
      /* ignore */
    } else {
      nodes[selected_inode]->is_selected = false;
      selected_inode = std::distance(begin(nodes), next_selectable_node_it);
      nodes[selected_inode]->is_selected = true;
    }
    render();
  }

  void prevSelectable() {
    if (selected_inode == 0) {
      return;
    }
    auto current_node_rit = rbegin(nodes) + (nodes.size() - 1 - selected_inode);
    auto prev_selectable_node_it =
        std::find_if(current_node_rit + 1, rend(nodes),
                     [](const TreeNodePtr &n) { return n->selectable; });
    if (prev_selectable_node_it == rend(nodes)) {
      /* ignore */
    } else {
      nodes[selected_inode]->is_selected = false;
      selected_inode = nodes.size() -
                       std::distance(rbegin(nodes), prev_selectable_node_it) -
                       1;
      nodes[selected_inode]->is_selected = true;
    }

    render();
  }

  void toggleNameView() {
    if (name_view == kSymbolOnly) {
      name_view = kFileSymbol;
    } else if (name_view == kFileSymbol) {
      name_view = kObjectSymbol;
    } else if (name_view == kObjectSymbol) {
      name_view = kSymbolOnly;
    }
    render();
  }

  void toggleCostsView() {
    if (costs_view == kAbsolute)
      costs_view = kPersentage;
    else if (costs_view == kPersentage) {
      costs_view = kAbsolute;
    }
    render();
  }

  void renderSearchForm() {
    if (!search_form) {
      search_fields = {
          new_field(1 /* height */, getmaxx(window) - 11 - 2 /* width */,
                    getmaxy(window) - 2 /* startpos y */, 11 /* startposx */, 0,
                    1),
          nullptr};
      search_form = new_form(search_fields.data());
      set_form_sub(search_form, window);
    }

    if (search_activated) {
      post_form(search_form);

      init_pair(12, COLOR_YELLOW, COLOR_BLACK);
      wattron(window, COLOR_PAIR(12));
      mvwprintw(window, getmaxy(window) - 2, 1, "Search: ");
      wattroff(window, COLOR_PAIR(12));
    }
  }

  void resetHighlights() {
    for (auto &node_ptr : nodes) {
      node_ptr->is_highlighted = false;
    }
  }

  void doSearch() {
    auto buffer_begin = field_buffer(search_fields[0], 0);
    auto buffer_length = strlen(buffer_begin);
    auto buffer_end = buffer_begin + buffer_length;

    while (std::isspace(*buffer_begin)) buffer_begin++;
    while (buffer_end > buffer_begin && std::isspace(*(buffer_end - 1)))
      buffer_end--;

    std::string search_string{buffer_begin, buffer_end};

    resetHighlights();
    if (search_string.empty()) {
      return;
    }
    for (auto &node_ptr : nodes) {
      if (node_ptr->render_string(0, 0).find(search_string, 0) !=
          std::string::npos) {
        node_ptr->is_highlighted = true;
      }
    }

    /* move selector to the first highlighted entry */
    auto first_highlighted =
        std::find_if(begin(nodes), end(nodes), [](const TreeNodePtr &node) {
          return node->is_highlighted && node->selectable;
        });
    if (first_highlighted != end(nodes)) {
      nodes[selected_inode]->is_selected = false;
      selected_inode = std::distance(begin(nodes), first_highlighted);
      nodes[selected_inode]->is_selected = true;
    }
  }

  void setMessage(const std::string &message) const {
    if (item_view) {
      item_view->message = message;
      item_view->render();
    }
  }

  ENameView name_view{kSymbolOnly};
  CostsView costs_view{kAbsolute};
  long selected_inode{0};
  long offset_inode{0};

  WINDOW *window{nullptr};
  std::shared_ptr<const CallgrindParser> parser{};

  bool nodes_initialized{false};
  std::vector<std::shared_ptr<TreeNode> > nodes;

  bool search_activated{false};
  std::vector<FIELD *> search_fields;
  FORM *search_form{nullptr};

  std::shared_ptr<ItemView> item_view;
};

int main(int argc, char *argv[]) {
  if (argc == 1) return 1;

  std::string file_to_process{argv[1]};

  initscr(); /* Start curses mode 		*/

  if (!has_colors()) {
    std::cerr << "Colors are not supported by your terminal" << std::endl;
    exit(1);
  }
  start_color();

  cbreak();             /* Line buffering disabled, Pass on
                         * everty thing to me 		*/
  keypad(stdscr, TRUE); /* I need that nifty F1 	*/
  /* param 1     : color name
   * param 2, 3, 4 : rgb content min = 0, max = 1000 */
  //  assert(init_color(color_test, 500, 500, 500) != ERR);

  noecho();

  printw("Press F10 to exit");
  refresh();

  auto parser = std::make_shared<CallgrindParser>(file_to_process);
  //  auto list_view = std::make_shared<ListView>(parser);
  auto tree_view = std::make_shared<TreeView>(parser);
  auto item_view = std::make_shared<ItemView>();
  tree_view->SetItemView(item_view);
  parser->SetVerbose(false);
  parser->parse();

  tree_view->render();
  item_view->render();

  int ch = 1;
  while (true) {
    //    list_view->dispatch(ch);
    if (0 != tree_view->dispatch(ch)) {
      break;
    }
  }

  //  list_view->destroy();
  tree_view->destroy();
  endwin(); /* End curses mode		  */
  return 0;
}
